/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:

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

#ifdef PERCONA_AUDIT_ENABLED

#include "mongo/client/undef_macros.h"

#include <cstdio>
#include <iostream>
#include <string>

#include <boost/filesystem/path.hpp>
#include <boost/scoped_ptr.hpp>

#include "mongo/util/debug_util.h"
#include "mongo/client/redef_macros.h"

/*
 * See this link for explanation of the MONGO_LOG macro:
 * http://www.mongodb.org/about/contributors/reference/server-logging-rules/
 */
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/base/init.h"
#include "mongo/bson/bson_field.h"
#include "mongo/db/audit.h"
#include "mongo/db/auth/authorization_session.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/client_basic.h"
#include "mongo/db/commands.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/matcher/matcher.h"
#include "mongo/db/namespace_string.h"
#include "mongo/logger/auditlog.h"
#include "mongo/util/concurrency/mutex.h"
#include "mongo/util/exit_code.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/net/sock.h"
#include "mongo/util/paths.h"
#include "mongo/util/time_support.h"

#include "audit_options.h"
#include "audit_file.h"

#define PERCONA_AUDIT_STUB {}

namespace mongo {

namespace audit {

    NOINLINE_DECL void realexit( ExitCode rc ) {
#ifdef _COVERAGE
        // Need to make sure coverage data is properly flushed before exit.
        // It appears that ::_exit() does not do this.
        log() << "calling regular ::exit() so coverage data may flush..." << std::endl;
        ::exit( rc );
#else
        ::_exit( rc );
#endif
    }

    // Writable interface for audit events
    class WritableAuditLog : public logger::AuditLog {
    public:
        virtual ~WritableAuditLog() {}
        virtual void append(const BSONObj &obj) = 0;
        virtual void rotate() = 0;
    };

    // Writes audit events to a json file
    class JSONAuditLog : public WritableAuditLog {
        bool ioErrorShouldRetry(int errcode) {
            return (errcode == EAGAIN ||
                    errcode == EWOULDBLOCK ||
                    errcode == EINTR);
        }

    public:
        JSONAuditLog(const std::string &file, const BSONObj &filter)
            : _file(new AuditFile), 
              _matcher(filter.getOwned()), 
              _fileName(file),
              _mutex("auditFileMutex") {
            _file->open(file.c_str(), false, false);
        }

        virtual void append(const BSONObj &obj) {
            if (_matcher.matches(obj)) {
                const std::string str = mongoutils::str::stream() << obj.jsonString() << "\n";

                // mongo::File does not have an "atomic append" operation.
                // As such, with a rwlock we are vulnerable to a race
                // where we get the length of the file, then try to pwrite
                // at that offset.  If another write beats us to pwrite,
                // we'll overwrite that audit data when our write goes
                // through.
                //
                // Somewhere, we need a mutex around grabbing the file
                // offset and trying to write to it (even if this were in
                // the kernel, the synchronization is still there).  This
                // is a good enough place as any.
                //
                // We don't need the mutex around fsync, except to protect against concurrent
                // logRotate destroying our pointer.  Welp.
                SimpleMutex::scoped_lock lck(_mutex);

                // If pwrite performs a partial write, we don't want to
                // muck about figuring out how much it did write (hard to
                // get out of the File abstraction) and then carefully
                // writing the rest.  Easier to calculate the position
                // first, then repeatedly write to that position if we
                // have to retry.
                fileofs pos = _file->len();

                int writeRet;
                for (int retries = 10; retries > 0; --retries) {
                    writeRet = _file->writeReturningError(pos, str.c_str(), str.size());
                    if (writeRet == 0) {
                        break;
                    } else if (!ioErrorShouldRetry(writeRet)) {
                        error() << "Audit system cannot write event " << obj.jsonString() << " to log file " << _fileName << std::endl;
                        error() << "Write failed with fatal error " << errnoWithDescription(writeRet) << std::endl;
                        error() << "As audit cannot make progress, the server will now shut down." << std::endl;
                        realexit(EXIT_AUDIT_ERROR);
                    }
                    warning() << "Audit system cannot write event " << obj.jsonString() << " to log file " << _fileName << std::endl;
                    warning() << "Write failed with retryable error " << errnoWithDescription(writeRet) << std::endl;
                    warning() << "Audit system will retry this write another " << retries - 1 << " times." << std::endl;
                    if (retries <= 7 && retries > 0) {
                        sleepmillis(1 << ((7 - retries) * 2));
                    }
                }

                if (writeRet != 0) {
                    error() << "Audit system cannot write event " << obj.jsonString() << " to log file " << _fileName << std::endl;
                    error() << "Write failed with fatal error " << errnoWithDescription(writeRet) << std::endl;
                    error() << "As audit cannot make progress, the server will now shut down." << std::endl;
                    realexit(EXIT_AUDIT_ERROR);
                }

                int fsyncRet;
                for (int retries = 10; retries > 0; --retries) {
                    fsyncRet = _file->fsyncReturningError();
                    if (fsyncRet == 0) {
                        break;
                    } else if (!ioErrorShouldRetry(fsyncRet)) {
                        error() << "Audit system cannot fsync event " << obj.jsonString() << " to log file " << _fileName << std::endl;
                        error() << "Fsync failed with fatal error " << errnoWithDescription(fsyncRet) << std::endl;
                        error() << "As audit cannot make progress, the server will now shut down." << std::endl;
                        realexit(EXIT_AUDIT_ERROR);
                    }
                    warning() << "Audit system cannot fsync event " << obj.jsonString() << " to log file " << _fileName << std::endl;
                    warning() << "Fsync failed with retryable error " << errnoWithDescription(fsyncRet) << std::endl;
                    warning() << "Audit system will retry this fsync another " << retries - 1 << " times." << std::endl;
                    if (retries <= 7 && retries > 0) {
                        sleepmillis(1 << ((7 - retries) * 2));
                    }
                }

                if (fsyncRet != 0) {
                    error() << "Audit system cannot fsync event " << obj.jsonString() << " to log file " << _fileName << std::endl;
                    error() << "Fsync failed with fatal error " << errnoWithDescription(fsyncRet) << std::endl;
                    error() << "As audit cannot make progress, the server will now shut down." << std::endl;
                    realexit(EXIT_AUDIT_ERROR);
                }
            }
        }

