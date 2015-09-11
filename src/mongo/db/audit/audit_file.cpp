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

#include <iostream>

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault
#include "mongo/util/log.h"

#include "audit_file.h"

namespace mongo {

namespace audit {

int AuditFile::fsyncReturningError() const {
    if (::fsync(_fd)) {
        int _errno = errno;
        log() << "In File::fsync(), ::fsync for '" << _name
              << "' failed with " << errnoWithDescription() << std::endl;
        return _errno;
    }
    return 0;
}

int AuditFile::writeReturningError(fileofs o, const char *data, unsigned len) {
    ssize_t bytesWritten = ::pwrite(_fd, data, len, o);
    if (bytesWritten != static_cast<ssize_t>(len)) {
        _bad = true;
        int _errno = errno;
        log() << "In File::write(), ::pwrite for '" << _name
              << "' tried to write " << len
              << " bytes but only wrote " << bytesWritten
              << " bytes, failing with " << errnoWithDescription() << std::endl;
        return _errno;
    }
    return 0;
}

}  // namespace audit
}  // namespace mongo

