/**
 * Copyright (C) 2015 Deepin Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 **/

#include "osd.h"
#include <X11/extensions/shape.h>
#include <QtX11Extras/QX11Info>
#include <QDesktopWidget>
#include <QApplication>
#include <QPainter>
#include <QPen>
#include <QPainterPath>
#include <QBrush>

#include <dplatformwindowhandle.h>

// basic commandlineoption
const OsdOption BrightnessUp = OsdOption("BrightnessUp");
const OsdOption BrightnessDown = OsdOption("BrightnessDown");
const OsdOption AudioMute = OsdOption("AudioMute");
const OsdOption AudioDown = OsdOption("AudioDown");
const OsdOption AudioUp = OsdOption("AudioUp");
const OsdOption SwitchMonitors = OsdOption("SwitchMonitors");
const OsdOption SwitchLayouts = OsdOption("SwitchLayout");
const OsdOption NumLockOn = OsdOption("NumLockOn", ":/icons/OSD_num_lock_on.png");
const OsdOption NumLockOff = OsdOption("NumLockOff", ":/icons/OSD_num_lock_off.png");
const OsdOption CapsLockOn = OsdOption("CapsLockOn", ":/icons/OSD_caps_lock_on.png");
const OsdOption CapsLockOff = OsdOption("CapsLockOff", ":/icons/OSD_caps_lock_off.png");
const OsdOption TouchpadOn = OsdOption("TouchpadOn", ":/icons/OSD_trackpad_on.png");
const OsdOption TouchpadOff = OsdOption("TouchpadOff", ":/icons/OSD_trackpad_off.png");
//const OsdOption SwitchWM3D = OsdOption("SwitchWM3D", "wm-effect-enabled", qApp->translate("Osd", "Enable window effects"));
//const OsdOption SwitchWM2D = OsdOption("SwitchWM2D", "wm-effect-disabled", qApp->translate("Osd", "Disable window effects"));
//const OsdOption SwitchWMError = OsdOption("SwitchWMError", "wm-effect-error", qApp->translate("Osd", "Failed to enable window effects"));
OsdOption SwitchWM3D = OsdOption("SwitchWM3D", ":/icons/OSD_wm_3d.png");
OsdOption SwitchWM2D = OsdOption("SwitchWM2D", ":/icons/OSD_wm_2d.png");
OsdOption SwitchWMError = OsdOption("SwitchWMError", ":/icons/OSD_wm_failed.png");

const OsdOption *optionList[] = {
    &BrightnessUp,
    &BrightnessDown,
    &AudioMute,
    &AudioDown,
    &AudioUp,
    &SwitchLayouts,
    &SwitchMonitors,
    &NumLockOn,
    &NumLockOff,
    &CapsLockOn,
    &CapsLockOff,
    &TouchpadOn,
    &TouchpadOff,
    &SwitchWM3D,
    &SwitchWM2D,
    &SwitchWMError,
};

Osd::Osd(QWidget *parent)
    : DBlurEffectWidget(parent)
{
    // init osd options
    SwitchWM3D.setText(qApp->translate("Osd", "Enable window effects"));
    SwitchWM2D.setText(qApp->translate("Osd", "Disable window effects"));
    SwitchWMError.setText(qApp->translate("Osd", "Failed to enable window effects"));

    DPlatformWindowHandle handle(this);

    handle.setBorderColor(QColor(0, 0, 0, 0.04 * 255));
    handle.setWindowRadius(10);
    handle.setShadowColor(Qt::transparent);

    initGlobalVars();
    initBasicOperation();
    initConnect();
    initBasicCommandLine();

    setBlendMode(DBlurEffectWidget::BehindWindowBlend);
    setBlurRectXRadius(10);
    setBlurRectYRadius(10);
    setMaskColor(QColor(245, 245, 245));
}

Osd::~Osd()
{

}

void Osd::delayAction()
{
    hide();

    // set the monitor mode when the app quits
    if (actionMode == SwitchMonitorEnum) {
        m_SwitchMonitor->setMonitorMode();
    }

    // set the keyboard layout when the app quits
    if (actionMode == SwitchLayoutEnum && m_SwitchLayout->layoutCount() >= 1) {
        m_SwitchLayout->setKeyboard();
    }
    actionMode =Unknow;
}

void Osd::initGlobalVars()
{
    m_SwitchNormal = new SwitchNormal(this);

    m_SwitchLayout = new SwitchLayout(this);

    m_SwitchMonitor = new SwitchMonitor(this);

    // m_Timer is used to record time , to quit the app properly
    m_Timer = new QTimer(this);

    connect(m_SwitchNormal, &SwitchNormal::muteChanged, this, [=]{
        actionMode = AudioProgressBar;
        update();
    });
}