        virtual void rotate() {
            SimpleMutex::scoped_lock lck(_mutex);

            // Close the current file.
            _file.reset();

            // Rename the current file
            // Note: we append a timestamp to the file name.
            std::stringstream ss;
            ss << _fileName << "." << terseCurrentTime(false);
            std::string s = ss.str();
            int r = std::rename(_fileName.c_str(), s.c_str());
            if (r != 0) {
                error() << "Could not rotate audit log, but continuing normally "
                        << "(error desc: " << errnoWithDescription() << ")"
                        << std::endl;
            }

            // Open a new file, with the same name as the original.
            _file.reset(new AuditFile);
            _file->open(_fileName.c_str(), false, false);
        }

    private:
        boost::scoped_ptr<AuditFile> _file;
        const Matcher _matcher;
        const std::string _fileName;
        SimpleMutex _mutex;
    };    

    // A void audit log does not actually write any audit events. Instead, it
    // verifies that we can call jsonString() on the generatd bson obj and that
    // the result is non-empty. This is useful for sanity testing the audit bson
    // generation code even when auditing is not explicitly enabled in debug builds.
    class VoidAuditLog : public WritableAuditLog {
    public:
        void append(const BSONObj &obj) {
            verify(!obj.jsonString().empty());
        }

        void rotate() { }
    };

    static std::shared_ptr<WritableAuditLog> _auditLog;

    static void _setGlobalAuditLog(WritableAuditLog *log) {
        _auditLog.reset(log);

        // Sets the audit log in the general logging framework which
        // will rotate() the audit log when the server log rotates.
        setAuditLog(log);
    }

    static bool _auditEnabledOnCommandLine() {
        return auditOptions.destination != "";
    }

    Status initialize() {
        if (!_auditEnabledOnCommandLine()) {
            // Write audit events into the void for debug builds, so we get
            // coverage on the code that generates audit log objects.
            DEV {
                log() << "Initializing dev null audit..." << std::endl;
                _setGlobalAuditLog(new VoidAuditLog());
            }
            return Status::OK();
        }

        log() << "Initializing audit..." << std::endl;
        const BSONObj filter = fromjson(auditOptions.filter);
        _setGlobalAuditLog(new JSONAuditLog(auditOptions.path, filter));
        return Status::OK();
    }

    MONGO_INITIALIZER_WITH_PREREQUISITES(AuditInit, ("SetGlobalEnvironment"))
                                         (InitializerContext *context)
    {
        return initialize();
    }

///////////////////////// audit.h functions ////////////////////////////
    
