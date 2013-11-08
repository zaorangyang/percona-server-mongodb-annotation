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
*
*    As a special exception, the copyright holders give permission to link the
*    code of portions of this program with the OpenSSL library under certain
*    conditions as described in each individual source file and distribute
*    linked combinations including the program with the OpenSSL library. You
*    must comply with the GNU Affero General Public License in all respects for
*    all of the code used other than as permitted herein. If you modify file(s)
*    with this exception, you may extend this exception to your version of the
*    file(s), but you are not obligated to do so. If you do not wish to do so,
*    delete this exception statement from your version. If you delete this
*    exception statement from all source files in the program, then also delete
*    it in the license file.
*/

#include "mongo/db/auth/authz_manager_external_state_local.h"

#include "mongo/base/status.h"
#include "mongo/bson/mutable/document.h"
#include "mongo/bson/mutable/element.h"
#include "mongo/bson/util/bson_extract.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/auth/user_document_parser.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

    AuthzManagerExternalStateLocal::AuthzManagerExternalStateLocal() :
        _roleGraphState(roleGraphStateInitial) {}
    AuthzManagerExternalStateLocal::~AuthzManagerExternalStateLocal() {}

    Status AuthzManagerExternalStateLocal::initialize() {
        Status status = _initializeRoleGraph();
        if (!status.isOK()) {
            if (status == ErrorCodes::GraphContainsCycle) {
                error() << "Cycle detected in admin.system.roles; role inheritance disabled. "
                    "Remove the listed cycle and any others to re-enable role inheritance. " <<
                    status.reason();
            }
            else {
                error() << "Could not generate role graph from admin.system.roles; "
                    "only system roles available. TODO EXPLAIN REMEDY. " << status;
            }
        }

        return Status::OK();
    }

    Status AuthzManagerExternalStateLocal::getStoredAuthorizationVersion(int* outVersion) {
        BSONObj versionDoc;
        Status status = findOne(AuthorizationManager::versionCollectionNamespace,
                                AuthorizationManager::versionDocumentQuery,
                                &versionDoc);
        if (status.isOK()) {
            BSONElement versionElement = versionDoc[AuthorizationManager::schemaVersionFieldName];
            if (versionElement.isNumber()) {
                *outVersion = versionElement.numberInt();
                return Status::OK();
            }
            else if (versionElement.eoo()) {
                return Status(ErrorCodes::NoSuchKey, mongoutils::str::stream() <<
                              "No " << AuthorizationManager::schemaVersionFieldName <<
                              " field in version document.");
            }
            else {
                return Status(ErrorCodes::TypeMismatch, mongoutils::str::stream() <<
                              "Bad (non-numeric) type " << versionElement.type() <<
                              "for " << AuthorizationManager::schemaVersionFieldName <<
                              " field in version document");
            }
        }
        else if (status == ErrorCodes::NoMatchingDocument) {
            if (hasAnyPrivilegeDocuments()) {
                *outVersion = AuthorizationManager::schemaVersion24;
            }
            else {
                *outVersion = AuthorizationManager::schemaVersion26Final;
            }
            return Status::OK();
        }
        else {
            return status;
        }
    }

namespace {
    void addRoleNameToObjectElement(mutablebson::Element object, const RoleName& role) {
        fassert(17153, object.appendString(AuthorizationManager::ROLE_NAME_FIELD_NAME,
                                           role.getRole()));
        fassert(17154, object.appendString(AuthorizationManager::ROLE_SOURCE_FIELD_NAME,
                                           role.getDB()));
    }

    void addRoleNameObjectsToArrayElement(mutablebson::Element array, RoleNameIterator roles) {
        for (; roles.more(); roles.next()) {
            mutablebson::Element roleElement = array.getDocument().makeElementObject("");
            addRoleNameToObjectElement(roleElement, roles.get());
            fassert(17155, array.pushBack(roleElement));
        }
    }