void Osd::initBasicOperation()
{
    setWindowFlags(Qt::X11BypassWindowManagerHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    // set mouse penetration
    XShapeCombineRectangles(QX11Info::display(), winId(), ShapeInput, 0, 0, NULL, 0, ShapeSet, YXBanded);
}

void Osd::initConnect()
{
    connect(m_Timer, &QTimer::timeout, this, &Osd::delayAction);
}

void Osd::initBasicCommandLine()
{
    // command line parser
    m_Parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    m_Parser.addHelpOption();
    for (auto opt : optionList)  {
        m_Parser.addOption(*opt);
    }
}

void Osd::moveToCenter()
{
    // find out the screen that contains mouse.
    QDesktopWidget *desktop = QApplication::desktop();
    int primaryScreenKey = desktop->primaryScreen();
    for (int i = 0; i < desktop->screenCount(); i++) {
        QRect screen = desktop->screenGeometry(primaryScreenKey + i);
        if (screen.contains(QCursor::pos())) {
            m_MouseInScreen = screen;
            break;
        }
    }
    // move to corresponding screen
    this->move(m_MouseInScreen.x() + (m_MouseInScreen.width() - this->width()) / 2, m_MouseInScreen.y() + (m_MouseInScreen.height() - this->height()) / 2);
}

void Osd::processParser()
{
    m_Parser.process(*qApp);
}

void Osd::processParser(const QStringList &args)
{
    m_Parser.parse(args);
}

// when reaches deadline, we need to quit the app immediately
void Osd::setTimer()
{
    m_Timer->start(DEADLINE_TIME);
}

void Osd::tailInWork()
{
    moveToCenter();
    setTimer();
    this->update();
}

void Osd::dbusShowOSD()
{
    QDBusInterface iface("com.deepin.dde.osd", "/", "com.deepin.dde.osd", QDBusConnection::sessionBus());
    for (auto opt : optionList)  {
        if (m_Parser.isSet(*opt)) {
            iface.call("ShowOSD", opt->names().first());
        }
    }
}

void Osd::showOSD()
{
    if (m_Parser.isSet(BrightnessUp) || m_Parser.isSet(BrightnessDown)) {
        loadBasicNormal("Brightness");
        cancelNormalText();
    } else if (m_Parser.isSet(AudioUp) || m_Parser.isSet(AudioDown)) {
        loadBasicNormal("Audio");
        cancelNormalText();
    } else if (m_Parser.isSet(AudioMute)) {
        loadBasicNormal("AudioMute");
        cancelNormalText();
    } else if (m_Parser.isSet(SwitchLayouts)) {
        if (!m_SwitchLayout->isPanelVailed()) {
            return;
        }
        if (!getLayoutStatus()) {
            loadSwitchLayout();
        } else {
            lastAppMode() == SwitchLayoutEnum ? highlightNextLayout() : highlightCurrentLayout();
        }
    } else if (m_Parser.isSet(SwitchMonitors)) {
        if (!m_SwitchMonitor->isPanelVailed()) {
            return;
        }
        if (!getMonitorStatus()) {
            loadSwitchMonitors();
        } else {
            lastAppMode() == SwitchMonitorEnum ? highlightNextMonitor() : highlightCurrentMonitor();
        }
    } else if (m_Parser.isSet(NumLockOn)) {
        loadAdditionalNormal(NumLockOn.imageName());
        cancelNormalText();
    } else if (m_Parser.isSet(NumLockOff)) {
        loadAdditionalNormal(NumLockOff.imageName());
        cancelNormalText();
    } else if (m_Parser.isSet(CapsLockOn)) {
        loadAdditionalNormal(CapsLockOn.imageName());
        cancelNormalText();
    } else if (m_Parser.isSet(CapsLockOff)) {
        loadAdditionalNormal(CapsLockOff.imageName());
        cancelNormalText();
    } else if (m_Parser.isSet(TouchpadOn)) {
        loadAdditionalNormal(TouchpadOn.imageName());
        cancelNormalText();
    } else if (m_Parser.isSet(TouchpadOff)) {
        loadAdditionalNormal(TouchpadOff.imageName());
        cancelNormalText();
    } else if (m_Parser.isSet(SwitchWM2D)) {
        loadAdditionalNormal(SwitchWM2D.imageName());
        showNormalText(SwitchWM2D.text());
    } else if (m_Parser.isSet(SwitchWM3D)) {
        loadAdditionalNormal(SwitchWM3D.imageName());
        showNormalText(SwitchWM3D.text());
    } else if (m_Parser.isSet(SwitchWMError)) {
        loadAdditionalNormal(SwitchWMError.imageName());
        showNormalText(SwitchWMError.text());
    } else {
        qDebug() << "Unknow osd";
        return;
    }
    setTimer();
    moveToCenter();
    show();
    update();
}

void Osd::loadBasicNormal(QString whichImage)
{
    if (whichImage == "Brightness") {
        actionMode = BrightnessProgressBar;
    } else if (whichImage == "Audio") {
        actionMode = AudioProgressBar;
    } else {
        actionMode = NoProgressBar;
    }

    m_SwitchLayout->hideLayout();
    m_SwitchMonitor->hideMonitors();
    this->resize(BASE_SIZE, BASE_SIZE);

    m_SwitchNormal->loadBasicImage(whichImage);
}

int Osd::lastAppMode()
{
    return actionMode;
}

bool Osd::getLayoutStatus()
{
    return layoutHasBeenInited;
}

bool Osd::getMonitorStatus()
{
    return monitorHasBeenInited;
}

void Osd::cancelNormalText()
{
    m_SwitchNormal->cancelText();
}

void Osd::showNormalText(QString text)
{
    m_SwitchNormal->showText(text);
}

void Osd::loadAdditionalNormal(QString imageName)
{
    qDebug() << "loadAdditionalNormal" << imageName;

    actionMode = NoProgressBar;
    m_SwitchLayout->hideLayout();
    m_SwitchMonitor->hideMonitors();
    m_SwitchNormal->showNormal();
    this->resize(BASE_SIZE, BASE_SIZE);
    m_SwitchNormal->searchAddedImage(imageName);
}

void Osd::loadSwitchLayout()
{

    m_SwitchNormal->hideNormal();
    m_SwitchMonitor->hideMonitors();
    m_SwitchLayout->loadSwitchLayout();
    actionMode = SwitchLayoutEnum;
    layoutHasBeenInited = true;
}

void Osd::highlightNextLayout()
{
    m_SwitchLayout->highlightNextLayout();
}

void Osd::highlightCurrentLayout()
{
    actionMode = SwitchLayoutEnum;
    m_SwitchNormal->hideNormal();
    m_SwitchMonitor->hideMonitors();
    m_SwitchLayout->showLayout();
    m_SwitchLayout->resizeParent();
    m_SwitchLayout->reHiglightKeyboard();
}

void Osd::loadSwitchMonitors()
{
    m_SwitchNormal->hideNormal();
    m_SwitchLayout->hideLayout();
    m_SwitchMonitor->loadSwitchMonitors();
    actionMode = SwitchMonitorEnum;
    monitorHasBeenInited = true;
}

void Osd::highlightNextMonitor()
{
    m_SwitchMonitor->highlightNextMonitor();
}

void Osd::highlightCurrentMonitor()
{
    actionMode = SwitchMonitorEnum;
    m_SwitchNormal->hideNormal();
    m_SwitchLayout->hideLayout();
    m_SwitchMonitor->showMonitors();
    m_SwitchMonitor->resizeParent();
    m_SwitchMonitor->reHighlightMonitor();
}

void Osd::paintEvent(QPaintEvent *e)
{
    DBlurEffectWidget::paintEvent(e);

    // paint app's background
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (actionMode == AudioProgressBar || actionMode == BrightnessProgressBar) {
        // paint progressbar's background
        QBrush brush;
        brush.setStyle(Qt::SolidPattern);

        QRectF progressBarBackRect(30, 110, 80, 4);
        brush.setColor(QColor(0, 0, 0, 25));
        painter.setPen(Qt::NoPen);
        painter.setBrush(brush);
        painter.drawRoundedRect(progressBarBackRect, 2, 2);

        if (actionMode == AudioProgressBar) {
            // paint audio progressbar
            QRectF progressBarRect(30, 110, 80 * (m_SwitchNormal->getVolume() >= 1.0 ? 1.0 : m_SwitchNormal->getVolume()), 4);
            brush.setColor(Qt::black);
            painter.setBrush(brush);
            painter.drawRoundedRect(progressBarRect, 2, 2);
        } else if (actionMode == BrightnessProgressBar) {
            // paint brightness progressbar
            QRectF progressBarRect(30, 110, 80 * m_SwitchMonitor->getBrightness(), 4);
            brush.setColor(Qt::black);
            painter.setBrush(brush);
            painter.drawRoundedRect(progressBarRect, 2, 2);
        }
    }
}

