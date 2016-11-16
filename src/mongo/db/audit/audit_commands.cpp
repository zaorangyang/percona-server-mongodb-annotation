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
#include <string>

#include "mongo/base/init.h"
#include "mongo/bson/bson_field.h"
#include "mongo/db/audit.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/client_basic.h"
#include "mongo/db/commands.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/namespace_string.h"
#include "mongo/util/mongoutils/str.h"

#include "audit_options.h"

namespace mongo {

    class AuditCommand : public Command {
    public:
        AuditCommand(const char *name, bool webUI=false, const char *oldName=NULL) : Command(name, webUI, oldName) {}
        virtual ~AuditCommand() {}
        // TODO: Investigate if any other Command class virtual
        // methods need to be overridden.
        virtual bool isWriteCommandForConfigServer() const { return false; }
        virtual bool slaveOk() const { return true; }
        virtual bool supportsWriteConcern(const BSONObj& cmd) const { return false; }
    };

    class LogApplicationMessageCommand : public AuditCommand {
    public:
        LogApplicationMessageCommand() : AuditCommand("logApplicationMessage") { }
        virtual ~LogApplicationMessageCommand() { }
        virtual void help( std::stringstream &help ) const {
            help << 
                "Log a custom application message string to the audit log. Must be a string." << 
                "Example: { logApplicationMessage: \"it's a trap!\" }";
        }

        virtual void addRequiredPrivileges(const std::string& dbname,
                                           const BSONObj& cmdObj,
                                           std::vector<Privilege>* out) {
            ActionSet actions;
            actions.addAction(ActionType::logApplicationMessage);

            // TODO: Investigate if using the 'normal resource'
            // pattern of the new ResourcePattern API matches our
            // original use of SERVER_RESOURCE_NAME. We may want
            // to use somethine scoped to the given database name.
            out->push_back(Privilege(ResourcePattern::forAnyNormalResource(), actions));
        }

        bool run(OperationContext* txn, const std::string& dbname, BSONObj& jsobj, int, std::string& errmsg, BSONObjBuilder& result) {
            bool ok = true;
            const BSONElement &e = jsobj["logApplicationMessage"];

            if (e.type() == String) {
                audit::logApplicationMessage(ClientBasic::getCurrent(), e.checkAndGetStringData());
            } else {
                errmsg = "logApplicationMessage only accepts string messages";
                ok = false;
            }
            result.append("ok", ok);
            return ok;
        }
    } cmdLogApplicationMessage;

    class AuditGetOptionsCommand : public AuditCommand {
    public:
        AuditGetOptionsCommand() : AuditCommand("auditGetOptions") { }
        virtual ~AuditGetOptionsCommand() { }
        virtual void help( std::stringstream &help ) const {
            help << 
                "Get the options the audit system is currently using"
                "Example: { auditGetOptions: 1 }";
        }

        virtual void addRequiredPrivileges(const std::string& dbname,
                                           const BSONObj& cmdObj,
                                           std::vector<Privilege>* out) { }

        bool run(OperationContext* txn, const std::string& dbname, BSONObj& jsobj, int, std::string& errmsg, BSONObjBuilder& result) {
            result.appendElements(auditOptions.toBSON());
            return true;
        }
    };

    // so tests can determine where the audit log lives
    MONGO_INITIALIZER(RegisterAuditGetOptionsCommand)(InitializerContext* context) {
        if (Command::testCommandsEnabled) {
            // Leaked intentionally: a Command registers itself when constructed.
            new AuditGetOptionsCommand();
        }

        return Status::OK();
    }

}  // namespace mongo