    void addPrivilegeObjectsOrWarningsToArrayElement(mutablebson::Element privilegesElement,
                                                     mutablebson::Element warningsElement,
                                                     const PrivilegeVector& privileges) {
        std::string errmsg;
        for (size_t i = 0; i < privileges.size(); ++i) {
            ParsedPrivilege pp;
            if (ParsedPrivilege::privilegeToParsedPrivilege(privileges[i], &pp, &errmsg)) {
                fassert(17156, privilegesElement.appendObject("", pp.toBSON()));
            } else {
                fassert(17157,
                        warningsElement.appendString(
                        "",
                        std::string(mongoutils::str::stream() <<
                                    "Skipped privileges on resource " <<
                                    privileges[i].getResourcePattern().toString() <<
                                    ". Reason: " << errmsg)));
            }
        }
    }
}  // namespace

    Status AuthzManagerExternalStateLocal::getUserDescription(
            const UserName& userName,
            BSONObj* result) {

        BSONObj userDoc;
        Status status = _getUserDocument(userName, &userDoc);
        if (!status.isOK())
            return status;

        BSONElement directRolesElement;
        status = bsonExtractTypedField(userDoc, "roles", Array, &directRolesElement);
        if (!status.isOK())
            return status;
        std::vector<RoleName> directRoles;
        status = V2UserDocumentParser::parseRoleVector(BSONArray(directRolesElement.Obj()),
                                                       &directRoles);
        if (!status.isOK())
            return status;

        unordered_set<RoleName> indirectRoles;
        PrivilegeVector allPrivileges;
        bool isRoleGraphInconsistent;
        {
            boost::lock_guard<boost::mutex> lk(_roleGraphMutex);
            isRoleGraphInconsistent = _roleGraphState == roleGraphStateConsistent;
            for (size_t i = 0; i < directRoles.size(); ++i) {
                const RoleName& role(directRoles[i]);
                indirectRoles.insert(role);
                if (isRoleGraphInconsistent) {
                    for (RoleNameIterator subordinates = _roleGraph.getIndirectSubordinates(role);
                         subordinates.more();
                         subordinates.next()) {

                        indirectRoles.insert(subordinates.get());
                    }
                }
                const PrivilegeVector& rolePrivileges(
                        isRoleGraphInconsistent ?
                        _roleGraph.getAllPrivileges(role) :
                        _roleGraph.getDirectPrivileges(role));
                for (PrivilegeVector::const_iterator priv = rolePrivileges.begin(),
                         end = rolePrivileges.end();
                     priv != end;
                     ++priv) {

                    Privilege::addPrivilegeToPrivilegeVector(&allPrivileges, *priv);
                }
            }
        }

        mutablebson::Document resultDoc(userDoc, mutablebson::Document::kInPlaceDisabled);
        mutablebson::Element indirectRolesElement = resultDoc.makeElementArray("indirectRoles");
        mutablebson::Element privilegesElement = resultDoc.makeElementArray("privileges");
        mutablebson::Element warningsElement = resultDoc.makeElementArray("warnings");
        fassert(17158, resultDoc.root().pushBack(privilegesElement));
        fassert(17159, resultDoc.root().pushBack(indirectRolesElement));
        if (!isRoleGraphInconsistent) {
            fassert(17160, warningsElement.appendString(
                            "", "Role graph inconsistent, only direct privileges available."));
        }
        addRoleNameObjectsToArrayElement(indirectRolesElement,
                                         makeRoleNameIteratorForContainer(indirectRoles));
        addPrivilegeObjectsOrWarningsToArrayElement(
                privilegesElement, warningsElement, allPrivileges);
        if (warningsElement.hasChildren()) {
            fassert(17161, resultDoc.root().pushBack(warningsElement));
        }
        *result = resultDoc.getObject();
        return Status::OK();
    }

    Status AuthzManagerExternalStateLocal::getRoleDescription(const RoleName& roleName,
                                                              bool showPrivileges,
                                                              BSONObj* result) {
        boost::lock_guard<boost::mutex> lk(_roleGraphMutex);
        return _getRoleDescription_inlock(roleName, showPrivileges, result);
    }

    Status AuthzManagerExternalStateLocal::_getRoleDescription_inlock(const RoleName& roleName,
                                                                      bool showPrivileges,
                                                                      BSONObj* result) {
        if (!_roleGraph.roleExists(roleName))
            return Status(ErrorCodes::RoleNotFound, "No role named " + roleName.toString());

        mutablebson::Document resultDoc;
        fassert(17162, resultDoc.root().appendString(
                        AuthorizationManager::ROLE_NAME_FIELD_NAME, roleName.getRole()));
        fassert(17163, resultDoc.root().appendString(
                        AuthorizationManager::ROLE_SOURCE_FIELD_NAME, roleName.getDB()));
        mutablebson::Element rolesElement = resultDoc.makeElementArray("roles");
        fassert(17164, resultDoc.root().pushBack(rolesElement));
        mutablebson::Element indirectRolesElement = resultDoc.makeElementArray("indirectRoles");
        fassert(17165, resultDoc.root().pushBack(indirectRolesElement));
        mutablebson::Element privilegesElement = resultDoc.makeElementArray("privileges");
        if (showPrivileges) {
            fassert(17166, resultDoc.root().pushBack(privilegesElement));
        }
        fassert(17267,
                resultDoc.root().appendBool("isBuiltin", _roleGraph.isBuiltinRole(roleName)));
        mutablebson::Element warningsElement = resultDoc.makeElementArray("warnings");

        addRoleNameObjectsToArrayElement(rolesElement, _roleGraph.getDirectSubordinates(roleName));
        if (_roleGraphState == roleGraphStateConsistent) {
            addRoleNameObjectsToArrayElement(
                    indirectRolesElement, _roleGraph.getIndirectSubordinates(roleName));
            if (showPrivileges) {
                addPrivilegeObjectsOrWarningsToArrayElement(
                        privilegesElement, warningsElement, _roleGraph.getAllPrivileges(roleName));
            }
        }
        else if (showPrivileges) {
            warningsElement.appendString(
                    "", "Role graph state inconsistent; only direct privileges available.");
            addPrivilegeObjectsOrWarningsToArrayElement(
                    privilegesElement, warningsElement, _roleGraph.getDirectPrivileges(roleName));
        }
        if (warningsElement.hasChildren()) {
            fassert(17167, resultDoc.root().pushBack(warningsElement));
        }
        *result = resultDoc.getObject();
        return Status::OK();
    }

    Status AuthzManagerExternalStateLocal::getRoleDescriptionsForDB(const std::string dbname,
                                                                    bool showPrivileges,
                                                                    bool showBuiltinRoles,
                                                                    vector<BSONObj>* result) {
        boost::lock_guard<boost::mutex> lk(_roleGraphMutex);

        for (RoleNameIterator it = _roleGraph.getRolesForDatabase(dbname);
                it.more(); it.next()) {
            if (!showBuiltinRoles && _roleGraph.isBuiltinRole(it.get())) {
                continue;
            }
            BSONObj roleDoc;
            Status status = _getRoleDescription_inlock(it.get(), showPrivileges, &roleDoc);
            if (!status.isOK()) {
                return status;
            }
            result->push_back(roleDoc);
        }
        return Status::OK();
    }