    namespace AuditFields {
        // Common fields
        BSONField<StringData> type("atype");
        BSONField<BSONObj> timestamp("ts");
        BSONField<BSONObj> local("local");
        BSONField<BSONObj> remote("remote");
        BSONField<BSONObj> params("params");
        BSONField<int> result("result");
    }

    // This exists because NamespaceString::toString() prints "admin."
    // when dbname == "admin" and coll == "", which isn't so great.
    static std::string nssToString(const NamespaceString &nss) {
        std::stringstream ss;
        if (!nss.db().empty()) {
            ss << nss.db();
        }

        if (!nss.coll().empty()) {
            ss << '.' << nss.coll();
        }

        return ss.str();
    }

    static void appendCommonInfo(BSONObjBuilder &builder,
                                 const StringData &atype,
                                 ClientBasic* client) {
        builder << AuditFields::type(atype);
        builder << AuditFields::timestamp(BSON("$date" << static_cast<long long>(jsTime().millis)));
        builder << AuditFields::local(BSON("host" << getHostNameCached() << "port" << serverGlobalParams.port));
        if (client->hasRemote()) {
            const HostAndPort hp = client->getRemote();
            builder << AuditFields::remote(BSON("host" << hp.host() << "port" << hp.port()));
        } else {
            // It's not 100% clear that an empty obj here actually makes sense..
            builder << AuditFields::remote(BSONObj());
        }
        if (client->hasAuthorizationSession()) {
            // Build the users array, which consists of (user, db) pairs
            AuthorizationSession *session = client->getAuthorizationSession();
            BSONArrayBuilder users(builder.subarrayStart("users"));
            for (UserNameIterator it = session->getAuthenticatedUserNames(); it.more(); it.next()) {
                BSONObjBuilder user(users.subobjStart());
                user.append("user", it->getUser());
                user.append("db", it->getDB());
                user.doneFast();
            }
            users.doneFast();
        } else {
            // It's not 100% clear that an empty obj here actually makes sense..
            builder << "users" << BSONObj();
        }
    }

    static void appendPrivileges(BSONObjBuilder &builder, const PrivilegeVector& privileges) {
        BSONArrayBuilder privbuilder(builder.subarrayStart("privileges"));
        for (PrivilegeVector::const_iterator it = privileges.begin(); it != privileges.end(); ++it) {
            privbuilder.append(it->toBSON());
        }
        privbuilder.doneFast();
    }

    static void appendRoles(BSONObjBuilder &builder, const std::vector<RoleName>& roles) {
        BSONArrayBuilder rolebuilder(builder.subarrayStart("roles"));
        for (std::vector<RoleName>::const_iterator it = roles.begin(); it != roles.end(); ++it) {
            BSONObjBuilder r(rolebuilder.subobjStart());
            r.append("role", it->getRole());
            r.append("db", it->getDB());
            r.doneFast();
        }
        rolebuilder.doneFast();
    }


    static void _auditEvent(ClientBasic* client,
                            const StringData& atype,
                            const BSONObj& params,
                            ErrorCodes::Error result = ErrorCodes::OK) {
        BSONObjBuilder builder;
        appendCommonInfo(builder, atype, client);
        builder << AuditFields::params(params);
        builder << AuditFields::result(static_cast<int>(result));
        _auditLog->append(builder.done());
    }

    static void _auditAuthzFailure(ClientBasic* client,
                                 const StringData& ns,
                                 const StringData& command,
                                 const BSONObj& args,
                                 ErrorCodes::Error result) {
        const BSONObj params = !ns.empty() ?
            BSON("command" << command << "ns" << ns << "args" << args) :
            BSON("command" << command << "args" << args);
        _auditEvent(client, "authCheck", params, result);
    }

    void logAuthentication(ClientBasic* client,
                           const StringData& mechanism,
                           const UserName& user,
                           ErrorCodes::Error result) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("user" << user.getUser() <<
                                    "db" << user.getDB() <<
                                    "mechanism" << mechanism);
        _auditEvent(client, "authenticate", params, result);
    }

    void logCommandAuthzCheck(ClientBasic* client,
                              const std::string& dbname,
                              const BSONObj& cmdObj,
                              Command* command,
                              ErrorCodes::Error result) {
        if (!_auditLog) {
            return;
        }

        if (result != ErrorCodes::OK) {
            _auditAuthzFailure(client, command->parseNs(dbname, cmdObj), cmdObj.firstElement().fieldName(), cmdObj, result);
        }
    }


