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

#include "fs/xfs.h"

#include "util/externalcommand.h"
#include "util/capacity.h"
#include "util/report.h"

#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <KLocalizedString>

namespace FS
{
FileSystem::CommandSupportType xfs::m_GetUsed = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType xfs::m_GetLabel = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType xfs::m_Create = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType xfs::m_Grow = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType xfs::m_Move = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType xfs::m_Check = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType xfs::m_Copy = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType xfs::m_Backup = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType xfs::m_SetLabel = FileSystem::cmdSupportNone;

xfs::xfs(qint64 firstsector, qint64 lastsector, qint64 sectorsused, const QString& label, const QVariantMap& features) :
    FileSystem(firstsector, lastsector, sectorsused, label, features, FileSystem::Type::Xfs)
{
}

void xfs::init()
{
    m_GetLabel = cmdSupportCore;
    m_SetLabel = m_GetUsed = findExternal(QStringLiteral("xfs_db")) ? cmdSupportFileSystem : cmdSupportNone;
    m_Create = findExternal(QStringLiteral("mkfs.xfs")) ? cmdSupportFileSystem : cmdSupportNone;

    m_Check = findExternal(QStringLiteral("xfs_repair")) ? cmdSupportFileSystem : cmdSupportNone;
    m_Grow = (findExternal(QStringLiteral("xfs_growfs"), { QStringLiteral("-V") }) && m_Check != cmdSupportNone) ? cmdSupportFileSystem : cmdSupportNone;
    m_Copy = findExternal(QStringLiteral("xfs_copy")) ? cmdSupportFileSystem : cmdSupportNone;
    m_Move = (m_Check != cmdSupportNone) ? cmdSupportCore : cmdSupportNone;
    m_Backup = cmdSupportCore;
}

bool xfs::supportToolFound() const
{
    return
        m_GetUsed != cmdSupportNone &&
        m_GetLabel != cmdSupportNone &&
        m_SetLabel != cmdSupportNone &&
        m_Create != cmdSupportNone &&
        m_Check != cmdSupportNone &&
//          m_UpdateUUID != cmdSupportNone &&
        m_Grow != cmdSupportNone &&
//          m_Shrink != cmdSupportNone &&
        m_Copy != cmdSupportNone &&
        m_Move != cmdSupportNone &&
        m_Backup != cmdSupportNone;
//          m_GetUUID != cmdSupportNone;
}

FileSystem::SupportTool xfs::supportToolName() const
{
    return SupportTool(QStringLiteral("xfsprogs"), QUrl(QStringLiteral("https://xfs.org/index.php/Getting_the_latest_source_code")));
}

qint64 xfs::minCapacity() const
{
    return 32 * Capacity::unitFactor(Capacity::Unit::Byte, Capacity::Unit::MiB);
}

qint64 xfs::maxCapacity() const
{
    return Capacity::unitFactor(Capacity::Unit::Byte, Capacity::Unit::EiB);
}

int xfs::maxLabelLength() const
{
    return 12;
}

qint64 xfs::readUsedCapacity(const QString& deviceNode) const
{
    ExternalCommand cmd(QStringLiteral("xfs_db"), { QStringLiteral("-c"), QStringLiteral("sb 0"), QStringLiteral("-c"), QStringLiteral("print"), deviceNode });

    if (cmd.run(-1) && cmd.exitCode() == 0) {
        qint64 dBlocks = -1;
        QRegularExpression re(QStringLiteral("dblocks = (\\d+)"));
        QRegularExpressionMatch reDBlocks = re.match(cmd.output());

        if (reDBlocks.hasMatch())
            dBlocks = reDBlocks.captured(1).toLongLong();

        qint64 blockSize = -1;
        re.setPattern(QStringLiteral("blocksize = (\\d+)"));
        QRegularExpressionMatch reBlockSize = re.match(cmd.output());

        if (reBlockSize.hasMatch())
            blockSize = reBlockSize.captured(1).toLongLong();

        qint64 fdBlocks = -1;
        re.setPattern(QStringLiteral("fdblocks = (\\d+)"));
        QRegularExpressionMatch reFdBlocks = re.match(cmd.output());

        if (reFdBlocks.hasMatch())
            fdBlocks = reFdBlocks.captured(1).toLongLong();

        if (dBlocks > -1 && blockSize > -1 && fdBlocks > -1)
            return (dBlocks - fdBlocks) * blockSize;
    }

    return -1;
}

bool xfs::writeLabel(Report& report, const QString& deviceNode, const QString& newLabel)
{
    ExternalCommand cmd(report, QStringLiteral("xfs_db"), { QStringLiteral("-x"), QStringLiteral("-c"), QStringLiteral("sb 0"), QStringLiteral("-c"), QStringLiteral("label ") + newLabel, deviceNode });
    return cmd.run(-1) && cmd.exitCode() == 0;
}

bool xfs::check(Report& report, const QString& deviceNode) const
{
    ExternalCommand cmd(report, QStringLiteral("xfs_repair"), { QStringLiteral("-v"), deviceNode });
    return cmd.run(-1) && cmd.exitCode() == 0;
}

bool xfs::create(Report& report, const QString& deviceNode)
{
    ExternalCommand cmd(report, QStringLiteral("mkfs.xfs"), { QStringLiteral("-f"), deviceNode });
    return cmd.run(-1) && cmd.exitCode() == 0;
}

bool xfs::copy(Report& report, const QString& targetDeviceNode, const QString& sourceDeviceNode) const
{
    ExternalCommand cmd(report, QStringLiteral("xfs_copy"), { sourceDeviceNode, targetDeviceNode });

    // xfs_copy behaves a little strangely. It apparently kills itself at the end of main, causing QProcess
    // to report that it crashed.
    // See https://marc.info/?l=linux-xfs&m=110178337825926&w=2
    // So we cannot rely on QProcess::exitStatus() and thus not on ExternalCommand::run() returning true.

    cmd.run(-1);
    return cmd.exitCode() == 0;
}

bool xfs::resize(Report& report, const QString& deviceNode, qint64) const
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        report.line() << xi18nc("@info:progress", "Resizing XFS file system on partition <filename>%1</filename> failed: Could not create temp dir.", deviceNode);
        return false;
    }

    bool rval = false;

    ExternalCommand mountCmd(report, QStringLiteral("mount"), { QStringLiteral("--verbose"), QStringLiteral("--types"), QStringLiteral("xfs"), deviceNode, tempDir.path() });

    if (mountCmd.run(-1)) {
        ExternalCommand resizeCmd(report, QStringLiteral("xfs_growfs"), { tempDir.path() });

        if (resizeCmd.run(-1) && resizeCmd.exitCode() == 0)
            rval = true;
        else
            report.line() << xi18nc("@info:progress", "Resizing XFS file system on partition <filename>%1</filename> failed: xfs_growfs failed.", deviceNode);

        ExternalCommand unmountCmd(report, QStringLiteral("umount"), { tempDir.path() });

        if (!unmountCmd.run(-1))
            report.line() << xi18nc("@info:progress", "<warning>Resizing XFS file system on partition <filename>%1</filename> failed: Unmount failed.</warning>", deviceNode);
    } else
        report.line() << xi18nc("@info:progress", "Resizing XFS file system on partition <filename>%1</filename> failed: Initial mount failed.", deviceNode);

    return rval;
}

bool xfs::resizeOnline(Report& report, const QString& deviceNode, const QString& mountPoint, qint64) const
{
    ExternalCommand resizeCmd(report, QStringLiteral("xfs_growfs"), { mountPoint });

    if (resizeCmd.run(-1) && resizeCmd.exitCode() == 0)
        return true;

    report.line() << xi18nc("@info:progress", "Resizing XFS file system on partition <filename>%1</filename> failed: xfs_growfs failed.", deviceNode);
    return false;
}
}
