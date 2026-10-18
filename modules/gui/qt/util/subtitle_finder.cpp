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

#include "subtitle_finder.hpp"

#include "vlcaccess_image_provider.hpp"
#include "qt.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QThreadPool>
#include <QThread>
#include <QVariantMap>

namespace {

// Public, keyless Stremio addon endpoints (the same ones Stremio itself uses)
const QString CINEMETA_URL = QStringLiteral("https://v3-cinemeta.strem.io");
const QString SUBTITLES_URL = QStringLiteral("https://opensubtitles-v3.strem.io");

constexpr int MAX_RESULTS = 200;
constexpr qint64 MAX_JSON_SIZE = 8 * 1024 * 1024;
constexpr qint64 MAX_SUBTITLE_SIZE = 8 * 1024 * 1024;

const QStringList SUBTITLE_FILTERS = {
    QStringLiteral("*.srt"), QStringLiteral("*.ass"), QStringLiteral("*.ssa"),
    QStringLiteral("*.sub"), QStringLiteral("*.vtt"), QStringLiteral("*.smi")
};

const QStringList VIDEO_FILTERS = {
    QStringLiteral("*.mp4"), QStringLiteral("*.mkv"), QStringLiteral("*.avi"),
    QStringLiteral("*.mov"), QStringLiteral("*.wmv"), QStringLiteral("*.flv"),
    QStringLiteral("*.webm"), QStringLiteral("*.m4v"), QStringLiteral("*.mpg"),
    QStringLiteral("*.mpeg"), QStringLiteral("*.ts"), QStringLiteral("*.m2ts"),
    QStringLiteral("*.ogv"), QStringLiteral("*.3gp")
};

/** download an url through VLC's access layer (http/https/...), retrying on transient failures */
QByteArray fetchBytes(const QString &url, qint64 maxBytes, QString *error)
{
    // the public addon servers sit behind Cloudflare and now and then answer 5xx
    constexpr int attempts = 3;

    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        if (attempt > 0)
            QThread::msleep(500 * attempt);

        VLCIODevice device(url);
        if (!device.open(QIODevice::ReadOnly))
        {
            *error = QObject::tr("Unable to reach the subtitle server");
            continue;
        }

        QByteArray data;
        bool tooLarge = false;
        char buffer[16 * 1024];
        for (;;)
        {
            const qint64 n = device.read(buffer, sizeof(buffer));
            if (n <= 0)
                break;
            data.append(buffer, n);
            if (data.size() > maxBytes)
            {
                tooLarge = true;
                break;
            }
        }

        if (tooLarge)
        {
            *error = QObject::tr("The server answer is too large");
            return {};
        }

        if (!data.isEmpty())
        {
            error->clear();
            return data;
        }

        *error = QObject::tr("The subtitle server returned no data");
    }

    return {};
}

QString percentEncode(const QString &text)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(text));
}

QString cleanSeparators(QString text)
{
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    text.replace(QLatin1Char('.'), QLatin1Char(' '));
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    text.replace(spaces, QStringLiteral(" "));
    return text.trimmed();
}

struct ParsedQuery
{
    QString imdbId;
    QString title;
    bool isSeries = false;
    int season = 0;
    int episode = 0;
    QString year;
};

ParsedQuery parseQuery(const QString &rawQuery)
{
    ParsedQuery parsed;
    const QString query = rawQuery.trimmed();

    static const QRegularExpression imdbRe(QStringLiteral("^tt\\d{6,}$"));
    if (imdbRe.match(query).hasMatch())
    {
        parsed.imdbId = query;
        return parsed;
    }

    const QString cleaned = cleanSeparators(query);

    static const QRegularExpression seRe(
        QStringLiteral("^(.*?)\\s*\\b[sS](\\d{1,2})\\s*[eE](\\d{1,3})\\b"));
    static const QRegularExpression xRe(
        QStringLiteral("^(.*?)\\s*\\b(\\d{1,2})x(\\d{1,3})\\b"));

    QRegularExpressionMatch match = seRe.match(cleaned);
    if (!match.hasMatch())
        match = xRe.match(cleaned);

    if (match.hasMatch() && !match.captured(1).trimmed().isEmpty())
    {
        parsed.isSeries = true;
        parsed.title = match.captured(1).trimmed();
        parsed.season = match.captured(2).toInt();
        parsed.episode = match.captured(3).toInt();
        return parsed;
    }

    // movie, with an optional year
    static const QRegularExpression yearRe(QStringLiteral("^(.*?)\\s*[\\(\\[]?\\b((?:19|20)\\d{2})\\b[\\)\\]]?\\s*$"));
    match = yearRe.match(cleaned);
    if (match.hasMatch() && !match.captured(1).trimmed().isEmpty())
    {
        parsed.title = match.captured(1).trimmed();
        parsed.year = match.captured(2);
    }
    else
        parsed.title = cleaned;

    return parsed;
}

