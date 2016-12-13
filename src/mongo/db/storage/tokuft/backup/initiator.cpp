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
#include <boost/filesystem.hpp>

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/db/operation_context.h"
#include "mongo/db/server_options.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/util/log.h"

#include "initiator.h"
#include <backup.h>

namespace mongo {
    namespace backup {
        int c_poll_fun(float f, const char* c, void *p) {
            BackupInitiator *initiator = static_cast<BackupInitiator *>(p);
            if (initiator->UserWantsToCancelBackup())
                return 1;

            return 0;
        };

        void c_error_fun(int i, const char *c, void *p) {
            BackupInitiator *initiator = static_cast<BackupInitiator *>(p);
            initiator->HandleErrorFromBackupProcess(i, c);
        };

        int c_exclude_copy_fun(const char *source_file,void *p) {
            BackupInitiator *initiator = static_cast<BackupInitiator *>(p);
            return initiator->ExcludeCopy(source_file);
        }

        BackupInitiator::BackupInitiator() :
            _txn(NULL),
            _backupReturnValue(0),
            _fileErrorOccurred(false) {}

        BackupInitiator::BackupInitiator(OperationContext *txn, std::string dir) :
            _txn(txn),
            _destinationDirectory(dir),
            _backupReturnValue(0),
            _fileErrorOccurred(false) {}

        void BackupInitiator::SetOperationContext(OperationContext* txn) {
            _txn = txn;
        }

        void BackupInitiator::SetDestinationDirectory(std::string directory) {
            _destinationDirectory = directory;
        }

        void BackupInitiator::ExecuteBackup() {
            this->ResolveFullPathOfSourceDirectory();
            verify(!_sources.empty());
            verify(_sources.size() <= 2);
            this->SetupDestinationDirectory();
            if (!_fileErrorOccurred) {
                _backupReturnValue = this->CreateBackup();
            }

            this->LogAnyErrors();
        }

        bool BackupInitiator::GetResults(std::string &errmsg, BSONObjBuilder &result) {
            if (_fileErrorOccurred) {
                errmsg = _fileErrorMessage;
            }

            if (!this->IsOk()) {
                _error.get(result);
            }

            if (!_killedString.empty()) {
                result.append("reason", _killedString);
            }

            return this->IsOk() && !_fileErrorOccurred;
        }

        bool BackupInitiator::UserWantsToCancelBackup() const {
            Status s = _txn->checkForInterruptNoAssert();
            if (!s.isOK()) {
                return true;
            }

            return false;
        }

        void BackupInitiator::HandleErrorFromBackupProcess(int i, const char *c) {
            _error.parse(i, c);
        }

        int BackupInitiator::ExcludeCopy(const char *source_file) const {
            return _excludedPath == source_file ? 1 : 0;
        }

        void BackupInitiator::LogAnyErrors() const {
            bool ok = this->IsOk();
            if (ok && !_error.empty()) {
                log() << "backup succeeded but reported an error" << std::endl;
            } else if (!ok && _error.empty()) {
                log() << "backup failed but didn't report an error" << std::endl;
            }
        }

        bool BackupInitiator::SetupDestinationDirectory() {
            bool result = false;
            if (this->HasOnlyOneSourceDirectory()) {
                _destinations.push_back(_destinationDirectory);
                result = true;
            }

            return result;
        }

        void BackupInitiator::HandleFileError(const char * error) {
            const char *e = "ERROR: Hot Backup could not create backup subdirectories:"; 
            log() << *e << *error << std::endl;
            _fileErrorOccurred = true;
            _fileErrorMessage = *e;
        }

        void BackupInitiator::ResolveFullPathOfSourceDirectory() {
            // We want the fully resolved path, rid of '..' and
            // symlinks, for the data dir.
            const boost::filesystem::path data = canonical(boost::filesystem::path(storageGlobalParams.dbpath));
            std::string data_path = data.generic_string();
            _sources.push_back(data_path);
        }

        int BackupInitiator::CreateBackup() {
            const char *source_dirs[2];
            const char *dest_dirs[2];
            const size_t dir_count = _sources.size();
            for (size_t i = 0; i < dir_count; ++i) {
                source_dirs[i] = _sources[i].c_str();
                dest_dirs[i] = _destinations[i].c_str();
            }
            _excludedPath = (boost::filesystem::path(_sources.front()) / "diagnostic.data").generic_string();

            DEV {log() << "Creating backup in "
                        << _destinationDirectory
                        << std::endl;}
            return tokubackup_create_backup(source_dirs,
                                            dest_dirs,
                                            dir_count,
                                            c_poll_fun,
                                            this,
                                            c_error_fun,
                                            this,
                                            c_exclude_copy_fun,
                                            this);
        }
    }
}
