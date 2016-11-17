/*======
This file is part of Percona Server for MongoDB.

Copyright (c) 2006, 2016, Percona and/or its affiliates. All rights reserved.

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

#include <boost/filesystem.hpp>

#include "mongo/db/auth/action_type.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/backup/backupable.h"
#include "mongo/db/commands.h"
#include "mongo/db/service_context.h"
#include "mongo/db/storage/engine_extension.h"
#include "mongo/db/storage/storage_options.h"

namespace mongo {
    extern StorageGlobalParams storageGlobalParams;
}
using namespace mongo;

namespace percona {

class CreateBackupCommand : public Command {
public:
    CreateBackupCommand() : Command("createBackup") {}
    void help(std::stringstream& help) const override {
        help << "Creates a hot backup, into the given directory, of the files currently in the "
                "storage engine's data directory."
             << std::endl
             << "{ createBackup: 1, backupDir: <destination directory> }";
    }
    Status checkAuthForCommand(ClientBasic* client,
                               const std::string& dbname,
                               const BSONObj& cmdObj) override {
        return AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                   ResourcePattern::forAnyNormalResource(), ActionType::startBackup)
            ? Status::OK()
            : Status(ErrorCodes::Unauthorized, "Unauthorized");
    }
    bool adminOnly() const override {
        return true;
    }
    bool slaveOk() const override {
        return true;
    }
    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return false;
    }
    bool run(mongo::OperationContext* txn,
             const std::string& db,
             BSONObj& cmdObj,
             int options,
             std::string& errmsg,
             BSONObjBuilder& result) override;
} createBackupCmd;

bool CreateBackupCommand::run(mongo::OperationContext* txn,
                              const std::string& db,
                              BSONObj& cmdObj,
                              int options,
                              std::string& errmsg,
                              BSONObjBuilder& result) {
    namespace fs = boost::filesystem;

    const std::string& dest = cmdObj["backupDir"].String();
    fs::path destPath(dest);

    // Validate destination directory.
    try {
        if (!destPath.is_absolute()) {
            errmsg = "Destination path must be absolute";
            return false;
        }

        if (!fs::is_directory(destPath)) {
            errmsg = "Destination directory doesn't exist";
            return false;
        }

        if (!fs::is_empty(destPath)) {
            errmsg = "Destination directory is not empty";
            return false;
        }
    } catch (const fs::filesystem_error& ex) {
        errmsg = ex.what();
        return false;
    }

    // Flush all files first.
    auto se = getGlobalServiceContext()->getGlobalStorageEngine();
    se->flushAllFiles(true);

    // Do the backup itself.
    const auto status = se->hotBackup(dest);

    if (!status.isOK()) {
        errmsg = status.reason();
        return false;
    }

    // Copy storage engine metadata.
    try {
        const char* storageMetadata = "storage.bson";
        fs::path srcPath(mongo::storageGlobalParams.dbpath);
        fs::copy_file(srcPath / storageMetadata, destPath / storageMetadata, fs::copy_option::none);
    } catch (const fs::filesystem_error& ex) {
        errmsg = ex.what();
        return false;
    }
    return true;
}

}  // end of percona namespace.
