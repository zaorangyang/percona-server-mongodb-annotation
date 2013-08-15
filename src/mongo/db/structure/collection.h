// collection.h

/**
*    Copyright (C) 2012 10gen Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <string>

#include "mongo/base/string_data.h"
#include "mongo/db/diskloc.h"

namespace mongo {

    class Database;
    class NamespaceDetails;

    /**
     * this is NOT safe through a yield right now
     * not sure if it will be, or what yet
     */
    class CollectionTemp {
    public:
        CollectionTemp( const StringData& fullNS,
                        NamespaceDetails* details,
                        Database* database );

    private:
        std::string _ns; // TODO: this copy might be annoyingly slow
        NamespaceDetails* _details;
        Database* _database;
    };

}
