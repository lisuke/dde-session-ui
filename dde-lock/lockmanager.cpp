/*
 * Copyright (C) 2015 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     sbw <sbw@sbw.so>
 *             kirigaya <kirigaya@mkacg.com>
 *             Hualet <mr.asianwang@gmail.com>
 *
 * Maintainer: sbw <sbw@sbw.so>
 *             kirigaya <kirigaya@mkacg.com>
 *             Hualet <mr.asianwang@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "userwidget.h"
#include "passwdedit.h"
#include "util_updateui.h"
#include "dbus/dbusinputdevices.h"

#include <QApplication>
#include <QDesktopWidget>
#include <QKeyEvent>
#include <QDebug>
#include <QMap>
#include <QProcess>
#include <QDBusConnection>
#include <libintl.h>
#include <grp.h>
#include <pwd.h>

#include "lockmanager.h"
#include "lockframe.h"
#include "dbus/dbuslockfront.h"
#include "dbus/dbuscontrolcenter.h"

LockManager::LockManager(QWidget *parent)
    : QFrame(parent)
    , m_activatedUser(UserWidget::currentContextUser())
    , m_userState(Passwd)
    , m_currentUser(nullptr)
    , m_blurImageInter(new ImageBlur("com.deepin.daemon.Accounts",
                                     "/com/deepin/daemon/ImageBlur",
                                     QDBusConnection::systemBus(), this))
{
    initUI();
    initConnect();
    initBackend();
    updateUI();

    m_layoutState = UnlockState;

    QTimer::singleShot(1, this, [=] {
        onCurrentUserChanged(m_userWidget->currentUser());
    });

    m_currentUser = m_userWidget->currentUser();
}

void LockManager::initConnect()
{
    connect(m_lockInter, &DBusLockService::Event, this, &LockManager::lockServiceEvent);
    connect(m_passwordEdit, &PassWdEdit::submit, this, &LockManager::unlock);
    connect(m_userWidget, &UserWidget::currentUserChanged, this, &LockManager::onCurrentUserChanged);
    connect(m_userWidget, &UserWidget::switchToLogindUser, this, &LockManager::switchToUser);
    connect(m_passwordEdit, &PassWdEdit::keybdLayoutButtonClicked, this, &LockManager::keybdLayoutWidgetPosit);
    connect(m_controlWidget, &ControlWidget::requestShutdown, this, &LockManager::shutdownMode);
    connect(m_controlWidget, &ControlWidget::requestSwitchUser, this, &LockManager::chooseUserMode);
    connect(m_controlWidget, &ControlWidget::requestSwitchUser, m_userWidget, &UserWidget::expandWidget, Qt::QueuedConnection);
    connect(m_blurImageInter, &ImageBlur::BlurDone, this, &LockManager::onBlurWallpaperFinished);

    connect(m_requireShutdownWidget, &ShutdownWidget::shutDownWidgetAction, [this](const ShutdownWidget::Actions action) {
        switch (action) {
        case ShutdownWidget::RequireRestart:    m_action = Restart;         break;
        case ShutdownWidget::RequireShutdown:   m_action = Shutdown;        break;
        case ShutdownWidget::RequireSuspend:    m_action = Suspend;         break;
        }

        passwordMode();
    });

    connect(m_requireShutdownWidget, &ShutdownWidget::abortOperation, this, [=] {
        m_passwordEdit->setMessage("");
        m_action = Unlock;
        passwordMode();
    });

    connect(qApp, &QApplication::aboutToQuit, [this] {
        m_hotZoneInterface->EnableZoneDetected(true);
    });

    connect(m_unlockButton, &QPushButton::clicked, this, &LockManager::unlock);

    connect(m_userWidget, &UserWidget::otherUserLogin, this, [=] {
        QProcess *process = new QProcess;
        connect(process, static_cast<void (QProcess::*)(int)>(&QProcess::finished), process, &QProcess::deleteLater);
        process->start("dbus-send --print-reply --system --dest=org.freedesktop.DisplayManager "
                       "/org/freedesktop/DisplayManager/Seat0 org.freedesktop.DisplayManager.Seat.SwitchToGreeter");
    });

    connect(m_userWidget, &UserWidget::currentUserBackgroundChanged, this, [=] {
//        updateBackground(m_userWidget->currentUser());
    });

    connect(m_userWidget, &UserWidget::userCountChanged, this, [=] (int count) {
        m_controlWidget->setUserSwitchEnable(count > 1);
    });
}

void LockManager::keybdLayoutWidgetPosit()
{
    const QPoint p(m_keybdArrowWidget->rect().x() + 45, m_keybdArrowWidget->rect().y() + 21);

    m_keybdArrowWidget->setCornerPoint(p);

    const int x = m_passwordEdit->x() + m_keybdArrowWidget->width() / 2 - 22;

    m_keybdArrowWidget->show(x, m_passwordEdit->y() + m_passwordEdit->height() - 15);

    m_keybdLayoutWidget->show();
}

void LockManager::initUI()
{
    setFocusPolicy(Qt::NoFocus);
    resize(qApp->primaryScreen()->geometry().size());

    m_timeWidget = new TimeWidget(this);
    m_timeWidget->setFixedSize(400, 300);

    m_userWidget = new UserWidget(this);
    m_userWidget->setFixedWidth(width());
    m_userWidget->move(0, (height() - m_userWidget->height()) / 2 - 95);

    m_passwordEdit = new PassWdEdit(this);
    m_passwordEdit->setEnterBtnStyle(":/img/action_icons/unlock_normal.svg",
                                     ":/img/action_icons/unlock_hover.svg",
                                     ":/img/action_icons/unlock_press.svg");
    m_passwordEdit->setFocusPolicy(Qt::StrongFocus);
    m_passwordEdit->show();

    m_unlockButton = new QPushButton(this);
    m_unlockButton->setText(tr("Login"));
    m_unlockButton->setFixedSize(160, 36);
    m_unlockButton->hide();
    m_unlockButton->setFocusPolicy(Qt::StrongFocus);
    m_unlockButton->setDefault(true);

    m_requireShutdownWidget = new ShutdownWidget(this);
    m_requireShutdownWidget->hide();
    m_requireShutdownWidget->setFixedWidth(width());
    m_requireShutdownWidget->setFixedHeight(300);

    m_controlWidget = new ControlWidget(this);
    m_controlWidget->setUserSwitchEnable(m_userWidget->availableUserCount() > 1);
    m_controlWidget->setMPRISEnable(true);

    QHBoxLayout *passwdLayout = new QHBoxLayout;
    passwdLayout->setMargin(0);
    passwdLayout->setSpacing(0);
    passwdLayout->addStretch();
    passwdLayout->addWidget(m_passwordEdit);
    passwdLayout->addWidget(m_unlockButton);
    passwdLayout->addStretch();

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->setMargin(0);
    mainLayout->setSpacing(0);
    mainLayout->addStretch();
    mainLayout->addLayout(passwdLayout);
    mainLayout->addStretch();

    setLayout(mainLayout);
    updateWidgetsPosition();
    updateStyle(":/skin/lock.qss", this);

    m_lockInter = new DBusLockService(LOCKSERVICE_NAME, LOCKSERVICE_PATH,
                                      QDBusConnection::systemBus(), this);
}

void LockManager::updateWidgetsPosition()
{
    const int width = this->width();
    const int height = this->height();
    m_timeWidget->move(48, height - m_timeWidget->height() - 36); // left 48px and bottom 36px
    m_userWidget->setFixedWidth(width);
    m_userWidget->move(0, (height - m_userWidget->height()) / 2 - 95); // center and margin-top: -95px

    m_requireShutdownWidget->setFixedWidth(width);
    m_requireShutdownWidget->move(0, (height - m_requireShutdownWidget->height()) / 2 - 50);
    m_controlWidget->move(width - m_controlWidget->width(),
                          height - m_controlWidget->height() - 36); // margin right 0 margin bottom 36
}

void LockManager::chooseUserMode()
{
    m_layoutState = UsersState;

    m_passwordEdit->hide();
    m_unlockButton->hide();
    m_userWidget->show();
    m_requireShutdownWidget->hide();
    m_keybdLayoutWidget->hide();
    m_keybdArrowWidget->hide();
}

void LockManager::onUnlockFinished(const bool unlocked)
{
    if (!unlocked) {
        qDebug() << "Authorization failed!";

        m_passwordEdit->selectAll();
        m_passwordEdit->setAlert(true, tr("Wrong Password"));

        m_lockInter->AuthenticateUser(m_activatedUser);
        return;
    }

    // Auth success
    switch (m_action) {
    case Restart:       m_sessionManagerIter->RequestReboot();    break;
    case Shutdown:      m_sessionManagerIter->RequestShutdown();    break;
    case Suspend:       m_sessionManagerIter->RequestSuspend();     break;
    default: break;
    }

    m_userWidget->saveLastUser();

#ifdef LOCK_NO_QUIT
    m_passwordEdit->setMessage("");
    emit checkedHide();
#else
    qApp->exit();
#endif
}

void LockManager::showEvent(QShowEvent *event)
{
    QTimer::singleShot(100, this, &LockManager::disableZone);

    m_keybdLayoutWidget->hide();
    m_keybdArrowWidget->hide();

    // check user is nopassword group
    updatePasswordEditVisible(m_currentUser);

//    updateBackground(m_activatedUser);

    m_passwordEdit->setMessage("");

    const QStringList &kblayout = m_userWidget->currentUser()->kbLayoutList();
    m_passwordEdit->updateKeybdLayoutUI(kblayout);
    m_keybdLayoutWidget->updateButtonList(kblayout);
    m_keybdLayoutWidget->setDefault(m_userWidget->currentUser()->currentKBLayout());

    // hide dde-control-center
    DBusControlCenter *DCCInter = new DBusControlCenter;
    if (DCCInter->isValid()) {
        DCCInter->HideImmediately();
    }
    DCCInter->deleteLater();

    QFrame::showEvent(event);
}

void LockManager::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);

    QTimer::singleShot(0, this, &LockManager::updateWidgetsPosition);
}

void LockManager::mouseReleaseEvent(QMouseEvent *e)
{
    qDebug() << "ReleaseEvent";

    m_action = Unlock;

    if (m_layoutState != UnlockState) {
        m_layoutState = UnlockState;
        passwordMode();
        if (!m_keybdArrowWidget->isHidden()) {
            m_keybdArrowWidget->hide();
        }

        m_passwordEdit->setMessage("");
    }

    QFrame::mouseReleaseEvent(e);
}

void LockManager::unlock()
{
    m_isThumbAuth = false;

    m_keybdLayoutWidget->hide();
    m_keybdArrowWidget->hide();

    if (!m_requireShutdownWidget->isHidden()) {
        m_requireShutdownWidget->shutdownAction();
        return;
    }

    const QString &username = m_activatedUser;
    const QString &password = m_passwordEdit->getText();

    if (password.isEmpty() && m_userState == Passwd)
        return;

    if (m_authenticating)
        return;

    m_authenticating = true;

    m_lockInter->UnlockCheck(username, password);
}

void LockManager::lockServiceEvent(quint32 eventType, quint32 pid, const QString &username, const QString &message)
{
    if (username != m_activatedUser)
        return;

    qDebug() << eventType << pid << username << message;

    // Don't show password prompt from standard pam modules since
    // we'll provide our own prompt or just not.
    const QString msg = message == "Password: " ? "" : message;

    m_authenticating = false;

    if (msg == "Verification timed out") {
        m_isThumbAuth = true;
        m_passwordEdit->setMessage(tr("Fingerprint verification timed out, please enter your password manually"));
        return;
    }

    switch (eventType) {
    case DBusLockService::PromptQuestion:
        qDebug() << "prompt quesiton from pam: " << message;
        m_passwordEdit->setMessage(message);
        break;
    case DBusLockService::PromptSecret:
        qDebug() << "prompt secret from pam: " << message;
        if (m_isThumbAuth)
            return;

        if (!msg.isEmpty())
            m_passwordEdit->setMessage(msg);
        break;
    case DBusLockService::ErrorMsg:
        qWarning() << "error message from pam: " << message;
        if (msg == "Failed to match fingerprint") {
            m_passwordEdit->setAlert(true, tr("Failed to match fingerprint"));
            m_passwordEdit->setMessage("");
        }
        break;
    case DBusLockService::TextInfo:
        m_passwordEdit->setMessage(QString(dgettext("fprintd", message.toLatin1())));
        break;
    case DBusLockService::Failure:
        onUnlockFinished(false);
        break;
    case DBusLockService::Success:
        onUnlockFinished(true);
        break;
    default:
        break;
    }
}

bool LockManager::checkUserIsNoPWGrp(User *user)
{
    if (user->type() == User::ADDomain) {
        return false;
    }

    // Caution: 32 here is unreliable, and there may be more
    // than this number of groups that the user joins.

    int ngroups = 32;
    gid_t groups[32];
    struct passwd pw;
    struct group gr;

    /* Fetch passwd structure (contains first group ID for user) */

    pw = *getpwnam(user->name().toUtf8().data());

    /* Retrieve group list */

    if (getgrouplist(user->name().toUtf8().data(), pw.pw_gid, groups, &ngroups) == -1) {
        fprintf(stderr, "getgrouplist() returned -1; ngroups = %d\n",
                ngroups);
        return false;
    }

    /* Display list of retrieved groups, along with group names */

    for (int i = 0; i < ngroups; i++) {
        gr = *getgrgid(groups[i]);
        if (QString(gr.gr_name) == QString("nopasswdlogin")) {
            return true;
        }
    }

    return false;
}

