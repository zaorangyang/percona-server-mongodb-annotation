/*======
This file is part of Percona Server for MongoDB.

Copyright (c) 2006, 2017, Percona and/or its affiliates. All rights reserved.

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

#include "mongo/db/server_options.h"
#include "mongo/db/server_parameters.h"
#include "mongo/logger/logger.h"

namespace mongo {

// Allow --redactClientLogData access via setParameter/getParameter
namespace {

class RedactClientLogDataParameter : public ServerParameter
{
    MONGO_DISALLOW_COPYING(RedactClientLogDataParameter);

public:
    RedactClientLogDataParameter();
    virtual void append(OperationContext* txn, BSONObjBuilder& b, const std::string& name);
    virtual Status set(const BSONElement& newValueElement);
    virtual Status setFromString(const std::string& str);

private:
    Status _set(bool v);
} redactClientLogDataParameter;

RedactClientLogDataParameter::RedactClientLogDataParameter()
    : ServerParameter(ServerParameterSet::getGlobal(), "redactClientLogData", true, true) {}

void RedactClientLogDataParameter::append(OperationContext* txn,
                                   BSONObjBuilder& b,
                                   const std::string& name) {
    b.append(name, logger::globalLogDomain()->shouldRedactLogs());
}

Status RedactClientLogDataParameter::set(const BSONElement& newValueElement) {
    if (!newValueElement.isBoolean()) {
        return Status(ErrorCodes::BadValue, str::stream() << name() << " has to be a boolean");
    }
    return _set(newValueElement.boolean());
}

Status RedactClientLogDataParameter::setFromString(const std::string& newValueString) {
    if (newValueString == "true" || newValueString == "1")
        return _set(true);
    if (newValueString == "false" || newValueString == "0")
        return _set(false);
    return Status(ErrorCodes::BadValue, "can't convert string to bool");
}

Status RedactClientLogDataParameter::_set(bool v) {
    logger::globalLogDomain()->setShouldRedactLogs(v);
    return Status::OK();
}

}  // namespace

}  // namespace mongo
