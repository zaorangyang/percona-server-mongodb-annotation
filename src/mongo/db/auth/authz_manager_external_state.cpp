/**
*    Copyright (C) 2012 10gen Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "mongo/db/auth/authz_manager_external_state.h"

#include "mongo/base/status.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/auth/security_key.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/namespace_string.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

    AuthzManagerExternalState::AuthzManagerExternalState() {}
    AuthzManagerExternalState::~AuthzManagerExternalState() {}

    Status AuthzManagerExternalState::getPrivilegeDocument(const std::string& dbname,
                                                           const UserName& userName,
                                                           BSONObj* result) const {
        if (userName == internalSecurity.user->getName()) {
            return Status(ErrorCodes::InternalError,
                          "Requested privilege document for the internal user");
        }

        if (dbname == StringData("$external", StringData::LiteralTag()) ||
            dbname == AuthorizationManager::SERVER_RESOURCE_NAME ||
            dbname == AuthorizationManager::CLUSTER_RESOURCE_NAME) {
            return Status(ErrorCodes::UserNotFound,
                          mongoutils::str::stream() << "No privilege documents stored in the " <<
                          dbname << " user source.");
        }

        if (!NamespaceString::validDBName(dbname)) {
            return Status(ErrorCodes::BadValue, "Bad database name \"" + dbname + "\"");
        }

        std::string usersNamespace = dbname + ".system.users";

        BSONObj userBSONObj;
        BSONObjBuilder queryBuilder;
        queryBuilder.append(AuthorizationManager::USER_NAME_FIELD_NAME, userName.getUser());
        if (userName.getDB() == dbname) {
            queryBuilder.appendNull(AuthorizationManager::USER_SOURCE_FIELD_NAME);
        }
        else {
            queryBuilder.append(AuthorizationManager::USER_SOURCE_FIELD_NAME,
                                userName.getDB());
        }

        Status found = _findUser(usersNamespace, queryBuilder.obj(), &userBSONObj);
        if (!found.isOK()) {
            if (found.code() == ErrorCodes::UserNotFound) {
                // Return more detailed status that includes user name.
                return Status(ErrorCodes::UserNotFound,
                              mongoutils::str::stream() << "auth: couldn't find user " <<
                                      userName.toString() << ", " << usersNamespace,
                              0);
            } else {
                return found;
            }
        }

        *result = userBSONObj.getOwned();
        return Status::OK();
    }

    bool AuthzManagerExternalState::hasPrivilegeDocument(const std::string& dbname) const {
        std::string usersNamespace = dbname + ".system.users";

        BSONObj userBSONObj;
        BSONObj query;
        return _findUser(usersNamespace, query, &userBSONObj).isOK();
    }

}  // namespace mongo
