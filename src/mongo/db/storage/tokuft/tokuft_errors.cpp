// tokuft_errors.cpp

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

#include "mongo/base/error_codes.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/storage/tokuft/tokuft_errors.h"
#include "mongo/util/mongoutils/str.h"

#include <db.h>
#include <ftcxx/exceptions.hpp>

namespace mongo {

    Status statusFromTokuFTException(const ftcxx::ft_exception &ftexc) {
        int r = ftexc.code();
        std::string errmsg = str::stream() << "TokuFT: " << ftexc.what();
        if (r == DB_KEYEXIST) {
            return Status(ErrorCodes::DuplicateKey, errmsg);
        } else if (r == DB_LOCK_DEADLOCK) {
            throw WriteConflictException();
            //return Status(ErrorCodes::WriteConflict, errmsg);
        } else if (r == DB_LOCK_NOTGRANTED) {
            throw WriteConflictException();
            //return Status(ErrorCodes::LockTimeout, errmsg);
        } else if (r == DB_NOTFOUND) {
            return Status(ErrorCodes::NoSuchKey, errmsg);
        } else if (r == TOKUDB_OUT_OF_LOCKS) {
            throw WriteConflictException();
            //return Status(ErrorCodes::LockFailed, errmsg);
        } else if (r == TOKUDB_DICTIONARY_TOO_OLD) {
            return Status(ErrorCodes::UnsupportedFormat, errmsg);
        } else if (r == TOKUDB_DICTIONARY_TOO_NEW) {
            return Status(ErrorCodes::UnsupportedFormat, errmsg);
        } else if (r == TOKUDB_MVCC_DICTIONARY_TOO_NEW) {
            throw WriteConflictException();
            //return Status(ErrorCodes::NamespaceNotFound, errmsg);
        }

        return Status(ErrorCodes::InternalError,
                      str::stream() << "TokuFT: internal error code "
                      << ftexc.code() << ": " << ftexc.what());
    }

    Status statusFromTokuFTError(int r) {
        if (r == 0) {
            return Status::OK();
        }
        ftcxx::ft_exception ftexc(r);
        return statusFromTokuFTException(ftexc);
    }

}
