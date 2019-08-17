/*************************************************************************
 *  Copyright (C) 2008 by Volker Lanz <vl@fidra.de>                      *
 *  Copyright (C) 2016-2018 by Andrius Štikonas <andrius@stikonas.eu>    *
 *  Copyright (C) 2019 by Shubham <aryan100jangid@gmail.com>             *
 *                                                                       *
 *  This program is free software; you can redistribute it and/or        *
 *  modify it under the terms of the GNU General Public License as       *
 *  published by the Free Software Foundation; either version 3 of       *
 *  the License, or (at your option) any later version.                  *
 *                                                                       *
 *  This program is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of       *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        *
 *  GNU General Public License for more details.                         *
 *                                                                       *
 *  You should have received a copy of the GNU General Public License    *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.*
 *************************************************************************/

#include "backend/corebackendmanager.h"
#include "core/device.h"
#include "core/copysource.h"
#include "core/copytarget.h"
#include "core/copytargetbytearray.h"
#include "core/copysourcedevice.h"
#include "core/copytargetdevice.h"
#include "util/globallog.h"
#include "util/externalcommand.h"
#include "util/externalcommand_polkitbackend.h"
#include "util/report.h"

#include "externalcommandhelper_interface.h"

#include <QtDBus>
#include <QEventLoop>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariant>

#include <KJob>
#include <KLocalizedString>

#include <PolkitQt1/Authority>

struct ExternalCommandPrivate
{
    Report *m_Report;
    QString m_Command;
    QStringList m_Args;
    int m_ExitCode;
    QByteArray m_Output;
    QByteArray m_Input;
    DBusThread *m_thread;
    QProcess::ProcessChannelMode processChannelMode;
};

Auth::PolkitQt1Backend* ExternalCommand::m_authJob;
bool ExternalCommand::helperStarted = false;
QWidget* ExternalCommand::parent;

/** Creates a new ExternalCommand instance without Report.
    @param cmd the command to run
    @param args the arguments to pass to the command
*/
ExternalCommand::ExternalCommand(const QString& cmd, const QStringList& args, const QProcess::ProcessChannelMode processChannelMode) :
    d(std::make_unique<ExternalCommandPrivate>())
{
    d->m_Report = nullptr;
    d->m_Command = cmd;
    d->m_Args = args;
    d->m_ExitCode = -1;
    d->m_Output = QByteArray();

    m_authJob = new Auth::PolkitQt1Backend;
    
    if (!helperStarted)
        if(!startHelper())
            Log(Log::Level::error) << xi18nc("@info:status", "Could not obtain administrator privileges.");

    d->processChannelMode = processChannelMode;
}

/** Creates a new ExternalCommand instance with Report.
    @param report the Report to write output to.
    @param cmd the command to run
    @param args the arguments to pass to the command
 */
ExternalCommand::ExternalCommand(Report& report, const QString& cmd, const QStringList& args, const QProcess::ProcessChannelMode processChannelMode) :
    d(std::make_unique<ExternalCommandPrivate>())
{
    d->m_Report = report.newChild();
    d->m_Command = cmd;
    d->m_Args = args;
    d->m_ExitCode = -1;
    d->m_Output = QByteArray();
    d->processChannelMode = processChannelMode;
}

ExternalCommand::~ExternalCommand()
{
    
}

/*
void ExternalCommand::setup()
{
     connect(this, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &ExternalCommand::onFinished);
     connect(this, &ExternalCommand::readyReadStandardOutput, this, &ExternalCommand::onReadOutput);
}
*/

