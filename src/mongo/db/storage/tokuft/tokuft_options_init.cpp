// tokuft_options_init.cpp

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
 
#include "mongo/util/options_parser/startup_option_init.h"
#include "mongo/util/options_parser/startup_options.h"

#include "mongo/db/storage/tokuft/tokuft_global_options.h"

namespace mongo {

    // Interface to MongoDB option parsing.
    TokuFTGlobalOptions tokuftGlobalOptions;

    MONGO_MODULE_STARTUP_OPTIONS_REGISTER(TokuFTOptions)(InitializerContext* context) {
        return tokuftGlobalOptions.add(&moe::startupOptions);
    }

    MONGO_STARTUP_OPTIONS_VALIDATE(TokuFTOptions)(InitializerContext* context) {
        if (!tokuftGlobalOptions.handlePreValidation(moe::startupOptionsParsed)) {
            ::_exit(EXIT_SUCCESS);
        }
        Status ret = moe::startupOptionsParsed.validate();
        if (!ret.isOK()) {
            return ret;
        }
        return Status::OK();
    }

    MONGO_STARTUP_OPTIONS_STORE(TokuFTOptions)(InitializerContext* context) {
        Status ret = tokuftGlobalOptions.store(moe::startupOptionsParsed, context->args());
        if (!ret.isOK()) {
            std::cerr << ret.toString() << std::endl;
            std::cerr << "try '" << context->args()[0] << " --help' for more information"
                      << std::endl;
            ::_exit(EXIT_BADOPTIONS);
        }
        return Status::OK();
    }
}
