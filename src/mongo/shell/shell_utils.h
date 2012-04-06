// mongo/shell/shell_utils.h
/*
 *    Copyright 2010 10gen Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */


#pragma once

#include "mongo/db/jsobj.h"

namespace mongo {

    class Scope;
    class DBClientWithCommands;
    
    namespace shellUtils {

        extern std::string _dbConnect;
        extern std::string _dbAuth;
        extern map< string, set<string> > _allMyUris;
        extern bool _nokillop;

        void RecordMyLocation( const char *_argv0 );
        void installShellUtils( Scope& scope );

        void initScope( Scope &scope );
        void onConnect( DBClientWithCommands &c );

        const char* getUserDir();
        
        BSONElement oneArg(const BSONObj& args);
        extern const BSONObj undefined_;
    }
}