/** title -> imdb id using Cinemeta */
QString lookupImdbId(const ParsedQuery &parsed, QString *error)
{
    const QString type = parsed.isSeries ? QStringLiteral("series") : QStringLiteral("movie");
    const QString url = CINEMETA_URL % QStringLiteral("/catalog/") % type
                        % QStringLiteral("/top/search=") % percentEncode(parsed.title)
                        % QStringLiteral(".json");

    const QByteArray data = fetchBytes(url, MAX_JSON_SIZE, error);
    if (data.isEmpty())
        return {};

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        *error = QObject::tr("Unexpected answer from the title database");
        return {};
    }

    const QJsonArray metas = doc.object().value(QStringLiteral("metas")).toArray();
    QString fallback;
    for (const QJsonValue &value : metas)
    {
        const QJsonObject meta = value.toObject();
        QString id = meta.value(QStringLiteral("imdb_id")).toString();
        if (id.isEmpty())
            id = meta.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;

        if (fallback.isEmpty())
            fallback = id;

        if (!parsed.year.isEmpty()
            && meta.value(QStringLiteral("releaseInfo")).toString().startsWith(parsed.year))
            return id;
    }

    if (fallback.isEmpty())
        *error = QObject::tr("No title found for \"%1\"").arg(parsed.title);
    return fallback;
}

void runSearch(const QString &query, const QString &language,
               QVector<SubtitleFinder::Entry> &entries, QString &error)
{
    ParsedQuery parsed = parseQuery(query);
    if (parsed.imdbId.isEmpty() && parsed.title.isEmpty())
    {
        error = QObject::tr("Type a title to search");
        return;
    }

    QString imdbId = parsed.imdbId;
    if (imdbId.isEmpty())
    {
        imdbId = lookupImdbId(parsed, &error);
        if (imdbId.isEmpty())
            return;
    }

    QString path;
    if (parsed.isSeries)
        path = QStringLiteral("/subtitles/series/") % imdbId % QLatin1Char(':')
               % QString::number(parsed.season) % QLatin1Char(':')
               % QString::number(parsed.episode) % QStringLiteral(".json");
    else
        path = QStringLiteral("/subtitles/movie/") % imdbId % QStringLiteral(".json");

    const QByteArray data = fetchBytes(SUBTITLES_URL + path, MAX_JSON_SIZE, &error);
    if (data.isEmpty())
        return;
    error.clear();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        error = QObject::tr("Unexpected answer from the subtitle server");
        return;
    }

    QSet<QString> seenUrls;
    const QJsonArray subtitles = doc.object().value(QStringLiteral("subtitles")).toArray();
    for (const QJsonValue &value : subtitles)
    {
        const QJsonObject obj = value.toObject();
        const QString url = obj.value(QStringLiteral("url")).toString();
        const QString lang = obj.value(QStringLiteral("lang")).toString();
        if (url.isEmpty() || seenUrls.contains(url))
            continue;
        if (!language.isEmpty() && canonicalLanguage(lang) != canonicalLanguage(language))
            continue;
        seenUrls.insert(url);

        SubtitleFinder::Entry entry;
        entry.id = obj.value(QStringLiteral("id")).toString();
        entry.url = url;
        entry.language = lang;
        entry.release = obj.value(QStringLiteral("movieReleaseName")).toString();
        entry.fileName = obj.value(QStringLiteral("subtitleFileName")).toString();
        if (entry.fileName.isEmpty())
            entry.fileName = (entry.release.isEmpty() ? entry.id : entry.release) + QStringLiteral(".srt");

        entries.append(entry);
        if (entries.size() >= MAX_RESULTS)
            break;
    }

    if (entries.isEmpty())
        error = QObject::tr("No subtitles found");
}

