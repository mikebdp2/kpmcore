/*************************************************************************
 *  Copyright (C) 2020 by Gaël PORTAY <gael.portay@collabora.com>        *
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

#if !defined(KPMCORE_SETPARTITIONNAMEJOB_H)

#define KPMCORE_SETPARTITIONNAMEJOB_H

#include "jobs/job.h"

class Partition;
class Device;
class Report;

class QString;

/** Set a Partition label (GPT only).
    @author Gaël PORTAY <gael.portay@collabora.com>
*/
class SetPartitionLabelJob : public Job
{
public:
    SetPartitionLabelJob(Device& d, Partition& p, const QString& newLabel);

public:
    bool run(Report& parent) override;
    QString description() const override;

protected:
    Partition& partition() {
        return m_Partition;
    }
    const Partition& partition() const {
        return m_Partition;
    }

    Device& device() {
        return m_Device;
    }
    const Device& device() const {
        return m_Device;
    }

    const QString& label() const {
        return m_Label;
    }
    void setLabel(const QString& l) {
        m_Label = l;
    }

private:
    Device& m_Device;
    Partition& m_Partition;
    QString m_Label;
};

#endif
