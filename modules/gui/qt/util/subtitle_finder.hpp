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
#ifndef SUBTITLE_FINDER_HPP
#define SUBTITLE_FINDER_HPP

#include <QAbstractListModel>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVector>

/**
 * @brief Online subtitle search + a few helpers for local subtitle files.
 *
 * Online search needs no API key: it uses the public Stremio addon endpoints
 *  - Cinemeta (title -> IMDb id)         https://v3-cinemeta.strem.io
 *  - OpenSubtitles v3 addon (id -> subs) https://opensubtitles-v3.strem.io
 *
 * QtNetwork is not available in VLC's bundled Qt, so every request goes
 * through VLC's own access layer (VLCIODevice) on a worker thread.
 *
 * As a list model it exposes the search results with a checkable
 * "selected" role.
 */
class SubtitleFinder : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectedCountChanged FINAL)

public:
    enum Role
    {
        FileNameRole = Qt::UserRole + 1,
        LanguageRole,
        ReleaseRole,
        SelectedRole
    };
    Q_ENUM(Role)

    // result entry, public so the worker helpers in the .cpp can build them
    struct Entry
    {
        QString id;
        QString url;
        QString fileName;
        QString language;
        QString release;
        bool selected = false;
    };

    explicit SubtitleFinder(QObject *parent = nullptr);
    ~SubtitleFinder() override;

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_entries.size(); }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    int selectedCount() const;

    // ---- online
    /**
     * @param query "Title", "Title S01E02", "Title 1999" or an IMDb id (tt1234567)
     * @param language ISO 639-2 code (eng, spa, ...) or empty for all
     */
    Q_INVOKABLE void search(const QString &query, const QString &language);
    Q_INVOKABLE void clear();

    /**
     * Downloads the selected results. Emits downloaded() with the local files
     * (saved next to the video when `mediaUrl` is a writable local file,
     * otherwise in the cache folder).
     */
    Q_INVOKABLE void downloadSelected(const QUrl &mediaUrl);

    // ---- local helpers
    /** "The_Wand_S01E01.mp4" -> "The Wand S01E01" */
    Q_INVOKABLE QString suggestedQuery(const QString &mediaName) const;
    /** folder of a file:// url, as a local path ("" if not local) */
    Q_INVOKABLE QString folderOf(const QUrl &mediaUrl) const;
    /** "C:/Videos/Shows/The Wand/S01" -> "Videos / Shows / The Wand / S01" */
    Q_INVOKABLE QString prettyPath(const QString &folder) const;
    /** native folder chooser, returns "" if cancelled */
    Q_INVOKABLE QString chooseFolder(const QString &startFolder) const;
    /** subtitle files in a folder: [{ name, path }] */
    Q_INVOKABLE QVariantList subtitleFilesIn(const QString &folder) const;
    /** video <-> subtitle pairing by S01E01 pattern: [{ video, videoPath, subtitle, subtitlePath }] */
    Q_INVOKABLE QVariantList matchFolder(const QString &folder) const;
    /**
     * copy the subtitle of each pair next to its video (never overwriting).
     * @return the subtitle to load for `currentMedia` if it is one of the videos, else an empty url
     */
    Q_INVOKABLE QUrl applyMatches(const QVariantList &matches, const QUrl &currentMedia) const;
    /** copy a local subtitle next to `mediaUrl` (if local), returns the url to load */
    Q_INVOKABLE QUrl prepareLocalSubtitle(const QString &subtitlePath, const QUrl &mediaUrl,
                                          const QString &language = QString()) const;

signals:
    void countChanged();
    void loadingChanged();
    void errorChanged();
    void selectedCountChanged();
    void downloaded(const QStringList &fileUrls);
    void downloadFailed(const QString &message);

private:
    void setLoading(bool loading);
    void setError(const QString &error);

    // called on the GUI thread by the workers
    void onSearchDone(quint64 generation, const QVector<Entry> &entries, const QString &error);
    void onDownloadDone(const QStringList &fileUrls, const QString &error);

    QVector<Entry> m_entries;
    bool m_loading = false;
    QString m_error;
    quint64 m_generation = 0;
};

#endif // SUBTITLE_FINDER_HPP