namespace {

    /**
     * Adds the role described in "doc" to "roleGraph".  If the role cannot be added, due to
     * some error in "doc", logs a warning.
     */
    void addRoleFromDocumentOrWarn(RoleGraph* roleGraph, const BSONObj& doc) {
        Status status = roleGraph->addRoleFromDocument(doc);
        if (!status.isOK()) {
            warning() << "Skipping invalid role document.  " << status << "; document " << doc;
        }
    }


}  // namespace

    Status AuthzManagerExternalStateLocal::_initializeRoleGraph() {
        boost::lock_guard<boost::mutex> lkInitialzeRoleGraph(_roleGraphMutex);

        _roleGraphState = roleGraphStateInitial;
        _roleGraph = RoleGraph();

        RoleGraph newRoleGraph;
        Status status = query(
                AuthorizationManager::rolesCollectionNamespace,
                BSONObj(),
                BSONObj(),
                boost::bind(addRoleFromDocumentOrWarn, &newRoleGraph, _1));
        if (!status.isOK())
            return status;

        status = newRoleGraph.recomputePrivilegeData();

        RoleGraphState newState;
        if (status == ErrorCodes::GraphContainsCycle) {
            error() << "Inconsistent role graph during authorization manager intialization.  Only "
                "direct privileges available. " << status.reason();
            newState = roleGraphStateHasCycle;
            status = Status::OK();
        }
        else if (status.isOK()) {
            newState = roleGraphStateConsistent;
        }
        else {
            newState = roleGraphStateInitial;
        }

        if (status.isOK()) {
            _roleGraph.swap(newRoleGraph);
            _roleGraphState = newState;
        }
        return status;
    }

    void AuthzManagerExternalStateLocal::logOp(
            const char* op,
            const char* ns,
            const BSONObj& o,
            BSONObj* o2,
            bool* b) {

        if (ns == AuthorizationManager::rolesCollectionNamespace.ns() ||
            ns == AuthorizationManager::adminCommandNamespace.ns()) {

            boost::lock_guard<boost::mutex> lk(_roleGraphMutex);
            Status status = _roleGraph.handleLogOp(op, NamespaceString(ns), o, o2);

            if (status == ErrorCodes::OplogOperationUnsupported) {
                _roleGraph = RoleGraph();
                _roleGraphState = roleGraphStateInitial;
                error() << "Unsupported modification to roles collection in oplog; "
                    "TODO how to remedy. " << status << " Oplog entry: " << op;
            }
            else if (!status.isOK()) {
                warning() << "Skipping bad update to roles collection in oplog. " << status <<
                    " Oplog entry: " << op;
            }
            status = _roleGraph.recomputePrivilegeData();
            if (status == ErrorCodes::GraphContainsCycle) {
                _roleGraphState = roleGraphStateHasCycle;
                error() << "Inconsistent role graph during authorization manager intialization.  "
                    "Only direct privileges available. " << status.reason() <<
                    " after applying oplog entry " << op;
            }
            else {
                fassert(17183, status);
                _roleGraphState = roleGraphStateConsistent;
            }
        }
    }

}  // namespace mongo
