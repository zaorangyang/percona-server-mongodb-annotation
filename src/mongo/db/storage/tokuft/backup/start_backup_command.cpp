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

#include "mongo/db/auth/action_type.h"
#include "mongo/db/commands.h"

#include "start_backup_command.h"
#include "initiator.h"

namespace mongo {
    namespace backup {
        static bool STARTED = false;
        StartBackupCommand::StartBackupCommand()
            : BackupCommandBase("backupStart") {}

        void StartBackupCommand::addRequiredPrivileges(const std::string& dbname,
                                                       const BSONObj& cmdObj,
                                                       std::vector<Privilege>* out) {
            ActionSet actions;
            actions.addAction(ActionType::startBackup);
            out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
        }

        void StartBackupCommand::help(std::stringstream &h) const {
            h << "Creates a hot backup, in the given directory, of the files in dbpath."
              << std::endl
              << "{ backupStart: <destination directory> }";
        }

        bool StartBackupCommand::run(mongo::OperationContext* txn,
                                     const std::string &db,
                                     BSONObj &cmdObj,
                                     int options,
                                     std::string &errmsg,
                                     BSONObjBuilder &result,
                                     bool fromRepl) {
            if (!this->currentEngineSupportsHotBackup()) {
                this->printEngineSupportFailure(errmsg);
                return false;
            }

            BSONElement e = cmdObj.firstElement();
            std::string dest = e.str();
            if (dest.empty()) {
                errmsg = "invalid destination directory: '" + dest + "'";
                return false;
            }

            if (!__sync_bool_compare_and_swap(&STARTED, false, true)) {
                errmsg = "Cannot initiate backup. There already is a backup in progress.";
                return false;
            }

            BackupInitiator initiator;
            initiator.SetOperationContext(txn);
            initiator.SetDestinationDirectory(dest);
            initiator.ExecuteBackup();
            const bool success = initiator.GetResults(errmsg, result);
            STARTED = false;
            return success;
        }
    }

    backup::StartBackupCommand startBackup;
}
