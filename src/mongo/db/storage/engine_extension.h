/*======
This file is part of Percona Server for MongoDB.

Copyright (c) 2006, 2016, Percona and/or its affiliates. All rights reserved.

    Percona Server for MongoDB is free software: you can redistribute
    it and/or modify it under the terms of the GNU Affero General
    Public License, version 3, as published by the Free Software
    Foundation.

    Percona Server for MongoDB is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied
    warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public
    License along with Percona Server for MongoDB.  If not, see
    <http://www.gnu.org/licenses/>.
======= */

#pragma once

#include "mongo/db/backup/backupable.h"

namespace percona {

/**
 * Storage engine extension interface.
 */
class EngineExtension : public Backupable {
protected:
    EngineExtension();
    virtual ~EngineExtension() {}
};

/**
 * Returns the singleton EngineExtension to storage engine interface
 * for querying for additional functionality.
 * Caller does not own the pointer.
 */
EngineExtension* getEngineExtension();

}  // end of percona namespace.
