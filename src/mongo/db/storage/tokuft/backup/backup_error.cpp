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
#include "mongo/bson/bsonobjbuilder.h"
#include "backup_error.h"

namespace mongo {
    namespace backup {
        void Error::parse(int error_number, const char *error_string) {
            eno = error_number;
            errstring = error_string;
        }

        void Error::get(BSONObjBuilder &b) const {
            b.append("message", errstring);
            b.append("errno", eno);
            b.append("strerror", strerror(eno));
        }
    }
}
