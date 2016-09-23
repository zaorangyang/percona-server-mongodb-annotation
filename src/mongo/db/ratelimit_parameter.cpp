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

#include "mongo/base/init.h"
#include "mongo/db/server_options.h"
#include "mongo/db/server_parameters.h"
#include "mongo/util/mongoutils/str.h"  // for str::stream()!

namespace mongo {

// Allow rateLimit access via setParameter/getParameter
namespace {

class RateLimitParameter : public ServerParameter
{
    MONGO_DISALLOW_COPYING(RateLimitParameter);

public:
    RateLimitParameter(ServerParameterSet* sps);
    virtual void append(OperationContext* txn, BSONObjBuilder& b, const std::string& name);
    virtual Status set(const BSONElement& newValueElement);
    virtual Status setFromString(const std::string& str);

private:
    Status _set(int rateLimit);
};

MONGO_INITIALIZER_GENERAL(InitRateLimitParameter,
                          MONGO_NO_PREREQUISITES,
                          ("BeginStartupOptionParsing"))(InitializerContext*) {
    new RateLimitParameter(ServerParameterSet::getGlobal());
    return Status::OK();
}

RateLimitParameter::RateLimitParameter(ServerParameterSet* sps)
    : ServerParameter(sps, "profilingRateLimit", true, true) {}

void RateLimitParameter::append(OperationContext* txn,
                                   BSONObjBuilder& b,
                                   const std::string& name) {
    b.append(name, serverGlobalParams.rateLimit);
}

Status RateLimitParameter::set(const BSONElement& newValueElement) {
    if (!newValueElement.isNumber()) {
        return Status(ErrorCodes::BadValue, str::stream() << name() << " has to be a number");
    }
    return _set(newValueElement.numberInt());
}

Status RateLimitParameter::setFromString(const std::string& newValueString) {
    int num = 0;
    Status status = parseNumberFromString(newValueString, &num);
    if (!status.isOK()) return status;
    return _set(num);
}

Status RateLimitParameter::_set(int rateLimit) {
    if (rateLimit == 0)
        rateLimit = 1;
    if (1 <= rateLimit && rateLimit <= RATE_LIMIT_MAX) {
        serverGlobalParams.rateLimit = rateLimit;
        return Status::OK();
    }
    StringBuilder sb;
    sb << "Bad value for profilingRateLimit: " << rateLimit
       << ".  Supported range is 0-" << RATE_LIMIT_MAX;
    return Status(ErrorCodes::BadValue, sb.str());
}

}  // namespace

}  // namespace mongo