    void logDeleteAuthzCheck(
            ClientBasic* client,
            const NamespaceString& ns,
            const BSONObj& pattern,
            ErrorCodes::Error result) {
        if (!_auditLog) {
            return;
        }

        if (result != ErrorCodes::OK) {
            _auditAuthzFailure(client, nssToString(ns), "delete", BSON("pattern" << pattern), result);
        } else if (ns.coll() == "system.users") {
            _auditEvent(client, "dropUser", BSON("db" << ns.db() << "pattern" << pattern));
        }
    }

    void logFsyncUnlockAuthzCheck(
            ClientBasic* client,
            ErrorCodes::Error result) {
        if (!_auditLog) {
            return;
        }

        if (result != ErrorCodes::OK) {
            _auditAuthzFailure(client, "", "fsyncUnlock", BSONObj(), result);
        }
    }

    void logGetMoreAuthzCheck(
            ClientBasic* client,
            const NamespaceString& ns,
            long long cursorId,
            ErrorCodes::Error result) {
        if (!_auditLog) {
            return;
        }

        if (result != ErrorCodes::OK) {
            _auditAuthzFailure(client, nssToString(ns), "getMore", BSON("cursorId" << cursorId), result);
        }
    }

    void logInProgAuthzCheck(
            ClientBasic* client,
            const BSONObj& filter,
            ErrorCodes::Error result) {
        if (!_auditLog) {
            return;
        }

        if (result != ErrorCodes::OK) {
            _auditAuthzFailure(client, "", "inProg", BSON("filter" << filter), result);
        }
    }

    void logInsertAuthzCheck(
            ClientBasic* client,
            const NamespaceString& ns,
            const BSONObj& insertedObj,
            ErrorCodes::Error result) {
        if (!_auditLog) {
            return;
        }

        if (result != ErrorCodes::OK) {
            _auditAuthzFailure(client, nssToString(ns), "insert", BSON("obj" << insertedObj), result);
        } else if (ns.coll() == "system.users") {
            _auditEvent(client, "createUser", BSON("db" << ns.db() << "userObj" << insertedObj));
        }
    }

    void logKillCursorsAuthzCheck(
            ClientBasic* client,
            const NamespaceString& ns,
            long long cursorId,
            ErrorCodes::Error result) {
        if (!_auditLog) {
            return;
        }

        if (result != ErrorCodes::OK) {
            _auditAuthzFailure(client, nssToString(ns), "killCursors", BSON("cursorId" << cursorId), result);
        }
    }

    void logKillOpAuthzCheck(
            ClientBasic* client,
            const BSONObj& filter,
            ErrorCodes::Error result) {
        if (!_auditLog) {
            return;
        }

        if (result != ErrorCodes::OK) {
            _auditAuthzFailure(client, "", "killOp", BSON("filter" << filter), result);
        }
    }

    void logQueryAuthzCheck(
            ClientBasic* client,
            const NamespaceString& ns,
            const BSONObj& query,
            ErrorCodes::Error result) {
        if (!_auditLog) {
            return;
        }

        if (result != ErrorCodes::OK) {
            _auditAuthzFailure(client, nssToString(ns), "query", BSON("query" << query), result);
        }
    }

    void logUpdateAuthzCheck(
            ClientBasic* client,
            const NamespaceString& ns,
            const BSONObj& query,
            const BSONObj& updateObj,
            bool isUpsert,
            bool isMulti,
            ErrorCodes::Error result) {
        if (!_auditLog) {
            return;
        }

        if (result != ErrorCodes::OK) {
            const BSONObj args = BSON("pattern" << query <<
                                      "updateObj" << updateObj <<
                                      "upsert" << isUpsert <<
                                      "multi" << isMulti); 
            _auditAuthzFailure(client, nssToString(ns), "update", args, result);
        } else if (ns.coll() == "system.users") {
            const BSONObj params = BSON("db" << ns.db() <<
                                        "pattern" << query <<
                                        "updateObj" << updateObj <<
                                        "upsert" << isUpsert <<
                                        "multi" << isMulti); 
            _auditEvent(client, "updateUser", params);
        }
    }