/** ISO 639-2: the bibliographic (B) and terminologic (T) codes of a language are the same language */
QString canonicalLanguage(const QString &code)
{
    static const QHash<QString, QString> aliases = {
        { QStringLiteral("deu"), QStringLiteral("ger") },
        { QStringLiteral("fra"), QStringLiteral("fre") },
        { QStringLiteral("nld"), QStringLiteral("dut") },
        { QStringLiteral("zho"), QStringLiteral("chi") },
        { QStringLiteral("zht"), QStringLiteral("chi") },
        { QStringLiteral("ces"), QStringLiteral("cze") },
        { QStringLiteral("ell"), QStringLiteral("gre") },
        { QStringLiteral("ron"), QStringLiteral("rum") },
        { QStringLiteral("fas"), QStringLiteral("per") },
        { QStringLiteral("en"),  QStringLiteral("eng") },
        { QStringLiteral("es"),  QStringLiteral("spa") },
        { QStringLiteral("fr"),  QStringLiteral("fre") },
        { QStringLiteral("de"),  QStringLiteral("ger") },
        { QStringLiteral("it"),  QStringLiteral("ita") },
        { QStringLiteral("pt"),  QStringLiteral("por") },
        { QStringLiteral("ru"),  QStringLiteral("rus") },
        { QStringLiteral("ar"),  QStringLiteral("ara") },
        { QStringLiteral("bn"),  QStringLiteral("ben") },
        { QStringLiteral("hi"),  QStringLiteral("hin") },
        { QStringLiteral("tr"),  QStringLiteral("tur") },
    };
    const QString lower = code.toLower();
    return aliases.value(lower, lower);
}

QString sanitizeFileName(QString name)
{
    static const QRegularExpression invalid(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    name.replace(invalid, QStringLiteral("_"));
    name = name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("subtitle");
    return name;
}

/** a file name that does not exist yet in `dir` */
QString uniquePath(const QString &dir, const QString &baseName, const QString &suffix)
{
    QString candidate = dir + QLatin1Char('/') + baseName + QLatin1Char('.') + suffix;
    int i = 2;
    while (QFileInfo::exists(candidate))
    {
        candidate = dir + QLatin1Char('/') + baseName + QLatin1Char('.')
                    + QString::number(i++) + QLatin1Char('.') + suffix;
    }
    return candidate;
}

bool isWritableFolder(const QString &folder)
{
    const QFileInfo info(folder);
    return info.exists() && info.isDir() && info.isWritable();
}

QString cacheFolder()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    const QString dir = base + QStringLiteral("/subtitles");
    QDir().mkpath(dir);
    return dir;
}

QString localFolderOf(const QUrl &mediaUrl)
{
    if (!mediaUrl.isLocalFile())
        return {};
    return QFileInfo(mediaUrl.toLocalFile()).absolutePath();
}

QString episodeKey(const QString &fileName)
{
    static const QRegularExpression seRe(QStringLiteral("[sS](\\d{1,2})\\s*[eE](\\d{1,3})"));
    static const QRegularExpression xRe(QStringLiteral("\\b(\\d{1,2})x(\\d{2,3})\\b"));

    QRegularExpressionMatch match = seRe.match(fileName);
    if (!match.hasMatch())
        match = xRe.match(fileName);
    if (!match.hasMatch())
        return {};

    return QStringLiteral("S%1E%2").arg(match.captured(1).toInt(), 2, 10, QLatin1Char('0'))
                                   .arg(match.captured(2).toInt(), 2, 10, QLatin1Char('0'));
}

int subtitlePreference(const QString &fileName)
{
    // when several subtitles exist for the same episode prefer English
    const QString lower = fileName.toLower();
    static const QRegularExpression en(QStringLiteral("(english|[\\._\\-\\s\\(\\[]en(g)?[\\._\\-\\s\\)\\]]|\\.en\\.|\\.eng\\.)"));
    return en.match(lower).hasMatch() ? 0 : 1;
}

} // anonymous namespace

