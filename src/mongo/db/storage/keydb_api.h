/*======
This file is part of Percona Server for MongoDB.

Copyright (c) 2006, 2018, Percona and/or its affiliates. All rights reserved.

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

namespace percona {

/**
 * The interface which provides the ability to execute KeyDB-related
 * functions in an engine independent way
 */
struct KeyDBAPI {
    virtual ~KeyDBAPI() {}

    /**
     * Returns whether the engine supports feature compatibility version 3.6
     */
    virtual void keydbDropDatabase(const std::string& db) {
        // do nothing for engines which do not support KeyDB
    }
};

}  // end of percona namespace.