    void logReplSetReconfig(ClientBasic* client,
                            const BSONObj* oldConfig,
                            const BSONObj* newConfig) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("old" << *oldConfig << "new" << *newConfig);
        _auditEvent(client, "replSetReconfig", params);
    }

    void logApplicationMessage(ClientBasic* client,
                               const StringData& msg) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("msg" << msg);
        _auditEvent(client, "applicationMessage", params);
    }

    void logShutdown(ClientBasic* client) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSONObj();
        _auditEvent(client, "shutdown", params);
    }

    void logCreateIndex(ClientBasic* client,
                        const BSONObj* indexSpec,
                        const StringData& indexname,
                        const StringData& nsname) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("ns" << nsname <<
                                    "indexName" << indexname <<
                                    "indexSpec" << *indexSpec);
        _auditEvent(client, "createIndex", params);
    }

    void logCreateCollection(ClientBasic* client,
                             const StringData& nsname) { 
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("ns" << nsname);
        _auditEvent(client, "createCollection", params);
    }

    void logCreateDatabase(ClientBasic* client,
                           const StringData& nsname) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("ns" << nsname);
        _auditEvent(client, "createDatabase", params);
    }

    void logDropIndex(ClientBasic* client,
                      const StringData& indexname,
                      const StringData& nsname) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("ns" << nsname << "indexName" << indexname);
        _auditEvent(client, "dropIndex", params);
    }

    void logDropCollection(ClientBasic* client,
                           const StringData& nsname) { 
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("ns" << nsname);
        _auditEvent(client, "dropCollection", params);
    }

    void logDropDatabase(ClientBasic* client,
                         const StringData& nsname) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("ns" << nsname);
        _auditEvent(client, "dropDatabase", params);
    }

    void logRenameCollection(ClientBasic* client,
                             const StringData& source,
                             const StringData& target) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("old" << source << "new" << target);
        _auditEvent(client, "renameCollection", params);
    }

    void logEnableSharding(ClientBasic* client,
                           const StringData& nsname) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("ns" << nsname);
        _auditEvent(client, "enableSharding", params);
    }

    void logAddShard(ClientBasic* client,
                     const StringData& name,
                     const std::string& servers,
                     long long maxsize) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params= BSON("shard" << name <<
                                   "connectionString" << servers <<
                                   "maxSize" << maxsize);
        _auditEvent(client, "addShard", params);
    }

    void logRemoveShard(ClientBasic* client,
                        const StringData& shardname) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("shard" << shardname);
        _auditEvent(client, "removeShard", params);
    }

    void logShardCollection(ClientBasic* client,
                            const StringData& ns,
                            const BSONObj& keyPattern,
                            bool unique) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("ns" << ns <<
                                    "key" << keyPattern <<
                                    "options" << BSON("unique" << unique));
        _auditEvent(client, "shardCollection", params);
    }

    void logCreateUser(ClientBasic* client,
                       const UserName& username,
                       bool password,
                       const BSONObj* customData,
                       const std::vector<RoleName>& roles) {
        if (!_auditLog) {
            return;
        }

        BSONObjBuilder params;
        params << "user" << username.getUser()
               << "db" << username.getDB() 
               << "password" << password 
               << "customData" << (customData ? *customData : BSONObj());
        appendRoles(params, roles);
        _auditEvent(client, "createUser", params.done());
    }

    void logDropUser(ClientBasic* client,
                     const UserName& username) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("user" << username.getUser() <<
                                    "db" << username.getDB());
        _auditEvent(client, "dropUser", params);
    }

    void logDropAllUsersFromDatabase(ClientBasic* client,
                                     const StringData& dbname) {
        if (!_auditLog) {
            return;
        }

        _auditEvent(client, "dropAllUsers", BSON("db" << dbname));
    }

    void logUpdateUser(ClientBasic* client,
                       const UserName& username,
                       bool password,
                       const BSONObj* customData,
                       const std::vector<RoleName>* roles) {
        if (!_auditLog) {
            return;
        }

        BSONObjBuilder params;
        params << "user" << username.getUser()
               << "db" << username.getDB() 
               << "password" << password 
               << "customData" << (customData ? *customData : BSONObj());
        if (roles) {
            appendRoles(params, *roles);
        }

        _auditEvent(client, "updateUser", params.done());
    }

    void logGrantRolesToUser(ClientBasic* client,
                             const UserName& username,
                             const std::vector<RoleName>& roles) {
        if (!_auditLog) {
            return;
        }

        BSONObjBuilder params;
        params << "user" << username.getUser()
               << "db" << username.getDB();
        appendRoles(params, roles);
        _auditEvent(client, "grantRolesToUser", params.done());
    }

    void logRevokeRolesFromUser(ClientBasic* client,
                                const UserName& username,
                                const std::vector<RoleName>& roles) {
        if (!_auditLog) {
            return;
        }

        BSONObjBuilder params;
        params << "user" << username.getUser()
               << "db" << username.getDB();
        appendRoles(params, roles);
        _auditEvent(client, "revokeRolesFromUser", params.done());
    }

    void logCreateRole(ClientBasic* client,
                       const RoleName& role,
                       const std::vector<RoleName>& roles,
                       const PrivilegeVector& privileges) {
        if (!_auditLog) {
            return;
        }

        BSONObjBuilder params;
        params << "role" << role.getRole()
               << "db" << role.getDB();
        appendRoles(params, roles);
        appendPrivileges(params, privileges);
        _auditEvent(client, "createRole", params.done());
    }

    void logUpdateRole(ClientBasic* client,
                       const RoleName& role,
                       const std::vector<RoleName>* roles,
                       const PrivilegeVector* privileges) {
        if (!_auditLog) {
            return;
        }

        BSONObjBuilder params;
        params << "role" << role.getRole()
               << "db" << role.getDB();
        appendRoles(params, *roles);
        appendPrivileges(params, *privileges);
        _auditEvent(client, "updateRole", params.done());
    }

    void logDropRole(ClientBasic* client,
                     const RoleName& role) {
        if (!_auditLog) {
            return;
        }

        const BSONObj params = BSON("role" << role.getRole() <<
                                    "db" << role.getDB());
        _auditEvent(client, "dropRole", params);
    }

    void logDropAllRolesFromDatabase(ClientBasic* client,
                                     const StringData& dbname) {
        if (!_auditLog) {
            return;
        }

        _auditEvent(client, "dropAllRoles", BSON("db" << dbname));
    }

    void logGrantRolesToRole(ClientBasic* client,
                             const RoleName& role,
                             const std::vector<RoleName>& roles) {
        if (!_auditLog) {
            return;
        }

        BSONObjBuilder params;
        params << "role" << role.getRole()
               << "db" << role.getDB();
        appendRoles(params, roles);
        _auditEvent(client, "grantRolesToRole", params.done());
    }

    void logRevokeRolesFromRole(ClientBasic* client,
                                const RoleName& role,
                                const std::vector<RoleName>& roles) {
        if (!_auditLog) {
            return;
        }

        BSONObjBuilder params;
        params << "role" << role.getRole()
               << "db" << role.getDB();
        appendRoles(params, roles);
        _auditEvent(client, "revokeRolesFromRole", params.done());
    }

    void logGrantPrivilegesToRole(ClientBasic* client,
                                  const RoleName& role,
                                  const PrivilegeVector& privileges) {
        if (!_auditLog) {
            return;
        }

        BSONObjBuilder params;
        params << "role" << role.getRole()
               << "db" << role.getDB();
        appendPrivileges(params, privileges);
        _auditEvent(client, "grantPrivilegesToRole", params.done());
    }

    void logRevokePrivilegesFromRole(ClientBasic* client,
                                     const RoleName& role,
                                     const PrivilegeVector& privileges) {
        if (!_auditLog) {
            return;
        }

        BSONObjBuilder params;
        params << "role" << role.getRole()
               << "db" << role.getDB();
        appendPrivileges(params, privileges);
        _auditEvent(client, "revokePrivilegesFromRole", params.done());
    }

    void appendImpersonatedUsers(BSONObjBuilder* cmd) PERCONA_AUDIT_STUB

    void parseAndRemoveImpersonatedUsersField(
            BSONObj cmdObj,
            AuthorizationSession* authSession,
            std::vector<UserName>* parsedUserNames,
            bool* fieldIsPresent) PERCONA_AUDIT_STUB

    void parseAndRemoveImpersonatedRolesField(
            BSONObj cmdObj,
            AuthorizationSession* authSession,
            std::vector<RoleName>* parsedRoleNames,
            bool* fieldIsPresent) PERCONA_AUDIT_STUB

}  // namespace audit
}  // namespace mongo

#endif  // PERCONA_AUDIT_ENABLED
