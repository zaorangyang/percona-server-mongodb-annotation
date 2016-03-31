// tokuft_engine_options.h
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

#pragma once

#include "mongo/util/options_parser/startup_options.h"

namespace mongo {

    namespace moe = mongo::optionenvironment;

    class TokuFTEngineOptions {
    public:
        TokuFTEngineOptions();

        Status add(moe::OptionSection* options);
        bool handlePreValidation(const moe::Environment& params);
        Status store(const moe::Environment& params, const std::vector<std::string>& args);

        unsigned long long cacheSize;
        int checkpointPeriod;
        int cleanerIterations;
        int cleanerPeriod;
        bool directio;
        int fsRedzone;
        int journalCommitInterval;
        int lockTimeout;
        unsigned long long locktreeMaxMemory;
        bool directoryForIndexes;

        // advanced
        bool compressBuffersBeforeEviction;
        int numCachetableBucketMutexes;
    };

}
