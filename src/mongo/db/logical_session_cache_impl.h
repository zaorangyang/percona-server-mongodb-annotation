/**
 * Copyright (C) 2017 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#pragma once

#include "mongo/db/logical_session_cache.h"
#include "mongo/db/logical_session_id.h"
#include "mongo/db/refresh_sessions_gen.h"
#include "mongo/db/service_liason.h"
#include "mongo/db/sessions_collection.h"
#include "mongo/db/time_proof_service.h"
#include "mongo/platform/atomic_word.h"
#include "mongo/stdx/thread.h"
#include "mongo/util/lru_cache.h"

namespace mongo {

class Client;
class OperationContext;
class ServiceContext;

extern int logicalSessionRecordCacheSize;
extern int logicalSessionRefreshMinutes;

/**
 * A thread-safe cache structure for logical session records.
 *
 * The cache takes ownership of the passed-in ServiceLiason and
 * SessionsCollection helper types.
 */
class LogicalSessionCacheImpl final : public LogicalSessionCache {
public:
    static constexpr int kLogicalSessionCacheDefaultCapacity = 10000;
    static constexpr Minutes kLogicalSessionDefaultRefresh = Minutes(5);

    /**
     * An Options type to support the LogicalSessionCacheImpl.
     */
    struct Options {
        Options(){};

        /**
         * The number of session records to keep in the cache.
         *
         * May be set with --setParameter logicalSessionRecordCacheSize=X.
         */
        int capacity = logicalSessionRecordCacheSize;

        /**
         * A timeout value to use for sessions in the cache, in minutes.
         *
         * By default, this is set to 30 minutes.
         *
         * May be set with --setParameter localLogicalSessionTimeoutMinutes=X.
         */
        Minutes sessionTimeout = Minutes(localLogicalSessionTimeoutMinutes);

        /**
         * The interval over which the cache will refresh session records.
         *
         * By default, this is set to every 5 minutes. If the caller is
         * setting the sessionTimeout by hand, it is suggested that they
         * consider also setting the refresh interval accordingly.
         *
         * May be set with --setParameter logicalSessionRefreshMinutes=X.
         */
        Minutes refreshInterval = Minutes(logicalSessionRefreshMinutes);
    };

    /**
     * Construct a new session cache.
     */
    explicit LogicalSessionCacheImpl(std::unique_ptr<ServiceLiason> service,
                                     std::unique_ptr<SessionsCollection> collection,
                                     Options options = Options{});

    LogicalSessionCacheImpl(const LogicalSessionCacheImpl&) = delete;
    LogicalSessionCacheImpl& operator=(const LogicalSessionCacheImpl&) = delete;

    ~LogicalSessionCacheImpl();

    Status promote(LogicalSessionId lsid) override;

    Status startSession(OperationContext* opCtx, LogicalSessionRecord record) override;

    Status refreshSessions(OperationContext* opCtx,
                           const RefreshSessionsCmdFromClient& cmd) override;
    Status refreshSessions(OperationContext* opCtx,
                           const RefreshSessionsCmdFromClusterMember& cmd) override;

    void vivify(OperationContext* opCtx, const LogicalSessionId& lsid) override;

    void clear() override;

    Status refreshNow(Client* client) override;

    Date_t now() override;

    size_t size() override;

private:
    /**
     * Internal methods to handle scheduling and perform refreshes for active
     * session records contained within the cache.
     */
    void _periodicRefresh(Client* client);
    Status _refresh(Client* client);

    /**
     * Returns true if a record has passed its given expiration.
     */
    bool _isDead(const LogicalSessionRecord& record, Date_t now) const;

    /**
     * Takes the lock and inserts the given record into the cache.
     */
    boost::optional<LogicalSessionRecord> _addToCache(LogicalSessionRecord record);

    const Minutes _refreshInterval;
    const Minutes _sessionTimeout;

    std::unique_ptr<ServiceLiason> _service;
    std::unique_ptr<SessionsCollection> _sessionsColl;

    stdx::mutex _cacheMutex;
    LRUCache<LogicalSessionId, LogicalSessionRecord, LogicalSessionIdHash> _cache;
};

}  // namespace mongo