//// SubtitleFinder

SubtitleFinder::SubtitleFinder(QObject *parent)
    : QAbstractListModel(parent)
{
}

SubtitleFinder::~SubtitleFinder() = default;

int SubtitleFinder::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant SubtitleFinder::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry &entry = m_entries.at(index.row());
    switch (role)
    {
    case Qt::DisplayRole:
    case FileNameRole:
        return entry.fileName;
    case LanguageRole:
        return entry.language;
    case ReleaseRole:
        return entry.release;
    case SelectedRole:
        return entry.selected;
    default:
        return {};
    }
}

bool SubtitleFinder::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != SelectedRole
        || index.row() < 0 || index.row() >= m_entries.size())
        return false;

    const bool selected = value.toBool();
    Entry &entry = m_entries[index.row()];
    if (entry.selected == selected)
        return true;

    entry.selected = selected;
    emit dataChanged(index, index, {SelectedRole});
    emit selectedCountChanged();
    return true;
}

Qt::ItemFlags SubtitleFinder::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QHash<int, QByteArray> SubtitleFinder::roleNames() const
{
    return {
        {Qt::DisplayRole, "display"},
        {FileNameRole, "fileName"},
        {LanguageRole, "language"},
        {ReleaseRole, "release"},
        {SelectedRole, "selected"}
    };
}

int SubtitleFinder::selectedCount() const
{
    int count = 0;
    for (const Entry &entry : m_entries)
        if (entry.selected)
            ++count;
    return count;
}

void SubtitleFinder::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void SubtitleFinder::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void SubtitleFinder::clear()
{
    ++m_generation; // drop any pending answer
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
    emit selectedCountChanged();
    setError({});
    setLoading(false);
}

void SubtitleFinder::search(const QString &query, const QString &language)
{
    const quint64 generation = ++m_generation;

    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
    emit selectedCountChanged();

    setError({});
    setLoading(true);

    QPointer<SubtitleFinder> self(this);
    QThreadPool::globalInstance()->start([self, generation, query, language] {
        QVector<Entry> entries;
        QString error;
        runSearch(query, language, entries, error);

        // deliver on the GUI thread; `self` is only touched there
        QMetaObject::invokeMethod(qApp, [self, generation, entries, error] {
            if (self)
                self->onSearchDone(generation, entries, error);
        }, Qt::QueuedConnection);
    });
}

void SubtitleFinder::onSearchDone(quint64 generation, const QVector<Entry> &entries, const QString &error)
{
    if (generation != m_generation)
        return; // outdated search

    beginResetModel();
    m_entries = entries;
    endResetModel();
    emit countChanged();
    emit selectedCountChanged();

    setError(error);
    setLoading(false);
}

void SubtitleFinder::downloadSelected(const QUrl &mediaUrl)
{
    struct Job { QString url; QString fileName; QString language; QString id; };
    QVector<Job> jobs;
    for (const Entry &entry : m_entries)
        if (entry.selected)
            jobs.append({entry.url, entry.fileName, entry.language, entry.id});

    if (jobs.isEmpty())
    {
        emit downloadFailed(tr("Select at least one subtitle"));
        return;
    }

    setError({});
    setLoading(true);

    const QString mediaFolder = localFolderOf(mediaUrl);
    const QString mediaBaseName = mediaUrl.isLocalFile()
                                      ? QFileInfo(mediaUrl.toLocalFile()).completeBaseName()
                                      : QString();
    const bool saveBesideMedia = !mediaFolder.isEmpty() && !mediaBaseName.isEmpty()
                                 && isWritableFolder(mediaFolder);

    QPointer<SubtitleFinder> self(this);
    QThreadPool::globalInstance()->start([self, jobs, mediaFolder, mediaBaseName, saveBesideMedia] {
        QStringList files;
        QString error;

        for (const Job &job : jobs)
        {
            QString jobError;
            const QByteArray data = fetchBytes(job.url, MAX_SUBTITLE_SIZE, &jobError);
            if (data.isEmpty())
            {
                error = jobError;
                continue;
            }

            QString path;
            if (saveBesideMedia)
            {
                // "<video>.<lang>.srt": auto-detected by VLC next time the video is played
                const QString lang = job.language.isEmpty() ? QStringLiteral("sub") : job.language;
                path = uniquePath(mediaFolder, mediaBaseName + QLatin1Char('.') + lang,
                                  QStringLiteral("srt"));
            }
            else
            {
                QString name = sanitizeFileName(job.fileName);
                if (!name.endsWith(QLatin1String(".srt"), Qt::CaseInsensitive))
                    name += QStringLiteral(".srt");
                const QString baseName = QFileInfo(name).completeBaseName();
                path = uniquePath(cacheFolder(), job.id + QLatin1Char('_') + baseName,
                                  QStringLiteral("srt"));
            }

            QFile out(path);
            if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size())
            {
                error = QObject::tr("Unable to save the subtitle file");
                continue;
            }
            out.close();

            files.append(QUrl::fromLocalFile(path).toString());
        }

        if (files.isEmpty() && error.isEmpty())
            error = QObject::tr("Download failed");

        QMetaObject::invokeMethod(qApp, [self, files, error] {
            if (self)
                self->onDownloadDone(files, error);
        }, Qt::QueuedConnection);
    });
}