void LockManager::updatePasswordEditVisible(User *user)
{
    if (checkUserIsNoPWGrp(user)) {
        m_passwordEdit->setVisible(false);
        m_unlockButton->show();
        m_userState = NoPasswd;
    } else {
        m_lockInter->AuthenticateUser(user->name());
        m_unlockButton->setVisible(false);
        m_passwordEdit->show();
        m_userState = Passwd;
    }
}

void LockManager::onCurrentUserChanged(User *user)
{
    m_requireShutdownWidget->hide();

    if (user->name() != m_userWidget->currentContextUser()) {
        switchToUser(user);
        return;
    }

    updatePasswordEditVisible(m_currentUser);

    const QString &wallpaper = m_blurImageInter->Get(user->greeterBackgroundPath());

    emit requestSetBackground(wallpaper.isEmpty() ? user->greeterBackgroundPath() : wallpaper);
}

void LockManager::switchToUser(User *user)
{
    m_requireShutdownWidget->hide();

    m_userWidget->restoreUser(m_currentUser);

    updatePasswordEditVisible(user);

    // TODO: FIXME
    saveUser(user->name());

    if (user->isLogin()) {
        QProcess::startDetached("dde-switchtogreeter", QStringList() << user->name());
    } else {
        QProcess::startDetached("dde-switchtogreeter");
    }
}

