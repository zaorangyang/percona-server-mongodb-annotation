// namespace_index.h

/**
 *    Copyright (C) 2013 10gen Inc.
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

#include <list>
#include <string>

#include "mongo/db/diskloc.h"
#include "mongo/db/namespace.h"
#include "mongo/util/hashtab.h"

namespace mongo {

    class NamespaceDetails;

    /* NamespaceIndex is the ".ns" file you see in the data directory.  It is the "system catalog"
       if you will: at least the core parts.  (Additional info in system.* collections.)
    */
    class NamespaceIndex {
    public:
        NamespaceIndex(const std::string &dir, const std::string &database) :
            ht( 0 ), dir_( dir ), database_( database ) {}

        /* returns true if new db will be created if we init lazily */
        bool exists() const;

        void init() {
            if( !ht )
                _init();
        }

        void add_ns( const StringData& ns, DiskLoc& loc, bool capped);
        void add_ns( const StringData& ns, const NamespaceDetails* details );
        void add_ns( const Namespace& ns, const NamespaceDetails* details );

        NamespaceDetails* details(const StringData& ns);
        NamespaceDetails* details(const Namespace& ns);

        void kill_ns(const char *ns);

        bool allocated() const { return ht != 0; }

        void getNamespaces( std::list<std::string>& tofill , bool onlyCollections = true ) const;

        boost::filesystem::path path() const;

        unsigned long long fileLength() const { return f.length(); }

    private:
        void _init();
        void maybeMkdir() const;

        MongoMMF f;
        HashTable<Namespace,NamespaceDetails> *ht;
        std::string dir_;
        std::string database_;
    };

}
