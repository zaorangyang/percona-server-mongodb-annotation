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

#include "engine_extension.h"

#include "mongo/util/assert_util.h"

namespace percona {

static EngineExtension* _currentEngine;

EngineExtension::EngineExtension() {
    verify(_currentEngine == nullptr);
    _currentEngine = this;
}

EngineExtension* getEngineExtension() {
    verify(_currentEngine != nullptr);
    return _currentEngine;
}

}  // end of percona namespace.