void LockManager::onBlurWallpaperFinished(const QString &source, const QString &blur, bool status)
{
    const QString &sourcePath = QUrl(source).isLocalFile() ? QUrl(source).toLocalFile() : source;

    if (status && m_userWidget->currentUser()->desktopBackgroundPath() == sourcePath)
        emit requestSetBackground(blur);
}

void LockManager::saveUser(const QString &username)
{
    QFile f("/tmp/lastuser");
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(username.toLocal8Bit());
        f.setPermissions(QFileDevice::Permissions(0x7777));
        f.close();
    }
}

void LockManager::initBackend()
{
    m_hotZoneInterface = new DBusHotzone("com.deepin.daemon.Zone", "/com/deepin/daemon/Zone",
                                         QDBusConnection::sessionBus(), this);
#ifndef LOCK_NO_QUIT
    m_hotZoneInterface->EnableZoneDetected(false);
#endif


    DBusInputDevices *dbusInputDevices = new DBusInputDevices(this);
    foreach(InputDevice device, dbusInputDevices->infos()) {
        if (device.deviceType == "keyboard") {
            m_keyboardLayoutInterface = new DBusKeyboard(this);
            break;
        }
    }

    m_sessionManagerIter = new DBusSessionManagerInterface("com.deepin.SessionManager", "/com/deepin/SessionManager",
            QDBusConnection::sessionBus(), this);
}

