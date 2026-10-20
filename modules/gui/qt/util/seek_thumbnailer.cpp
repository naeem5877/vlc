/*****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "seek_thumbnailer.hpp"

#include "qt.hpp"

#include <vlc_common.h>
#include <vlc_preparser.h>
#include <vlc_input_item.h>
#include <vlc_picture.h>
#include <vlc_block.h>
#include <vlc_fourcc.h>
#include <vlc_es.h>

#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QTransform>

#include <atomic>
#include <memory>

namespace
{

// the position is quantized, this is the number of distinct thumbnails of a media
constexpr int BUCKET_COUNT = 200;
// never ask for the very end of a media, seeking there often yields nothing
constexpr double MAX_POSITION = 0.99;
constexpr int MAX_CACHED_FRAMES = 400;
constexpr int DEBOUNCE_MS = 120;
// frames kept alive for the image provider (QML may still be loading the previous one)
constexpr int KEEP_PUBLISHED = 3;
constexpr int MAX_THUMBNAIL_SIDE = 2048;

qt_intf_t *s_intf = nullptr;

struct Store
{
    QMutex mutex;
    QHash<QString, QImage> images;
};

Store &store()
{
    static Store s;
    return s;
}

QString nextInstanceId()
{
    static std::atomic<quint64> counter{0};
    return QString::number(counter.fetch_add(1));
}

// VLC gives the pictures without applying their orientation (phone videos
// are often stored sideways with a rotation tag)
bool orientationSwapsAxes(video_orientation_t orientation)
{
    return orientation >= ORIENT_LEFT_TOP && orientation <= ORIENT_RIGHT_BOTTOM;
}

QImage applyOrientation(const QImage &image, video_orientation_t orientation)
{
    switch (orientation)
    {
    case ORIENT_TOP_RIGHT:
        return image.mirrored(true, false);
    case ORIENT_BOTTOM_LEFT:
        return image.mirrored(false, true);
    case ORIENT_BOTTOM_RIGHT:
        return image.transformed(QTransform().rotate(180));
    case ORIENT_LEFT_TOP: // transposed
        return image.transformed(QTransform().rotate(90)).mirrored(true, false);
    case ORIENT_LEFT_BOTTOM: // 90 degrees anti-clockwise
        return image.transformed(QTransform().rotate(-90));
    case ORIENT_RIGHT_TOP: // 90 degrees clockwise
        return image.transformed(QTransform().rotate(90));
    case ORIENT_RIGHT_BOTTOM: // anti-transposed
        return image.transformed(QTransform().rotate(90)).mirrored(false, true);
    case ORIENT_TOP_LEFT:
    default:
        return image;
    }
}

// Runs on the thumbnailer thread
QImage pictureToImage(vlc_object_t *obj, picture_t *pic, int height)
{
    const video_format_t &in = pic->format;

    const unsigned visibleWidth  = in.i_visible_width  ? in.i_visible_width  : in.i_width;
    const unsigned visibleHeight = in.i_visible_height ? in.i_visible_height : in.i_height;
    if (visibleWidth == 0 || visibleHeight == 0 || height <= 0)
        return {};

    double sar = 1.0;
    if (in.i_sar_num > 0 && in.i_sar_den > 0)
        sar = double(in.i_sar_num) / double(in.i_sar_den);

    // display aspect ratio of the picture as decoded (not rotated yet)
    const double aspect = (double(visibleWidth) * sar) / double(visibleHeight);
    const video_orientation_t orientation = in.orientation;

    // size of the exported (not rotated) image, so that the rotated one is `height` high
    int w, h;
    if (orientationSwapsAxes(orientation))
    {
        w = height;
        h = qRound(height / aspect);
    }
    else
    {
        h = height;
        w = qRound(height * aspect);
    }
    w = qBound(1, w, MAX_THUMBNAIL_SIDE);
    h = qBound(1, h, MAX_THUMBNAIL_SIDE);

    QImage image;

    // raw pixels: nothing to decode
    block_t *block = nullptr;
    video_format_t out;
    if (picture_Export(obj, &block, &out, pic, VLC_CODEC_RGBA, w, h, false) == VLC_SUCCESS
        && block != nullptr)
    {
        const size_t outWidth = out.i_width;
        const size_t outHeight = out.i_height;
        if (outWidth > 0 && outHeight > 0 && block->i_buffer >= outWidth * outHeight * 4)
        {
            // convertToFormat() makes a deep copy, the block is released below
            image = QImage(block->p_buffer, int(outWidth), int(outHeight),
                           int(outWidth * 4), QImage::Format_RGBA8888)
                        .convertToFormat(QImage::Format_RGB32);
        }
        block_Release(block);
    }

    // the raw encoder lives in the avcodec module, fall back to PNG if it is not there
    if (image.isNull())
    {
        block = nullptr;
        if (picture_Export(obj, &block, nullptr, pic, VLC_CODEC_PNG, w, h, false) == VLC_SUCCESS
            && block != nullptr)
        {
            image = QImage::fromData(block->p_buffer, int(block->i_buffer), "PNG")
                        .convertToFormat(QImage::Format_RGB32);
            block_Release(block);
        }
    }

    if (image.isNull())
        return {};

    return applyOrientation(image, orientation);
}

} // anonymous namespace

//// SeekThumbnailProvider

SeekThumbnailProvider::SeekThumbnailProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage SeekThumbnailProvider::requestImage(const QString &id, QSize *size, const QSize &)
{
    QImage image;
    {
        QMutexLocker locker(&store().mutex);
        image = store().images.value(id);
    }
    if (size)
        *size = image.size();
    return image;
}

//// SeekThumbnailer

struct SeekThumbnailer::RequestCtx
{
    SeekThumbnailer *owner;
    qt_intf_t *intf;
    quint64 mediaGeneration;
    quint64 requestGeneration;
    int bucket;
    int height;
};

SeekThumbnailer::SeekThumbnailer(QObject *parent)
    : QObject(parent)
    , m_id(nextInstanceId())
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(DEBOUNCE_MS);
    connect(&m_debounce, &QTimer::timeout, this, [this]() {
        startRequest(m_pendingPosition);
    });
}

SeekThumbnailer::~SeekThumbnailer()
{
    m_debounce.stop();

    if (m_preparser)
    {
        // pending requests end with a cancellation, their results are posted
        // to this object and dropped with it
        vlc_preparser_Cancel(m_preparser, nullptr);
        vlc_preparser_Delete(m_preparser);
        m_preparser = nullptr;
    }

    clearPublished();
}

void SeekThumbnailer::setInterface(qt_intf_t *intf)
{
    s_intf = intf;
}

bool SeekThumbnailer::available() const
{
    return s_intf != nullptr && m_mediaUrl.isValid() && m_mediaUrl.isLocalFile();
}

void SeekThumbnailer::setMediaUrl(const QUrl &url)
{
    if (m_mediaUrl == url)
        return;

    m_mediaUrl = url;
    reset();
    emit mediaUrlChanged();
}

void SeekThumbnailer::setThumbnailHeight(int height)
{
    height = qBound(16, height, 1080);
    if (m_height == height)
        return;

    m_height = height;
    // cached frames have the previous size
    m_cache.clear();
    m_publishedBucket = -1;
    emit thumbnailHeightChanged();
}

int SeekThumbnailer::bucketFor(double position)
{
    return qBound(0, qRound(position * BUCKET_COUNT), BUCKET_COUNT);
}

double SeekThumbnailer::positionForBucket(int bucket)
{
    return qMin(MAX_POSITION, double(bucket) / BUCKET_COUNT);
}

void SeekThumbnailer::requestPosition(double position)
{
    if (!available())
        return;

    m_pendingPosition = position;

    const int bucket = bucketFor(position);
    const auto it = m_cache.constFind(bucket);
    if (it != m_cache.constEnd())
    {
        // already known: show it right away and forget about older requests
        m_debounce.stop();
        ++m_requestGeneration;
        if (m_preparser)
            vlc_preparser_Cancel(m_preparser, nullptr);
        publish(*it, bucket);
        return;
    }

    m_debounce.start();
}

void SeekThumbnailer::requestPositionNow(double position)
{
    if (!available())
        return;

    m_debounce.stop();
    startRequest(position);
}

void SeekThumbnailer::cancel()
{
    m_debounce.stop();
    ++m_requestGeneration;
    if (m_preparser)
        vlc_preparser_Cancel(m_preparser, nullptr);
}

void SeekThumbnailer::reset()
{
    m_debounce.stop();
    ++m_mediaGeneration;
    ++m_requestGeneration;
    if (m_preparser)
        vlc_preparser_Cancel(m_preparser, nullptr);

    m_cache.clear();
    m_publishedBucket = -1;

    const bool hadSource = !m_source.isEmpty();
    clearPublished();
    m_source.clear();
    m_aspectRatio = 0.;
    if (hadSource)
        emit sourceChanged();
}

bool SeekThumbnailer::ensurePreparser()
{
    if (m_preparser)
        return true;
    if (!s_intf)
        return false;

    struct vlc_preparser_cfg cfg{};
    cfg.types = VLC_PREPARSER_TYPE_THUMBNAIL;
    cfg.max_thumbnailer_threads = 1;
    cfg.timeout = VLC_TICK_FROM_SEC(8);
    cfg.external_process = false;

    m_preparser = vlc_preparser_New(VLC_OBJECT(s_intf), &cfg);
    return m_preparser != nullptr;
}

void SeekThumbnailer::startRequest(double position)
{
    if (!available() || !ensurePreparser())
        return;

    const int bucket = bucketFor(position);

    // it may have been generated while the request was waiting
    const auto it = m_cache.constFind(bucket);
    if (it != m_cache.constEnd())
    {
        ++m_requestGeneration;
        publish(*it, bucket);
        return;
    }

    // only the latest request matters
    ++m_requestGeneration;
    vlc_preparser_Cancel(m_preparser, nullptr);

    input_item_t *item = input_item_New(m_mediaUrl.toEncoded().constData(), nullptr);
    if (!item)
        return;

    struct vlc_thumbnailer_arg arg{};
    arg.seek.type = vlc_thumbnailer_arg::seek::VLC_THUMBNAILER_SEEK_POS;
    arg.seek.pos = positionForBucket(bucket);
    arg.seek.speed = vlc_thumbnailer_arg::seek::VLC_THUMBNAILER_SEEK_FAST;
    arg.hw_dec = false;

    static const struct vlc_thumbnailer_cbs cbs = {
        &SeekThumbnailer::onThumbnailEnded,
    };

    auto *ctx = new RequestCtx{ this, s_intf, m_mediaGeneration, m_requestGeneration,
                                bucket, m_height };

    vlc_preparser_req *req = vlc_preparser_req_NewThumbnail(m_preparser, item, &arg, &cbs, ctx);
    // the request holds the item
    input_item_Release(item);

    if (!req)
    {
        delete ctx;
        return;
    }

    // On success the running task owns the request and calls `on_ended` exactly
    // once (that is where ctx is freed). On failure no callback will be invoked.
    if (vlc_preparser_Submit(m_preparser, req) != VLC_SUCCESS)
        delete ctx;
    vlc_preparser_req_Release(req);
}

// Called from the thumbnailer thread, exactly once per submitted request
void SeekThumbnailer::onThumbnailEnded(vlc_preparser_req *, int status, picture_t *pic, void *data)
{
    std::unique_ptr<RequestCtx> ctx(static_cast<RequestCtx *>(data));

    QImage image;
    if (status == VLC_SUCCESS && pic != nullptr)
        image = pictureToImage(VLC_OBJECT(ctx->intf), pic, ctx->height);

    SeekThumbnailer *owner = ctx->owner;
    const quint64 mediaGeneration = ctx->mediaGeneration;
    const quint64 requestGeneration = ctx->requestGeneration;
    const int bucket = ctx->bucket;

    // delivered in the thread of `owner`, discarded if it is destroyed before
    QMetaObject::invokeMethod(owner, [owner, mediaGeneration, requestGeneration, bucket, image]() {
        owner->onThumbnailReady(mediaGeneration, requestGeneration, bucket, image);
    }, Qt::QueuedConnection);
}

void SeekThumbnailer::onThumbnailReady(quint64 mediaGeneration, quint64 requestGeneration,
                                       int bucket, const QImage &image)
{
    // a frame of a previous media
    if (mediaGeneration != m_mediaGeneration || image.isNull())
        return;

    if (m_cache.size() >= MAX_CACHED_FRAMES)
        m_cache.clear();
    m_cache.insert(bucket, image);

    // superseded by a newer request: keep it in the cache but do not show it
    if (requestGeneration != m_requestGeneration)
        return;

    publish(image, bucket);
}

void SeekThumbnailer::publish(const QImage &image, int bucket)
{
    if (image.isNull())
        return;
    if (bucket == m_publishedBucket && !m_source.isEmpty())
        return;

    m_publishedBucket = bucket;

    const QString id = m_id + QLatin1Char('-') + QString::number(++m_serial);
    {
        QMutexLocker locker(&store().mutex);
        store().images.insert(id, image);
        m_publishedIds.append(id);
        while (m_publishedIds.size() > KEEP_PUBLISHED)
            store().images.remove(m_publishedIds.takeFirst());
    }

    m_aspectRatio = double(image.width()) / double(image.height());
    m_source = QUrl(QStringLiteral("image://seekthumb/") + id);
    emit sourceChanged();
}

void SeekThumbnailer::clearPublished()
{
    QMutexLocker locker(&store().mutex);
    for (const QString &id : std::as_const(m_publishedIds))
        store().images.remove(id);
    m_publishedIds.clear();
}
