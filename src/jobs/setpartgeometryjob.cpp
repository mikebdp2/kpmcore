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

#include "jobs/setpartgeometryjob.h"

#include "backend/corebackend.h"
#include "backend/corebackendmanager.h"
#include "backend/corebackenddevice.h"
#include "backend/corebackendpartitiontable.h"

#include "core/partition.h"
#include "core/device.h"
#include "core/lvmdevice.h"

#include "util/report.h"

#include <KLocalizedString>

/** Creates a new SetPartGeometryJob
    @param d the Device the Partition whose geometry is to be set is on
    @param p the Partition whose geometry is to be set
    @param newstart the new start sector for the Partition
    @param newlength the new length for the Partition

    @todo Wouldn't it be better to have newfirst (new first sector) and newlast (new last sector) as args instead?
    Having a length here doesn't seem to be very consistent with the rest of the app, right?
*/
SetPartGeometryJob::SetPartGeometryJob(Device& d, Partition& p, qint64 newstart, qint64 newlength) :
    Job(),
    m_Device(d),
    m_Partition(p),
    m_NewStart(newstart),
    m_NewLength(newlength)
{
}

bool SetPartGeometryJob::run(Report& parent)
{
    bool rval = false;

    Report* report = jobStarted(parent);

    if(device().type() == Device::Type::Disk_Device || device().type() == Device::Type::SoftwareRAID_Device) {
        std::unique_ptr<CoreBackendDevice> backendDevice = CoreBackendManager::self()->backend()->openDevice(device());

        if (backendDevice) {
            std::unique_ptr<CoreBackendPartitionTable> backendPartitionTable = backendDevice->openPartitionTable();

            if (backendPartitionTable) {
                rval = backendPartitionTable->updateGeometry(*report, partition(), newStart(), newStart() + newLength() - 1);

                if (rval) {
                    partition().setFirstSector(newStart());
                    partition().setLastSector(newStart() + newLength() - 1);
                    backendPartitionTable->commit();
                }
            }
        } else
            report->line() << xi18nc("@info:progress", "Could not open device <filename>%1</filename> while trying to resize/move partition <filename>%2</filename>.", device().deviceNode(), partition().deviceNode());
    } else if (device().type() == Device::Type::LVM_Device) {

        partition().setFirstSector(newStart());
        partition().setLastSector(newStart() + newLength() - 1);

        rval = LvmDevice::resizeLV(*report, partition());
    }

    jobFinished(*report, rval);

    return rval;
}

QString SetPartGeometryJob::description() const
{
    return xi18nc("@info:progress", "Set geometry of partition <filename>%1</filename>: Start sector: %2, length: %3", partition().deviceNode(), newStart(), newLength());
}
