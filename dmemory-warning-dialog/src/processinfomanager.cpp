/*
 * Copyright (C) 2015 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     sbw <sbw@sbw.so>
 *             Hualet <mr.asianwang@gmail.com>
 *             kirigaya <kirigaya@mkacg.com>
 *
 * Maintainer: sbw <sbw@sbw.so>
 *             Hualet <mr.asianwang@gmail.com>
 *             kirigaya <kirigaya@mkacg.com>
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

#include "processinfomanager.h"

#include <QTimer>
#include <QDebug>
#include <QWindow>
#include <QFile>
#include <QApplication>
#include <QRegularExpression>
#include <QMap>
#include <QDBusPendingCallWatcher>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusMetaType>

const QStringList BlackListDesktopSuffix = { "google-chrome.desktop",
                                             "deepin-activator.desktop",
                                             "im-launch.desktop",
                                             "polkit-dde-authentication-agent-1.desktop",
                                             "lastore-session-helper.desktop",
                                           };

QString genericAppName(const QString &desktop)
{
    do {
        if (desktop.isEmpty())
            break;

        QFile f(desktop);
        if (!f.open(QIODevice::ReadOnly))
            break;

        const QString output = f.readAll();
        const QString locale = QLocale().name();

        QRegularExpression regex(QString("Name\\[%1\\]=(.+)$").arg(locale));
        regex.setPatternOptions(QRegularExpression::MultilineOption);
        auto match = regex.match(output);
        if (match.isValid() && match.hasMatch())
            return match.captured(1);

        regex = QRegularExpression(QString("Name=(.+)$"));
        regex.setPatternOptions(QRegularExpression::MultilineOption);
        match = regex.match(output);
        if (match.isValid() && match.hasMatch())
            return match.captured(1);

    } while (false);

    return desktop;
}

QMap<QString, QString> parseValuePairs(const QStringList &valuePairs)
{
    QMap<QString, QString> ret;

    for (const QString &s : valuePairs)
    {
        const auto &v = s.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
        if (v.size() > 1)
            ret.insert(v[0], v[1]);
    }

    return ret;
}

unsigned readProcessMemory(const QString &pid)
{
    unsigned ret = 0;

    do {
        const QString &path = QString("/proc/%1/status").arg(pid.trimmed());
        QFile stat(path);
        if (!stat.open(QIODevice::ReadOnly))
            break;

        const QString &statInfo = stat.readAll();
        const auto values = parseValuePairs(statInfo.split('\n'));

        ret += values.value("RssAnon:").toUInt();
        ret += values.value("VmPTE:").toUInt();
        ret += values.value("VmPMD:").toUInt();
    } while(false);

    return ret;
}

unsigned totalMemoryBytes(const QList<QByteArray> &processList)
{
    unsigned ret(0);

    for (const QString &pid : processList)
    {
        if (pid.isEmpty())
            continue;
        ret += readProcessMemory(pid);
    }

    // convert kb to bytes
    return ret * 1024;
}

ProcessInfoManager::ProcessInfoManager(QObject *parent)
    : QObject(parent)

    , m_refreshTimer(new QTimer(this))
    , m_startManagerInter(new StartManagerInter("com.deepin.SessionManager", "/com/deepin/StartManager", QDBusConnection::sessionBus(), this))
    , m_chromeTabsInter(new ChromeTabsInter("com.deepin.chromeExtension.TabsLimit", "/com/deepin/chromeExtension/TabsLimit", QDBusConnection::sessionBus(), this))
{
    m_refreshTimer->setSingleShot(false);
    m_refreshTimer->setInterval(1000);

    QFile sessionId("/proc/self/sessionid");
    if (sessionId.open(QIODevice::ReadOnly))
        m_sessionId = sessionId.readAll();
    else
        m_sessionId = "2";

    connect(m_refreshTimer, &QTimer::timeout, this, &ProcessInfoManager::scanProcessInfos);
    connect(m_refreshTimer, &QTimer::timeout, this, &ProcessInfoManager::scanChromeTabs);
}

void ProcessInfoManager::scanChromeTabs()
{
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(m_chromeTabsInter->GetProcessInfo(), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, &ProcessInfoManager::scanChromeTabsCB);
    connect(watcher, &QDBusPendingCallWatcher::finished, watcher, &QDBusPendingCallWatcher::deleteLater, Qt::QueuedConnection);

    watcher->waitForFinished();
}

void ProcessInfoManager::scanProcessInfos()
{
    const auto &apps = m_startManagerInter->GetApps().value();

    processInfoList.clear();

    // append basic apps
    for (auto it(apps.cbegin()); it != apps.cend(); ++it)
        appendCGroupPath(QString("/%1@dde/uiapps/%2").arg(m_sessionId).arg(it.key()), it.value());

    mergeLists();

    // sort by mem
    std::sort(processInfoList.begin(), processInfoList.end(),
              [](const ProcessInfo &a, const ProcessInfo &b) {
        return a.total_mem_bytes > b.total_mem_bytes;
    });

    emit processInfoListChanged();
}

void ProcessInfoManager::startRefreshData()
{
    m_refreshTimer->start();
    QTimer::singleShot(1, this, &ProcessInfoManager::scanProcessInfos);
}

void ProcessInfoManager::mergeLists()
{
    for (const auto tab : tabsInfoList)
        processInfoList << tab;
}

void ProcessInfoManager::scanChromeTabsCB(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QString> reply = *watcher;
    const QJsonArray tabGroups = QJsonDocument::fromJson(reply.value().toUtf8()).array();

    tabsInfoList.clear();

    for (const auto &group : tabGroups)
    {
        const QJsonObject &groupInfo = group.toObject();
        ProcessInfo procInfo;
        procInfo.total_mem_bytes = groupInfo.value("Mem").toInt();
        procInfo.id = groupInfo.value("Pid").toInt();
        procInfo.desktop = "/google-chrome.desktop";

        for (const auto &tab : groupInfo.value("Tabs").toArray())
        {
            const QJsonObject &tabInfo = tab.toObject();
            QString title = tabInfo.value("Title").toString();
            ProcessInfo tabItem;
            tabItem.id = tabInfo.value("ID").toInt();
            tabItem.app_name = title.remove(0, title.indexOf(QRegularExpression("[:：]")) + 1).trimmed();

            procInfo.sub_procs << std::move(tabItem);
        }

        tabsInfoList << std::move(procInfo);
    }

    watcher->deleteLater();
}

void ProcessInfoManager::appendCGroupPath(const QString &path, const QString &desktop)
{
    if (desktop == "sh")
        return;
    for (const auto &suffix : BlackListDesktopSuffix)
        if (desktop.endsWith(suffix))
            return;

    const QString basePath = "/sys/fs/cgroup/memory" + path;

    const auto idx = std::find_if(processInfoList.begin(), processInfoList.end(),
                               [&](const ProcessInfo &info) {
        return info.cgroup_path == basePath;
    });
    if (idx != processInfoList.end())
        return;

//    QFile mem_stat(basePath + "/memory.stat");
//    if (!mem_stat.open(QIODevice::ReadOnly))
//        return;
//    const QString &mem_info = mem_stat.readAll();
//    const QMap<QString, QString> &infos = parseValuePairs(mem_info.split('\n'));
//    const unsigned total_mem = infos.value("rss").toUInt() + infos.value("mapped_file").toUInt();

    QFile procs(basePath + "/cgroup.procs");
    if (!procs.open(QIODevice::ReadOnly))
        return;
    const auto &pidList = procs.readAll().split('\n');
//    const QString &mainProcId = pidList.first();

//    QFile procCmdline("/proc/" + mainProcId + "/cmdline");
//    if (!procCmdline.open(QIODevice::ReadOnly))
//        return;

    ProcessInfo pInfo;
    pInfo.total_mem_bytes = totalMemoryBytes(pidList);
    pInfo.cgroup_path = basePath;
    pInfo.app_name = genericAppName(desktop);
    pInfo.desktop = desktop;
    pInfo.id = pidList.first().toUInt();
    for (const auto &id : pidList)
        if (!id.isEmpty())
            pInfo.pid_list << id;

    processInfoList << std::move(pInfo);
}

//void ProcessInfoManager::appendChromeTab(const ChromeTabInfo &tabInfo)
//{
//    ProcessInfo pInfo;
//    pInfo.total_mem_bytes = tabInfo.memory;
//    pInfo.app_name = tabInfo.title;
//    pInfo.id = tabInfo.id;
//    pInfo.desktop = "/google-chrome.desktop";

//    tabsInfoList << std::move(pInfo);
//}
