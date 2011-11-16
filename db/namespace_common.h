// namespace_common.h

/**
*    Copyright (C) 2008 10gen Inc.
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


/**
 * this file contains basics utilities for handling namespaces
 */
namespace mongo {

    using std::string;

    /* in the mongo source code, "client" means "database". */

    const int MaxDatabaseNameLen = 256; // max str len for the db name, including null char

    /* e.g.
       NamespaceString ns("acme.orders");
       cout << ns.coll; // "orders"
    */
    class NamespaceString {
    public:
        string db;
        string coll; // note collection names can have periods in them for organizing purposes (e.g. "system.indexes")

        NamespaceString( const char * ns ) { init(ns); }
        NamespaceString( const string& ns ) { init(ns.c_str()); }
        string ns() const { return db + '.' + coll; }
        bool isSystem() const { return strncmp(coll.c_str(), "system.", 7) == 0; }

        /**
         * @return true if ns is 'normal'.  $ used for collections holding index data, which do not contain BSON objects in their records.
         * special case for the local.oplog.$main ns -- naming it as such was a mistake.
         */
        static bool normal(const char* ns) {
            const char *p = strchr(ns, '$');
            if( p == 0 )
                return true;
            return strcmp( ns, "local.oplog.$main" ) == 0;
        }

        static bool special(const char *ns) { 
            return !normal(ns) || strstr(ns, ".system.");
        }
        
        /**
         * samples:
         *   good:  
         *      foo  
         *      bar
         *      foo-bar
         *   bad:
         *      foo bar
         *      foo.bar
         *      foo"bar
         *        
         * @param db - a possible database name
         * @return if db is an allowed database name
         */
        static bool validDBName( const string& db ) {
            if ( db.size() == 0 || db.size() > 64 )
                return false;
            size_t good = strcspn( db.c_str() , "/\\. \"" );
            return good == db.size();
        }

    private:
        void init(const char *ns) {
            const char *p = strchr(ns, '.');
            if( p == 0 ) return;
            db = string(ns, p - ns);
            coll = p + 1;
        }
    };

    // "database.a.b.c" -> "database"
    inline void nsToDatabase(const char *ns, char *database) {
        const char *p = ns;
        char *q = database;
        while ( *p != '.' ) {
            if ( *p == 0 )
                break;
            *q++ = *p++;
        }
        *q = 0;
        if (q-database>=MaxDatabaseNameLen) {
            log() << "nsToDatabase: ns too long. terminating, buf overrun condition" << endl;
            dbexit( EXIT_POSSIBLE_CORRUPTION );
        }
    }
    inline string nsToDatabase(const char *ns) {
        char buf[MaxDatabaseNameLen];
        nsToDatabase(ns, buf);
        return buf;
    }
    inline string nsToDatabase(const string& ns) {
        size_t i = ns.find( '.' );
        if ( i == string::npos )
            return ns;
        return ns.substr( 0 , i );
    }

}
