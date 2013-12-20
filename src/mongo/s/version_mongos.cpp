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
*
*    As a special exception, the copyright holders give permission to link the
*    code of portions of this program with the OpenSSL library under certain
*    conditions as described in each individual source file and distribute
*    linked combinations including the program with the OpenSSL library. You
*    must comply with the GNU Affero General Public License in all respects
*    for all of the code used other than as permitted herein. If you modify
*    file(s) with this exception, you may extend this exception to your
*    version of the file(s), but you are not obligated to do so. If you do not
*    wish to do so, delete this exception statement from your version. If you
*    delete this exception statement from all source files in the program,
*    then also delete it in the license file.
*/

#include "mongo/s/version_mongos.h"

#include <iostream>

#include "mongo/db/log_process_details.h"
#include "mongo/db/server_options.h"
#include "mongo/platform/process_id.h"
#include "mongo/util/debug_util.h"
#include "mongo/util/log.h"
#include "mongo/util/net/sock.h"
#include "mongo/util/version.h"
#include "mongo/util/version_reporting.h"

namespace mongo {

    void printShardingVersionInfo( bool out ) {
        if ( out ) {
            std::cout << "MongoS version " << versionString << " starting: pid=" <<
                ProcessId::getCurrent() << " port=" << serverGlobalParams.port <<
                ( sizeof(int*) == 4 ? " 32" : " 64" ) << "-bit host=" << getHostNameCached() <<
                " (--help for usage)" << std::endl;
            DEV std::cout << "_DEBUG build" << std::endl;
            std::cout << "git version: " << gitVersion() << std::endl;
            std::cout << openSSLVersion("OpenSSL version: ") << std::endl;
            std::cout <<  "build sys info: " << sysInfo() << std::endl;
        }
        else {
            log() << "MongoS version " << versionString << " starting: pid=" <<
                ProcessId::getCurrent() << " port=" << serverGlobalParams.port <<
                ( sizeof( int* ) == 4 ? " 32" : " 64" ) << "-bit host=" << getHostNameCached() <<
                " (--help for usage)" << std::endl;
            DEV log() << "_DEBUG build" << std::endl;
            logProcessDetails();
        }
    }
} // namespace mongo
