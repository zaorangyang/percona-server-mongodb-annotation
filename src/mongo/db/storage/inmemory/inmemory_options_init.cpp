// inmemory_options_init.cpp

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

#include "mongo/platform/basic.h"

#include "mongo/util/options_parser/startup_option_init.h"

#include <iostream>

#include "mongo/db/storage/inmemory/inmemory_global_options.h"
#include "mongo/util/exit_code.h"
#include "mongo/util/options_parser/startup_options.h"

namespace mongo {

MONGO_MODULE_STARTUP_OPTIONS_REGISTER(InMemoryOptions)(InitializerContext* context) {
    return inMemoryGlobalOptions.add(&moe::startupOptions);
}

MONGO_STARTUP_OPTIONS_VALIDATE(InMemoryOptions)(InitializerContext* context) {
    return Status::OK();
}

MONGO_STARTUP_OPTIONS_STORE(InMemoryOptions)(InitializerContext* context) {
    Status ret = inMemoryGlobalOptions.store(moe::startupOptionsParsed, context->args());
    if (!ret.isOK()) {
        std::cerr << ret.toString() << std::endl;
        std::cerr << "try '" << context->args()[0] << " --help' for more information" << std::endl;
        ::_exit(EXIT_BADOPTIONS);
    }
    return Status::OK();
}
}
