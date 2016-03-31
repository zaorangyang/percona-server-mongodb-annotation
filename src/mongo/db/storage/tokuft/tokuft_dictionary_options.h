// tokuft_dictionary_options.h

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

#include <db.h>

#include "mongo/base/status.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/db/catalog/collection_options.h"
#include "mongo/util/options_parser/startup_options.h"

namespace mongo {

    namespace moe = mongo::optionenvironment;

    class TokuFTDictionaryOptions {
        std::string _objectName;

        std::string optionName(const std::string &opt) const;
        std::string shortOptionName(const std::string &opt) const;

    public:
        TokuFTDictionaryOptions(const std::string& objectName);

        Status add(moe::OptionSection* options);
        bool handlePreValidation(const moe::Environment& params);
        Status store(const moe::Environment& params, const std::vector<std::string>& args);

        BSONObj toBSON() const;
        static Status validateOptions(const BSONObj& options);
        TokuFTDictionaryOptions mergeOptions(const BSONObj& options) const;

        TOKU_COMPRESSION_METHOD compressionMethod() const;

        unsigned long long pageSize;
        unsigned long long readPageSize;
        std::string compression;
        int fanout;
    };

}
