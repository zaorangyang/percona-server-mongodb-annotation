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

#include <string>
#include <vector>

#include "mongo/db/operation_context.h"
#include "mongo/bson/bsonobjbuilder.h"

#include "backup_error.h"

namespace mongo {
    namespace backup {
        class BackupInitiator {
        public:
            BackupInitiator();
            BackupInitiator(OperationContext *txn, std::string dir);
            void SetOperationContext(OperationContext* txn);
            void SetDestinationDirectory(std::string directory);
            void ExecuteBackup();
            bool GetResults(std::string &errmsg, BSONObjBuilder &result);
            bool UserWantsToCancelBackup() const;
            void HandleErrorFromBackupProcess(int i, const char *c);
            int ExcludeCopy(const char *source_file) const;
        private:
            bool SetupDestinationDirectory();
            void HandleFileError(const char *);
            void ResolveFullPathOfSourceDirectory();
            int CreateBackup();
            void LogAnyErrors() const;
            inline bool IsOk() const {
                return (_backupReturnValue == 0);
            }
            inline bool HasOnlyOneSourceDirectory() const {
                return _sources.size() == 1;
            }
        private:
            OperationContext *_txn;
            std::string _destinationDirectory;
            std::vector<std::string> _sources;
            std::vector<std::string> _destinations;
            int _backupReturnValue;
            Error _error;
            std::string _killedString;
            std::string _fileErrorMessage;
            bool _fileErrorOccurred;
            std::string _excludedPath;
        };
    }
}
