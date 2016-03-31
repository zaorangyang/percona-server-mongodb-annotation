// tokuft_global_options.cpp

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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/base/status.h"
#include "mongo/util/log.h"
#include "mongo/db/storage/tokuft/tokuft_global_options.h"

namespace mongo {

    TokuFTGlobalOptions::TokuFTGlobalOptions()
        : collectionOptions("collection"),
          indexOptions("index")
    {}

    Status TokuFTGlobalOptions::add(moe::OptionSection* options) {
        Status s = engineOptions.add(options);
        if (!s.isOK()) {
            return s;
        }
        s = collectionOptions.add(options);
        if (!s.isOK()) {
            return s;
        }
        s = indexOptions.add(options);
        if (!s.isOK()) {
            return s;
        }

        return Status::OK();
    }

    bool TokuFTGlobalOptions::handlePreValidation(const moe::Environment& params) {
        return engineOptions.handlePreValidation(params) &&
                collectionOptions.handlePreValidation(params) &&
                indexOptions.handlePreValidation(params);
    }

    Status TokuFTGlobalOptions::store(const moe::Environment& params,
                                 const std::vector<std::string>& args) {
        Status s = tokuftGlobalOptions.engineOptions.store(params, args);
        if (!s.isOK()) {
            return s;
        }

        s = tokuftGlobalOptions.collectionOptions.store(params, args);
        if (!s.isOK()) {
            return s;
        }

        s = tokuftGlobalOptions.indexOptions.store(params, args);
        if (!s.isOK()) {
            return s;
        }

        return Status::OK();
    }
}
