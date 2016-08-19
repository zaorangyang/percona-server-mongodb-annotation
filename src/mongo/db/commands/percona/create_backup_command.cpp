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
#include "mongo/db/commands/percona/create_backup_command.h"
#include "mongo/db/storage/storage_options.h"
    
namespace mongo {
    extern StorageGlobalParams storageGlobalParams;
    namespace percona {
        void CreateBackupCommand::help(std::stringstream &h) const {
            h << "Creates a hot backup, into the given directory, of the files currently in the storage engine's data directory."
              << std::endl
              << "{ createBackup: <destination directory> }";
        }

        void CreateBackupCommand::addRequiredPrivileges(const std::string& dbname,
                                                        const BSONObj& cmdObj,
                                                        std::vector<Privilege>* out) {
            ActionSet actions;
            actions.addAction(ActionType::startBackup);
            out->push_back(Privilege(parseResourcePattern(dbname, cmdObj), actions));
        }

        bool CreateBackupCommand::run(mongo::OperationContext* txn,
                                      const std::string &db,
                                      BSONObj &cmdObj,
                                      int options,
                                      std::string &errmsg,
                                      BSONObjBuilder &result) {
            // TODO: Check if destination directory is empty.
            // TODO: Get global storage engine interface
            // TODO: Pass destination directory to SE API.
            return true;
        }
    } // end of percona namespace.
} // end of mongo namespace.