void SubtitleFinder::onDownloadDone(const QStringList &fileUrls, const QString &error)
{
    setLoading(false);

    if (fileUrls.isEmpty())
    {
        setError(error);
        emit downloadFailed(error);
        return;
    }

    emit downloaded(fileUrls);
}

//// local helpers

QString SubtitleFinder::suggestedQuery(const QString &mediaName) const
{
    QString name = mediaName.trimmed();

    // drop the extension
    static const QRegularExpression extRe(QStringLiteral("\\.[A-Za-z0-9]{2,4}$"));
    name.remove(extRe);
    name = cleanSeparators(name);

    static const QRegularExpression seRe(QStringLiteral("^(.*?\\b[sS]\\d{1,2}\\s*[eE]\\d{1,3})\\b"));
    static const QRegularExpression xRe(QStringLiteral("^(.*?\\b\\d{1,2}x\\d{1,3})\\b"));
    static const QRegularExpression yearRe(QStringLiteral("^(.*?\\b(?:19|20)\\d{2})\\b"));
    static const QRegularExpression junkRe(
        QStringLiteral("^(.*?)\\s*\\b(480p|576p|720p|1080p|2160p|4k|web|webrip|web-dl|bluray|brrip|bdrip|hdtv|dvdrip|x264|x265|h264|h265|hevc)\\b"),
        QRegularExpression::CaseInsensitiveOption);

    for (const QRegularExpression *re : {&seRe, &xRe, &yearRe, &junkRe})
    {
        const QRegularExpressionMatch match = re->match(name);
        if (match.hasMatch() && !match.captured(1).trimmed().isEmpty())
            return match.captured(1).trimmed();
    }

    return name;
}

QString SubtitleFinder::folderOf(const QUrl &mediaUrl) const
{
    return localFolderOf(mediaUrl);
}

QString SubtitleFinder::prettyPath(const QString &folder) const
{
    QStringList parts = QDir::fromNativeSeparators(folder).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    // drop a leading drive letter ("C:")
    if (!parts.isEmpty() && parts.first().endsWith(QLatin1Char(':')))
        parts.removeFirst();

    constexpr int maxParts = 4;
    while (parts.size() > maxParts)
        parts.removeFirst();

    return parts.join(QStringLiteral(" / "));
}

QString SubtitleFinder::chooseFolder(const QString &startFolder) const
{
    return QFileDialog::getExistingDirectory(nullptr, tr("Choose a folder"), startFolder);
}

QVariantList SubtitleFinder::subtitleFilesIn(const QString &folder) const
{
    QVariantList result;
    if (folder.isEmpty())
        return result;

    const QFileInfoList files = QDir(folder).entryInfoList(SUBTITLE_FILTERS, QDir::Files,
                                                            QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : files)
    {
        QVariantMap item;
        item.insert(QStringLiteral("name"), info.fileName());
        item.insert(QStringLiteral("path"), info.absoluteFilePath());
        result.append(item);
    }
    return result;
}

