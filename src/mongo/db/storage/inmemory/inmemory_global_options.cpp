// inmemory_global_options.cpp

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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/platform/basic.h"

#include "mongo/base/status.h"
#include "mongo/db/storage/inmemory/inmemory_global_options.h"
#include "mongo/util/log.h"

namespace mongo {

InMemoryGlobalOptions inMemoryGlobalOptions;

Status InMemoryGlobalOptions::add(moe::OptionSection* options) {
    moe::OptionSection inMemoryOptions("InMemory options");

    // InMemory storage engine options
    inMemoryOptions
        .addOptionChaining("storage.inMemory.engineConfig.inMemorySizeGB",
                           "inMemorySizeGB",
                           moe::Double,
                           "The maximum memory in gigabytes to use for InMemory storage. "
                           "See documentation for default.");
    inMemoryOptions
        .addOptionChaining("storage.inMemory.engineConfig.statisticsLogDelaySecs",
                           "inMemoryStatisticsLogDelaySecs",
                           moe::Int,
                           "The number of seconds between writes to statistics log. "
                           "If 0 is specified then statistics will not be logged.")
        .validRange(0, 100000)
        .setDefault(moe::Value(0));

    // hidden options
    inMemoryOptions
        .addOptionChaining("storage.inMemory.engineConfig.configString",
                           "inMemoryEngineConfigString",
                           moe::String,
                           "InMemory storage engine custom "
                           "configuration settings")
        .hidden();
    inMemoryOptions
        .addOptionChaining("storage.inMemory.collectionConfig.configString",
                           "inMemoryCollectionConfigString",
                           moe::String,
                           "InMemory custom collection configuration settings")
        .hidden();
    inMemoryOptions
        .addOptionChaining("storage.inMemory.indexConfig.configString",
                           "inMemoryIndexConfigString",
                           moe::String,
                           "InMemory custom index configuration settings")
        .hidden();

    return options->addSection(inMemoryOptions);
}

Status InMemoryGlobalOptions::store(const moe::Environment& params,
                                    const std::vector<std::string>& args) {
    // InMemory storage engine options
    if (params.count("storage.inMemory.engineConfig.inMemorySizeGB")) {
        inMemoryGlobalOptions.cacheSizeGB =
            params["storage.inMemory.engineConfig.inMemorySizeGB"].as<double>();
    }
    if (params.count("storage.inMemory.engineConfig.statisticsLogDelaySecs")) {
        inMemoryGlobalOptions.statisticsLogDelaySecs =
            params["storage.inMemory.engineConfig.statisticsLogDelaySecs"].as<int>();
    }

    // hidden options
    if (params.count("storage.inMemory.engineConfig.configString")) {
        inMemoryGlobalOptions.engineConfig =
            params["storage.inMemory.engineConfig.configString"].as<std::string>();
        log() << "Engine custom option: " << inMemoryGlobalOptions.engineConfig;
    }
    if (params.count("storage.inMemory.collectionConfig.configString")) {
        inMemoryGlobalOptions.collectionConfig =
            params["storage.inMemory.collectionConfig.configString"].as<std::string>();
        log() << "Collection custom option: " << inMemoryGlobalOptions.collectionConfig;
    }
    if (params.count("storage.inMemory.indexConfig.configString")) {
        inMemoryGlobalOptions.indexConfig =
            params["storage.inMemory.indexConfig.configString"].as<std::string>();
        log() << "Index custom option: " << inMemoryGlobalOptions.indexConfig;
    }

    return Status::OK();
}

}  // namespace mongo
