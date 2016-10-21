// inmemory_global_options.h

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

#pragma once

#include "mongo/util/options_parser/startup_option_init.h"
#include "mongo/util/options_parser/startup_options.h"

namespace mongo {

namespace moe = mongo::optionenvironment;

class InMemoryGlobalOptions {
public:
    InMemoryGlobalOptions() : cacheSizeGB(0), statisticsLogDelaySecs(0) {}

    Status add(moe::OptionSection* options);
    Status store(const moe::Environment& params, const std::vector<std::string>& args);

    double cacheSizeGB;
    size_t statisticsLogDelaySecs;

    std::string engineConfig;
    std::string collectionConfig;
    std::string indexConfig;
};

extern InMemoryGlobalOptions inMemoryGlobalOptions;
}
