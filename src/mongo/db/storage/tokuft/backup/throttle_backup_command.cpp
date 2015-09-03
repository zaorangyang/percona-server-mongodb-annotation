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

#include "throttle_backup_command.h"
#include "backup.h"

namespace mongo {
    namespace backup {
        ThrottleBackupCommand::ThrottleBackupCommand()
            : BackupCommandBase("backupThrottle"), _bytesPerSecond(1024 * 1024) {}
        void ThrottleBackupCommand::addRequiredPrivileges(const std::string& dbname,
                                                          const BSONObj& cmdObj,
                                                          std::vector<Privilege>* out) {
            ActionSet actions;
            actions.addAction(ActionType::throttleBackup);
            out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
        }

        void ThrottleBackupCommand::help(std::stringstream &h) const {
            h << "Throttles hot backup to consume only N bytes/sec of I/O." << std::endl
              << "{ backupThrottle: <N> }" << std::endl
              << "N must be an integer.";
        }

        bool ThrottleBackupCommand::run(mongo::OperationContext* txn,
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
            if (!e.isNumber()) {
                errmsg = "backupThrottle argument must be a number";
                return false;
            }

            _bytesPerSecond = e.safeNumberLong();
            return this->Throttle(errmsg);
        }

        bool ThrottleBackupCommand::Throttle(std::string &errmsg) {
            if (this->BytesPerSecondIsValid(errmsg)) {
                log() << "Throttling backup to copy "
                      << _bytesPerSecond
                      << " bytes per second."
                      << std::endl;
                tokubackup_throttle_backup(_bytesPerSecond);
                return true;
            }

            return false;
        }

        bool ThrottleBackupCommand::BytesPerSecondIsValid(std::string &errmsg) const {
            return true;
            if (_bytesPerSecond < 0) {
                errmsg = "backupThrottle argument cannot be negative.";
                return false;
            }

            if (_bytesPerSecond == 0) {
                errmsg = "backupThrottle argument cannot be zero.";
                return false;
            }

            return true;
        }
    }

    backup::ThrottleBackupCommand backupThrottle;
}
