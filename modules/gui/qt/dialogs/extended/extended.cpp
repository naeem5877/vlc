/*****************************************************************************
 * extended.cpp : Extended controls - Undocked
 ****************************************************************************
 * Copyright (C) 2006-2008 the VideoLAN team
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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cassert>

#include "extended.hpp"

#include "maininterface/compositor.hpp" /* Needed for external MI size */
#include "player/player_controller.hpp"

#include <QTabWidget>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QWindow>
#include <vlc_configuration.h>
#include <vlc_modules.h>

/* Figma "Adjustments and Effects" look: #191919 window, #1B1B1B cards,
 * #090909 fields/grooves, brown slider fill + orange handle. */
static const char * const kFigmaExtendedStyleSheet = R"QSS(
QDialog, QTabWidget > QWidget { background: #191919; }
QWidget { color: #8F8F8F; }
QLabel { background: transparent; color: #6B6B6B; }
QLabel:disabled { color: #404040; }

QTabWidget::pane { border: none; border-radius: 6px; background: #1B1B1B; top: 6px; }
QTabBar { background: transparent; }
QTabBar::tab { background: #090909; color: #6B6B6B; border: none; border-radius: 6px;
               padding: 7px 14px; margin: 0 4px 0 0; }
QTabBar::tab:hover { background: #222222; color: #8F8F8F; }
QTabBar::tab:selected { background: #1B1B1B; color: #D37918; }

QGroupBox { background: #1B1B1B; border: none; border-radius: 6px; margin-top: 22px; padding: 14px 12px 10px 12px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 4px; color: #6B6B6B; }

QSlider::groove:horizontal { height: 16px; background: #090909; border-radius: 6px; }
QSlider::sub-page:horizontal { background: #371F06; border-radius: 4px; margin: 2px; }
QSlider::add-page:horizontal { background: transparent; }
QSlider::handle:horizontal { background: #AB6314; width: 8px; margin: -4px 0; border-radius: 4px; }
QSlider::groove:vertical { width: 16px; background: #090909; border-radius: 6px; }
QSlider::sub-page:vertical { background: #371F06; border-radius: 4px; margin: 2px; }
QSlider::add-page:vertical { background: transparent; }
QSlider::handle:vertical { background: #AB6314; height: 8px; margin: 0 -4px; border-radius: 4px; }
QSlider::handle:horizontal:disabled, QSlider::handle:vertical:disabled { background: #404040; }
QSlider::sub-page:disabled { background: #222222; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #090909; color: #8F8F8F; border: 1px solid #191919;
               border-radius: 5px; padding: 5px 8px; min-height: 22px; selection-background-color: #AB6314; selection-color: #090909; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border: 1px solid #AB6314; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: #404040; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: none; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: url(:/figma/chevron_down.svg); width: 16px; height: 16px; }
QComboBox QAbstractItemView { background: #191919; color: #8F8F8F; border: 1px solid #090909; outline: 0;
               selection-background-color: #222222; selection-color: #D37918; }

QCheckBox, QGroupBox::indicator { spacing: 10px; }
QCheckBox::indicator, QGroupBox::indicator { width: 68px; height: 32px; }
QCheckBox::indicator:unchecked, QGroupBox::indicator:unchecked { image: url(:/figma/toggle_off.svg); }
QCheckBox::indicator:checked, QGroupBox::indicator:checked { image: url(:/figma/toggle_on.svg); }

QPushButton, QToolButton { background: #090909; color: #8F8F8F; border: none; border-radius: 6px; padding: 7px 16px; min-height: 18px; }
QPushButton:hover, QToolButton:hover { background: #222222; }
QPushButton:pressed, QToolButton:pressed { background: #404040; }
QPushButton:default { color: #D37918; }
QPushButton:disabled, QToolButton:disabled { color: #404040; }

QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: #404040; border-radius: 4px; min-height: 24px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }
QScrollBar::handle:horizontal { background: #404040; border-radius: 4px; min-width: 24px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QToolTip { background: #191919; color: #8F8F8F; border: 1px solid #222222; padding: 4px; }
)QSS";

ExtendedDialog::ExtendedDialog( qt_intf_t *_p_intf )
               : QVLCDialog( nullptr, _p_intf )
{
#ifdef __APPLE__
    setWindowFlags( Qt::Drawer );
#else
    setWindowFlags( Qt::Tool );
#endif

    setWindowOpacity( var_InheritFloat( p_intf, "qt-opacity" ) );
    setWindowTitle( qtr( "Adjustments and Effects" ) );
    setWindowRole( "vlc-extended" );
    setStyleSheet( QString::fromUtf8( kFigmaExtendedStyleSheet ) );

    QVBoxLayout *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 2, 0, 1 );
    layout->setSpacing( 3 );

    mainTabW = new QTabWidget( this );

    /* AUDIO effects */
    QWidget *audioWidget = new QWidget;
    QHBoxLayout *audioLayout = new QHBoxLayout( audioWidget );
    QTabWidget *audioTab = new QTabWidget( audioWidget );

    equal = new Equalizer( p_intf, audioTab );
    connect( equal, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );
    audioTab->addTab( equal, qtr( "Equalizer" ) );

    Compressor *compres = new Compressor( p_intf, audioTab );
    connect( compres, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );
    audioTab->addTab( compres, qtr( "Compressor" ) );

    Expander *expand = new Expander( p_intf, audioTab );
    connect( expand, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );
    audioTab->addTab( expand, qtr( "Expander" ) );

    Limiter *limit = new Limiter( p_intf, audioTab );
    connect( limit, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );
    audioTab->addTab( limit, qtr( "Limiter" ) );

    Spatializer *spatial = new Spatializer( p_intf, audioTab );
    connect( spatial, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );
    audioTab->addTab( spatial, qtr( "Spatializer" ) );

    StereoWidener *stereowiden = new StereoWidener( p_intf, audioTab );
    connect( stereowiden, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );
    audioTab->addTab( stereowiden, qtr( "Stereo Widener" ) );

    QWidget *advancedTab = new QWidget;
    QGridLayout *advancedTabLayout = new QGridLayout;

    PitchShifter *pitchshifter = new PitchShifter( p_intf, audioTab );
    connect( pitchshifter, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );

    StereoPanner *stereopanner = new StereoPanner( p_intf, audioTab );
    connect( stereopanner, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );

    advancedTabLayout->setColumnStretch( 2, 10 );
    advancedTabLayout->addWidget( pitchshifter );
    advancedTabLayout->addWidget( stereopanner );

    advancedTab->setLayout( advancedTabLayout );
    audioTab->addTab( advancedTab, qtr( "Advanced" ) );

    audioLayout->addWidget( audioTab );

    mainTabW->insertTab( AUDIO_TAB, audioWidget, qtr( "Audio Effects" ) );

    /* Video Effects */
    QWidget *videoWidget = new QWidget;
    QHBoxLayout *videoLayout = new QHBoxLayout( videoWidget );
    QTabWidget *videoTab = new QTabWidget( videoWidget );

    videoEffect = new ExtVideo( p_intf, videoTab );
    connect( videoEffect, &ExtVideo::configChanged, this, &ExtendedDialog::putVideoConfig );
    videoLayout->addWidget( videoTab );
    videoTab->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Maximum );

    mainTabW->insertTab( VIDEO_TAB, videoWidget, qtr( "Video Effects" ) );

    syncW = new SyncControls( p_intf, mainTabW );
    mainTabW->insertTab( SYNCHRO_TAB, syncW, qtr( "Synchronization" ) );

    if( module_exists( "v4l2" ) )
    {
        ExtV4l2 *v4l2 = new ExtV4l2( p_intf, mainTabW );
        mainTabW->insertTab( V4L2_TAB, v4l2, qtr( "v4l2 controls" ) );
    }

    layout->addWidget( mainTabW );
    connect( mainTabW, &QTabWidget::currentChanged, this, &ExtendedDialog::currentTabChanged );

    /* Bottom buttons */
    QDialogButtonBox *buttonBox = new QDialogButtonBox( Qt::Horizontal, this );

    m_applyButton = new QPushButton( qtr("&Save"), this );
    m_applyButton->setEnabled( false );
    buttonBox->addButton( m_applyButton, QDialogButtonBox::ApplyRole );

    buttonBox->addButton(
        new QPushButton( qtr("&Close"), this ), QDialogButtonBox::RejectRole );
    layout->addWidget( buttonBox );

    connect( buttonBox, &QDialogButtonBox::rejected, this, &ExtendedDialog::close );
    connect( m_applyButton, &QPushButton::clicked, this, &ExtendedDialog::saveConfig );

    /* Restore geometry or move this dialog on the left pane of the MI */
    if( !restoreGeometry( getSettings()->value("EPanel/geometry").toByteArray() ) )
    {
        resize( QSize( 400, 280 ) );

        QWindow *window = p_intf->p_compositor->interfaceMainWindow();
        if( window && window->x() > 50 )
            move( ( window->x() - frameGeometry().width() - 10 ), window->y() );
        else
            move ( 450 , 0 );
    }

    connect( THEMIM, &PlayerController::playingStateChanged, this, &ExtendedDialog::changedItem );
}

ExtendedDialog::~ExtendedDialog()
{
    getSettings()->setValue("EPanel/geometry", saveGeometry());
}

void ExtendedDialog::showTab( int i )
{
    mainTabW->setCurrentIndex( i );
    show();
}

int ExtendedDialog::currentTab()
{
    return mainTabW->currentIndex();
}

void ExtendedDialog::changedItem( PlayerController::PlayingState i_status )
{
    if( i_status != PlayerController::PLAYING_STATE_STOPPED ) return;
    syncW->clean();
    videoEffect->clean();
}

void ExtendedDialog::currentTabChanged( int i )
{
    if( i == AUDIO_TAB || i == VIDEO_TAB )
    {
        m_applyButton->setVisible( true );
        m_applyButton->setEnabled( !m_hashConfigs[i].isEmpty() );
        m_applyButton->setFocusPolicy( Qt::StrongFocus );
    }
    else
    {
        m_applyButton->setVisible( false );
        m_applyButton->setFocusPolicy( Qt::NoFocus );
    }
}

void ExtendedDialog::putAudioConfig( const QString& name, const QVariant value )
{
    m_hashConfigs[AUDIO_TAB].insert( name, value );
    m_applyButton->setEnabled( true );
}

void ExtendedDialog::putVideoConfig( const QString& name, const QVariant value )
{
    m_hashConfigs[VIDEO_TAB].insert( name, value );
    m_applyButton->setEnabled( true );
}

void ExtendedDialog::saveConfig()
{
    assert( currentTab() == AUDIO_TAB || currentTab() == VIDEO_TAB );
    QHash<QString, QVariant> *hashConfig = &m_hashConfigs[currentTab()];

    for( QHash<QString, QVariant>::iterator i = hashConfig->begin();
         i != hashConfig->end(); ++i )
    {
        QVariant &value = i.value();
        switch( static_cast<QMetaType::Type>(value.typeId()) )
        {
            case QMetaType::QString:
                config_PutPsz( qtu(i.key()), qtu(value.toString()) );
                break;
            case QMetaType::Int:
            case QMetaType::Bool:
                config_PutInt( qtu(i.key()), value.toInt() ) ;
                break;
            case QMetaType::Double:
            case QMetaType::Float:
                config_PutFloat( qtu(i.key()), value.toFloat() ) ;
                break;
            default:
                vlc_assert_unreachable();
        }
    }
    config_SaveConfigFile( p_intf );
    hashConfig->clear();
    m_applyButton->setEnabled( false );
}