/** Executes the external command.
    @param timeout timeout to wait for the process to start
    @return true on success
*/
bool ExternalCommand::start(int timeout)
{
    if (command().isEmpty())
        return false;

    if (!QDBusConnection::systemBus().isConnected()) {
        qWarning() << QDBusConnection::systemBus().lastError().message();
        QTimer::singleShot(timeout, this, &ExternalCommand::quit);
        return false;
    }

    if (report())
        report()->setCommand(xi18nc("@info:status", "Command: %1 %2", command(), args().join(QStringLiteral(" "))));

    if ( qEnvironmentVariableIsSet( "KPMCORE_DEBUG" ))
        qDebug() << xi18nc("@info:status", "Command: %1 %2", command(), args().join(QStringLiteral(" ")));

    QString cmd = QStandardPaths::findExecutable(command());
    if (cmd.isEmpty())
        cmd = QStandardPaths::findExecutable(command(), { QStringLiteral("/sbin/"), QStringLiteral("/usr/sbin/"), QStringLiteral("/usr/local/sbin/") });

    auto interface = new org::kde::kpmcore::externalcommand(QStringLiteral("org.kde.kpmcore.externalcommand"),
                    QStringLiteral("/Helper"), QDBusConnection::systemBus(), this);

    interface->setTimeout(10 * 24 * 3600 * 1000); // 10 days

    bool rval = false;

    QDBusPendingCall pcall = interface->start(cmd, args(), d->m_Input, d->processChannelMode);

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pcall, this);
    QEventLoop loop;

    auto exitLoop = [&] (QDBusPendingCallWatcher *watcher) {
        loop.exit();

        if (watcher->isError())
            qWarning() << watcher->error();
        else {
            QDBusPendingReply<QVariantMap> reply = *watcher;

            d->m_Output = reply.value()[QStringLiteral("output")].toByteArray();
            setExitCode(reply.value()[QStringLiteral("exitCode")].toInt());
            rval = reply.value()[QStringLiteral("success")].toBool();
        }
    };

    connect(watcher, &QDBusPendingCallWatcher::finished, exitLoop);
    loop.exec();

    QTimer::singleShot(timeout, this, &ExternalCommand::quit);

    return rval;
}

bool ExternalCommand::copyBlocks(const CopySource& source, CopyTarget& target)
{
    bool rval = true;
    const qint64 blockSize = 10 * 1024 * 1024; // number of bytes per block to copy

    if (!QDBusConnection::systemBus().isConnected()) {
        qWarning() << QDBusConnection::systemBus().lastError().message();
        return false;
    }

    auto interface = new org::kde::kpmcore::externalcommand(QStringLiteral("org.kde.kpmcore.externalcommand"),
                QStringLiteral("/Helper"), QDBusConnection::systemBus(), this);
    interface->setTimeout(10 * 24 * 3600 * 1000); // 10 days

    QDBusPendingCall pcall = interface->copyblocks(source.path(), source.firstByte(), source.length(),
                                                   target.path(), target.firstByte(), blockSize);

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pcall, this);
    QEventLoop loop;

    auto exitLoop = [&] (QDBusPendingCallWatcher *watcher) {
        loop.exit();
        if (watcher->isError())
            qWarning() << watcher->error();
        else {
            QDBusPendingReply<QVariantMap> reply = *watcher;
            rval = reply.value()[QStringLiteral("success")].toBool();

            CopyTargetByteArray *byteArrayTarget = dynamic_cast<CopyTargetByteArray*>(&target);
            if (byteArrayTarget)
                byteArrayTarget->m_Array = reply.value()[QStringLiteral("targetByteArray")].toByteArray();
        }
        setExitCode(!rval);
    };

    connect(watcher, &QDBusPendingCallWatcher::finished, exitLoop);
    loop.exec();

    return rval;
}

bool ExternalCommand::writeData(Report& commandReport, const QByteArray& buffer, const QString& deviceNode, const quint64 firstByte)
{
    d->m_Report = commandReport.newChild();
    if (report())
        report()->setCommand(xi18nc("@info:status", "Command: %1 %2", command(), args().join(QStringLiteral(" "))));

    bool rval = true;

    if (!QDBusConnection::systemBus().isConnected()) {
        qWarning() << QDBusConnection::systemBus().lastError().message();
        return false;
    }

    auto interface = new org::kde::kpmcore::externalcommand(QStringLiteral("org.kde.kpmcore.externalcommand"),
                QStringLiteral("/Helper"), QDBusConnection::systemBus(), this);
    interface->setTimeout(10 * 24 * 3600 * 1000); // 10 days
 
    QDBusPendingCall pcall = interface->writeData(buffer, deviceNode, firstByte);

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pcall, this);
    QEventLoop loop;

    auto exitLoop = [&] (QDBusPendingCallWatcher *watcher) {
        loop.exit();
        if (watcher->isError())
            qWarning() << watcher->error();
        else {
            QDBusPendingReply<bool> reply = *watcher;
            rval = reply.argumentAt<0>();
        }
        setExitCode(!rval);
    };

    connect(watcher, &QDBusPendingCallWatcher::finished, exitLoop);
    loop.exec();

    return rval;
}


bool ExternalCommand::write(const QByteArray& input)
{
    if ( qEnvironmentVariableIsSet( "KPMCORE_DEBUG" ))
        qDebug() << "Command input:" << QString::fromLocal8Bit(input);
    
    d->m_Input = input;
    
    return true;
}

