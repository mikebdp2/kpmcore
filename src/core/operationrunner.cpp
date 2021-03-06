/*************************************************************************
 *  Copyright (C) 2008 by Volker Lanz <vl@fidra.de>                      *
 *  Copyright (C) 2016 by Andrius Štikonas <andrius@stikonas.eu>         *
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

#include "core/operationrunner.h"
#include "core/operationstack.h"
#include "ops/operation.h"
#include "util/report.h"

#include <QDBusInterface>
#include <QDBusReply>
#include <QMutex>

/** Constructs an OperationRunner.
    @param ostack the OperationStack to act on
*/
OperationRunner::OperationRunner(QObject* parent, OperationStack& ostack) :
    QThread(parent),
    m_OperationStack(ostack),
    m_Report(nullptr),
    m_SuspendMutex(),
    m_Cancelling(false)
{
}

/** Runs the operations in the OperationStack. */
void OperationRunner::run()
{
    Q_ASSERT(m_Report);

    setCancelling(false);

    bool status = true;

    // Disable Plasma removable device automounting
    QStringList modules;
    QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("sessionBus"));
    QDBusInterface kdedInterface( QStringLiteral("org.kde.kded5"), QStringLiteral("/kded"), QStringLiteral("org.kde.kded5"), bus );
    QDBusReply<QStringList> reply = kdedInterface.call( QStringLiteral("loadedModules")  );
    if ( reply.isValid() )
        modules = reply.value();
    QString automounterService = QStringLiteral("device_automounter");
    bool automounter = modules.contains(automounterService);
    if (automounter)
        kdedInterface.call( QStringLiteral("unloadModule"), automounterService );

    for (int i = 0; i < numOperations(); i++) {
        suspendMutex().lock();
        suspendMutex().unlock();

        if (!status || isCancelling()) {
            break;
        }

        Operation* op = operationStack().operations()[i];
        op->setStatus(Operation::StatusRunning);

        emit opStarted(i + 1, op);

        connect(op, &Operation::progress, this, &OperationRunner::progressSub);

        status = op->execute(report());
        op->preview();

        disconnect(op, &Operation::progress, this, &OperationRunner::progressSub);

        emit opFinished(i + 1, op);
    }

    if (automounter)
        kdedInterface.call( QStringLiteral("loadModule"), automounterService );

    if (!status)
        emit error();
    else if (isCancelling())
        emit cancelled();
    else
        emit finished();
}

/** @return the number of Operations to run */
qint32 OperationRunner::numOperations() const
{
    return operationStack().operations().size();
}

/** @return the number of Jobs to run */
qint32 OperationRunner::numJobs() const
{
    qint32 result = 0;

    for (const auto &op :  operationStack().operations())
        result += op->jobs().size();

    return result;
}

/** @param op the number of the Operation to get a description for
    @return the Operation's description
*/
QString OperationRunner::description(qint32 op) const
{
    Q_ASSERT(op >= 0);
    Q_ASSERT(op < operationStack().size());

    return operationStack().operations()[op]->description();
}
