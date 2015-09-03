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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/db/auth/action_type.h"
#include "mongo/db/commands.h"
#include "mongo/db/server_options.h"
#include "mongo/db/storage_options.h"
#include "mongo/util/log.h"

#include "backup_status_command.h"
#include "backup.h"

namespace mongo {
    namespace backup {
        BackupStatusCommand::BackupStatusCommand()
            : BackupCommandBase("backupStatus") {}
        void BackupStatusCommand::addRequiredPrivileges(const std::string& dbname,
                                                          const BSONObj& cmdObj,
                                                          std::vector<Privilege>* out) {
            ActionSet actions;
            actions.addAction(ActionType::backupStatus);
            out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
        }

        void BackupStatusCommand::help(std::stringstream &h) const {
            h << "Report the current status of hot backup." << std::endl;
        }

        bool BackupStatusCommand::run(mongo::OperationContext* txn,
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

            const bool in_progress = tokubackup_backup_is_in_progress();
            const long long bytes_copied = tokubackup_get_bytes_copied();
            const unsigned int files_copied = tokubackup_get_files_copied();            
            result.append("inProgress", in_progress);
            result.append("bytesCopied", bytes_copied);
            result.append("filesCopied", files_copied);
            return true;
        }
    }

    backup::BackupStatusCommand backupStatus;
}
