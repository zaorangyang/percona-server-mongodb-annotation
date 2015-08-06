/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:

/*======
This file is part of Percona Server for MongoDB.

Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved.

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


#include <cstdio>
#include <iostream>
//#include <memory>
#include <string>

#include <boost/filesystem/path.hpp>
#include <boost/scoped_ptr.hpp>

#include "mongo/base/init.h"
#include "mongo/bson/bson_field.h"
#include "mongo/db/audit.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/client_basic.h"
#include "mongo/db/commands.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/matcher/matcher.h"
#include "mongo/db/namespace_string.h"
#include "mongo/util/concurrency/mutex.h"
#include "mongo/util/exit_code.h"
#include "mongo/util/file.h"
//#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/net/sock.h"
#include "mongo/util/paths.h"
#include "mongo/util/time_support.h"

#include "audit_options.h"

namespace mongo {

namespace audit {


    AuditOptions::AuditOptions() :
        format("JSON"),
        path(""),
        destination("file"),
        filter("{}") {
    }

    BSONObj AuditOptions::toBSON() {
        return BSON("format" << format <<
                    "path" << path <<
                    "destination" << destination <<
                    "filter" << filter);
    }

    Status AuditOptions::initializeFromCommandLine() {
        if (serverGlobalParams.auditFormat != "") {
            if (serverGlobalParams.auditFormat != "JSON") {
                return Status(ErrorCodes::BadValue,
                              "The only audit format currently supported is `JSON'");
            }
            format = serverGlobalParams.auditFormat;
        }

        if (serverGlobalParams.auditPath != "") {
            File auditFile;
            auditFile.open(serverGlobalParams.auditPath.c_str(), false, false);
            if (auditFile.bad()) {
                return Status(ErrorCodes::BadValue,
                              "Could not open a file for writing at the given auditPath: "
                              + serverGlobalParams.auditPath);
            }
            path = serverGlobalParams.auditPath;
        } else if (!serverGlobalParams.logWithSyslog && !serverGlobalParams.logpath.empty()) {
            path = (boost::filesystem::path(serverGlobalParams.logpath).parent_path() / "auditLog.json").native();
        } else if (!storageGlobalParams.dbpath.empty()) {
            path = (boost::filesystem::path(storageGlobalParams.dbpath) / "auditLog.json").native();
        } else {
            path = (boost::filesystem::path(serverGlobalParams.cwd) / "auditLog.json").native();
        }

        if (serverGlobalParams.auditDestination != "") {
            if (serverGlobalParams.auditDestination != "file") {
                return Status(ErrorCodes::BadValue,
                              "The only audit destination currently supported is `file'");
            }
            destination = serverGlobalParams.auditDestination;
        }

        if (serverGlobalParams.auditFilter != "") {
            try {
                fromjson(serverGlobalParams.auditFilter);
            } catch (const std::exception &ex) {
                return Status(ErrorCodes::BadValue,
                              "Could not parse audit filter into valid json: "
                              + serverGlobalParams.auditFilter);
            }
            filter = serverGlobalParams.auditFilter;
        }

        return Status::OK();
    }
    
    AuditOptions _auditOptions;

}  // namespace audit
}  // namespace mongo