void LockManager::updateUI()
{
    m_keybdLayoutWidget = new KbLayoutWidget;

    m_keybdArrowWidget = new DArrowRectangle(DArrowRectangle::ArrowTop, this);

    m_keybdArrowWidget->setBackgroundColor(QColor::fromRgbF(1, 1, 1, 0.15));
    m_keybdArrowWidget->setBorderColor(Qt::transparent);
    m_keybdArrowWidget->setArrowX(17);
    m_keybdArrowWidget->setArrowWidth(12);
    m_keybdArrowWidget->setArrowHeight(6);
    m_keybdArrowWidget->setMargin(1);

    m_keybdArrowWidget->setContent(m_keybdLayoutWidget);
    m_keybdLayoutWidget->setParent(m_keybdArrowWidget);
    m_keybdLayoutWidget->show();
    m_keybdArrowWidget->move(m_passwordEdit->x() + 123, m_passwordEdit->y() + m_passwordEdit->height() - 15);
    m_keybdArrowWidget->hide();

    connect(m_keybdLayoutWidget, &KbLayoutWidget::setButtonClicked, this, &LockManager::setCurrentKeyboardLayout);
    connect(m_keybdLayoutWidget, &KbLayoutWidget::setButtonClicked, m_keybdArrowWidget, &DArrowRectangle::hide);
}