/** Runs the command.
    @param timeout timeout to use for waiting when starting and when waiting for the process to finish
    @return true on success
*/
bool ExternalCommand::run(int timeout)
{
    return start(timeout) /* && exitStatus() == 0*/;
}

//void ExternalCommand::onReadOutput()
//{
//     const QByteArray s = readAllStandardOutput();
//
//     if(m_Output.length() > 10*1024*1024) { // prevent memory overflow for badly corrupted file systems
//         if (report())
//             report()->line() << xi18nc("@info:status", "(Command is printing too much output)");
//         return;
//     }
//
//     m_Output += s;
//
//     if (report())
//         *report() << QString::fromLocal8Bit(s);
//}

void ExternalCommand::setCommand(const QString& cmd)
{
    d->m_Command = cmd;
}

const QString& ExternalCommand::command() const
{
    return d->m_Command;
}

const QStringList& ExternalCommand::args() const
{
    return d->m_Args;
}

void ExternalCommand::addArg(const QString& s)
{
    d->m_Args << s;
}

void ExternalCommand::setArgs(const QStringList& args)
{
    d->m_Args = args;
}

int ExternalCommand::exitCode() const
{
    return d->m_ExitCode;
}

const QString ExternalCommand::output() const
{
    return QString::fromLocal8Bit(d->m_Output);
}

const QByteArray& ExternalCommand::rawOutput() const
{
    return d->m_Output;
}

Report* ExternalCommand::report()
{
    return d->m_Report;
}

void ExternalCommand::setExitCode(int i)
{
    d->m_ExitCode = i;
}

/**< Dummy function for QTimer */
void ExternalCommand::quit()
{
    
}

bool ExternalCommand::startHelper()
{
    if (!QDBusConnection::systemBus().isConnected()) {
        qWarning() << QDBusConnection::systemBus().lastError().message();
        return false;
    }
    
    QDBusInterface iface(QStringLiteral("org.kde.kpmcore.helperinterface"), QStringLiteral("/Helper"), QStringLiteral("org.kde.kpmcore.externalcommand"), QDBusConnection::systemBus());
    
    if (iface.isValid()) {
        exit(0);
    }

    d->m_thread = new DBusThread;
    d->m_thread->start();

    /** Authorize using Polkit backend **/  
    
    // initialize KDE Polkit daemon
    m_authJob->initPolkitAgent(QStringLiteral("org.kde.kpmcore.externalcommand.init"), parent);

    bool isActionAuthorized = m_authJob->authorizeAction(QStringLiteral("org.kde.kpmcore.externalcommand.init"), m_authJob->callerID());
    
    auto authResult = m_authJob->actionStatus(QStringLiteral("org.kde.kpmcore.externalcommand.init"), m_authJob->callerID());
    
    // Wait until ExternalCommand Helper is ready and sends signal(Connect to newData signal)
    QEventLoop loop;
    auto exitLoop = [&] () { loop.exit(); };
    
    ExternalCommand cmd;
    auto conn = QObject::connect(&cmd, &ExternalCommand::newData, exitLoop);
    
    loop.exec();
    
    QObject::disconnect(conn);

    if (!isActionAuthorized  || authResult == PolkitQt1::Authority::No || authResult == PolkitQt1::Authority::Unknown) {
        qDebug() << "Unable to obtain Administrative privileges, the action can not be executed!!";
    }

    helperStarted = true;
    return true;
}

void ExternalCommand::stopHelper()
{
    auto interface = new org::kde::kpmcore::externalcommand(QStringLiteral("org.kde.kpmcore.externalcommand"),
                                                             QStringLiteral("/Helper"), QDBusConnection::systemBus());
    interface->exit();

}

void ExternalCommand::emitNewData(int percent)
{
    Q_UNUSED(percent)
    emit newData();
}

void ExternalCommand::emitNewData(QString message)
{
    Q_UNUSED(message)
    emit newData();
}

void DBusThread::run()
{
    if (!QDBusConnection::systemBus().registerService(QStringLiteral("org.kde.kpmcore.applicationinterface")) || 
        !QDBusConnection::systemBus().registerObject(QStringLiteral("/Application"), this, QDBusConnection::ExportAllSlots)) {
        qWarning() << QDBusConnection::systemBus().lastError().message();
        return;
    }
        
    QEventLoop loop;
    loop.exec();
}
