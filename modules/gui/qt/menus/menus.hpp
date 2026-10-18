/*****************************************************************************
 * menus.hpp : Menus handling
 ****************************************************************************
 * Copyright (C) 2006 the VideoLAN team
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef QVLC_MENUS_H_
#define QVLC_MENUS_H_

#include "qt.hpp"

#include "custom_menus.hpp"
#include "widgets/native/qvlcframe.hpp"

#include <QObject>

class VLCMenu : public QMenu
{
    Q_OBJECT

public:
    // Figma menu: #191919 rounded card, #8F8F8F text, #222222 hover
    static constexpr const char* figmaStyleSheet = R"QSS(
QMenu { background: #191919; border: none; border-radius: 10px; padding: 8px 0; color: #8F8F8F; }
QMenu::item { background: transparent; border-radius: 6px; margin: 0 8px; padding: 10px 28px 10px 36px; }
QMenu::item:selected { background: #222222; color: #B0B0B0; }
QMenu::item:disabled { color: #404040; }
QMenu::icon { padding-left: 10px; }
QMenu::indicator { left: 10px; width: 16px; height: 16px; }
QMenu::separator { height: 1px; background: #222222; margin: 6px 16px; }
QMenu::right-arrow { image: url(:/figma/chevron_right.svg); width: 16px; height: 16px; margin-right: 10px; }
QMenu::indicator:checked { image: url(:/figma/m_check.svg); }
QMenu::indicator:unchecked { image: none; }
)QSS";

    void applyFigmaStyle()
    {
        setWindowFlag(Qt::FramelessWindowHint, true);
        setWindowFlag(Qt::NoDropShadowWindowHint, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setStyleSheet(QString::fromUtf8(figmaStyleSheet));
    }

    explicit VLCMenu(qt_intf_t* p_intf) : QMenu()
    {
        assert(p_intf);
        applyFigmaStyle();
        if (isWindow())
        {
            QVLCDialog::setWindowTransientParent(this, nullptr, p_intf);
        }
    }

    explicit VLCMenu(const QString& title, qt_intf_t* p_intf) : VLCMenu(p_intf)
    {
        setTitle(title);
    }

    explicit VLCMenu(const QString &title, QWidget *parent) : QMenu(title, parent)
    {
        assert(parent); // use VLCMenu(qt_intf_t* p_intf) if parent is null pointer
        applyFigmaStyle();
    }
};

class VLCMenuBar : public QObject
{
    Q_OBJECT
    friend class MenuFunc;

public:
    VLCMenuBar(QObject* parent = nullptr);

    /* Popups Menus */
    static QMenu* PopupMenu( qt_intf_t *, bool );
    static QMenu* AudioPopupMenu( qt_intf_t *, bool );
    static QMenu* VideoPopupMenu( qt_intf_t *, bool );
    static QMenu* MiscPopupMenu( qt_intf_t *, bool );


    /* destructor for parentless Menus (kept in static variables) */
    static void freeRendererMenu(){ delete rendererMenu; rendererMenu = NULL; }

    /* All main Menus */
    static void FileMenu( qt_intf_t *, QMenu * );

    static void ToolsMenu( qt_intf_t *, QMenu * );

    static void ViewMenu(qt_intf_t *, QMenu *);

    static void InterfacesMenu( qt_intf_t *p_intf, QMenu * );
    static void ExtensionsMenu( qt_intf_t *p_intf, QMenu * );

    static void NavigMenu( qt_intf_t *, QMenu * );

    static void RebuildNavigMenu(qt_intf_t *, QMenu *);

    static void VideoMenu( qt_intf_t *, QMenu * );

    static void SubtitleMenu( qt_intf_t *, QMenu *current, bool b_popup = false );

    static void AudioMenu( qt_intf_t *, QMenu * );

    static void HelpMenu( qt_intf_t *, QMenu *menu );

    /* Popups Menus */
    static void PopupMenuStaticEntries( QMenu *menu );
    static void PopupMenuPlaylistEntries( QMenu *menu, qt_intf_t *p_intf );
    static void PopupMenuPlaylistControlEntries( QMenu *menu, qt_intf_t *p_intf );
    static void PopupMenuControlEntries( QMenu *menu, qt_intf_t *p_intf, bool b = true );

    static void VolumeEntries( qt_intf_t *p_intf, QMenu *current );

    static RendererMenu *rendererMenu;
};

#endif