void LockManager::enableZone()
{
    m_hotZoneInterface->EnableZoneDetected(true);
}

void LockManager::disableZone()
{
    m_hotZoneInterface->EnableZoneDetected(false);
}

ControlWidget *LockManager::control()
{
    return m_controlWidget;
}

void LockManager::setCurrentKeyboardLayout(QString keyboard_value)
{
    m_currentUser->setCurrentLayout(keyboard_value);
}

void LockManager::passwordMode()
{
    m_userWidget->restoreUser(m_currentUser);
    m_userWidget->show();
    m_requireShutdownWidget->hide();

    updatePasswordEditVisible(m_currentUser);

    if (m_action == Suspend) {
        m_sessionManagerIter->RequestSuspend().waitForFinished();
        m_action = Unlock;
        return;
    }

    if (m_action == Restart) {
        if (m_userState == Passwd)
            m_passwordEdit->setAlert(true, tr("Enter your password to reboot"));
        else
            m_unlockButton->setText(QApplication::translate("ShutdownWidget", "Reboot"));
        m_passwordEdit->setEnterBtnStyle(":/img/action_icons/reboot_normal.svg",
                                         ":/img/action_icons/reboot_hover.svg",
                                         ":/img/action_icons/reboot_press.svg");
    } else if (m_action == Shutdown) {
        if (m_userState == Passwd)
            m_passwordEdit->setAlert(true, tr("Enter your password to shutdown"));
        else
            m_unlockButton->setText(QApplication::translate("ShutdownWidget", "Shut down"));
        m_passwordEdit->setEnterBtnStyle(":/img/action_icons/shutdown_normal.svg",
                                         ":/img/action_icons/shutdown_hover.svg",
                                         ":/img/action_icons/shutdown_press.svg");
    } else if (m_action == Unlock) {
        m_passwordEdit->setEnterBtnStyle(":/img/action_icons/unlock_normal.svg",
                                         ":/img/action_icons/unlock_hover.svg",
                                         ":/img/action_icons/unlock_press.svg");
    }
}

void LockManager::shutdownMode()
{
    m_layoutState = PowerState;

    m_userWidget->hide();
    m_passwordEdit->hide();
    m_unlockButton->hide();
    m_requireShutdownWidget->show();
}
