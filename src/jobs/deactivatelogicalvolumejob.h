/*************************************************************************
 *  Copyright (C) 2016 by Chantara Tith <tith.chantara@gmail.com>        *
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

#if !defined(KPMCORE_DEACTIVATELOGICALVOLUMEJOB_H)

#define KPMCORE_DEACTIVATELOGICALVOLUMEJOB_H

#include "jobs/job.h"

class VolumeManagerDevice;
class Partition;
class Report;

class QString;

class DeactivateLogicalVolumeJob : public Job
{
public:
    explicit DeactivateLogicalVolumeJob(const VolumeManagerDevice& dev, const QStringList lvPaths = {});

public:
    bool run(Report& parent) override;
    QString description() const override;

protected:
    const VolumeManagerDevice& device() const {
        return m_Device;
    }

    QStringList LVList() const {
        return m_LVList;
    }

private:
    const VolumeManagerDevice& m_Device;
    const QStringList m_LVList;
};

#endif