QVariantList SubtitleFinder::matchFolder(const QString &folder) const
{
    QVariantList result;
    if (folder.isEmpty())
        return result;

    const QDir dir(folder);
    const QFileInfoList videos = dir.entryInfoList(VIDEO_FILTERS, QDir::Files, QDir::Name | QDir::IgnoreCase);
    const QFileInfoList subtitles = dir.entryInfoList(SUBTITLE_FILTERS, QDir::Files, QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &video : videos)
    {
        const QString key = episodeKey(video.fileName());
        const QString videoBase = video.completeBaseName();

        QFileInfo best;
        bool found = false;
        int bestScore = 0;

        for (const QFileInfo &sub : subtitles)
        {
            bool matches = false;
            if (!key.isEmpty())
                matches = (episodeKey(sub.fileName()) == key);
            else
                matches = sub.completeBaseName().startsWith(videoBase, Qt::CaseInsensitive)
                          || (videos.size() == 1 && subtitles.size() == 1);

            if (!matches)
                continue;

            const int score = subtitlePreference(sub.fileName());
            if (!found || score < bestScore)
            {
                best = sub;
                bestScore = score;
                found = true;
            }
        }

        QVariantMap item;
        item.insert(QStringLiteral("video"), video.fileName());
        item.insert(QStringLiteral("videoPath"), video.absoluteFilePath());
        item.insert(QStringLiteral("subtitle"), found ? best.fileName() : QString());
        item.insert(QStringLiteral("subtitlePath"), found ? best.absoluteFilePath() : QString());
        result.append(item);
    }

    return result;
}

QUrl SubtitleFinder::applyMatches(const QVariantList &matches, const QUrl &currentMedia) const
{
    QUrl forCurrent;
    const QFileInfo current = currentMedia.isLocalFile() ? QFileInfo(currentMedia.toLocalFile()) : QFileInfo();

    for (const QVariant &value : matches)
    {
        const QVariantMap item = value.toMap();
        const QString videoPath = item.value(QStringLiteral("videoPath")).toString();
        const QString subtitlePath = item.value(QStringLiteral("subtitlePath")).toString();
        if (videoPath.isEmpty() || subtitlePath.isEmpty())
            continue;

        const QFileInfo video(videoPath);
        const QFileInfo sub(subtitlePath);
        const bool isCurrent = !current.filePath().isEmpty() && (video == current);

        const QString dest = video.absolutePath() + QLatin1Char('/') + video.completeBaseName()
                             + QLatin1Char('.') + sub.suffix();

        QString result = subtitlePath;

        if (QFileInfo(dest) == sub)
        {
            // already named after the video, nothing to do
        }
        else if (isWritableFolder(video.absolutePath()))
        {
            // never overwrite what the user already has
            const QString target = QFileInfo::exists(dest)
                                       ? uniquePath(video.absolutePath(), video.completeBaseName(), sub.suffix())
                                       : dest;
            if (QFile::copy(subtitlePath, target))
                result = target;
        }

        if (isCurrent)
            forCurrent = QUrl::fromLocalFile(result);
    }

    return forCurrent;
}

QUrl SubtitleFinder::prepareLocalSubtitle(const QString &subtitlePath, const QUrl &mediaUrl,
                                          const QString &language) const
{
    const QFileInfo sub(subtitlePath);
    const QString mediaFolder = localFolderOf(mediaUrl);

    // already next to the video (or the video is not a local file): load it as is
    if (mediaFolder.isEmpty() || !isWritableFolder(mediaFolder)
        || QFileInfo(sub.absolutePath()) == QFileInfo(mediaFolder))
        return QUrl::fromLocalFile(subtitlePath);

    const QString base = QFileInfo(mediaUrl.toLocalFile()).completeBaseName();
    const QString stem = language.isEmpty() ? base : (base + QLatin1Char('.') + language);
    const QString target = uniquePath(mediaFolder, stem, sub.suffix());

    if (QFile::copy(subtitlePath, target))
        return QUrl::fromLocalFile(target);

    return QUrl::fromLocalFile(subtitlePath);
}
