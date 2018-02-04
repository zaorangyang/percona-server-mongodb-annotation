/**
 *    Copyright (C) 2014 MongoDB Inc.
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

#pragma once

#include <string>

#include "mongo/db/catalog/catalog_raii.h"
#include "mongo/db/stats/top.h"
#include "mongo/util/timer.h"

namespace mongo {

/**
 * RAII-style class which automatically tracks the operation namespace in CurrentOp and records the
 * operation via Top upon destruction.
 */
class AutoStatsTracker {
    MONGO_DISALLOW_COPYING(AutoStatsTracker);

public:
    /**
     * Sets the namespace of the CurOp object associated with 'opCtx' to be 'nss' and starts the
     * CurOp timer. 'lockType' describes which type of lock is held by this operation, and will be
     * used for reporting via Top. If 'dbProfilingLevel' is not given, this constructor will acquire
     * and then drop a database lock in order to determine the database's profiling level.
     */
    AutoStatsTracker(OperationContext* opCtx,
                     const NamespaceString& nss,
                     Top::LockType lockType,
                     boost::optional<int> dbProfilingLevel,
                     Date_t deadline = Date_t::max());

    /**
     * Records stats about the current operation via Top.
     */
    ~AutoStatsTracker();

private:
    OperationContext* _opCtx;
    Top::LockType _lockType;
};

/**
 * Same as calling AutoGetCollection with MODE_IS, but in addition ensures that the read will be
 * performed against an appropriately committed snapshot if the operation is using a readConcern of
 * 'majority'.
 *
 * Use this when you want to read the contents of a collection, but you are not at the top-level of
 * some command. This will ensure your reads obey any requested readConcern, but will not update the
 * status of CurrentOp, or add a Top entry.
 *
 * NOTE: Must not be used with any locks held, because it needs to block waiting on the committed
 * snapshot to become available.
 */
class AutoGetCollectionForRead {
    MONGO_DISALLOW_COPYING(AutoGetCollectionForRead);

public:
    AutoGetCollectionForRead(
        OperationContext* opCtx,
        const NamespaceStringOrUUID& nsOrUUID,
        AutoGetCollection::ViewMode viewMode = AutoGetCollection::ViewMode::kViewsForbidden,
        Date_t deadline = Date_t::max());

    Database* getDb() const {
        return _autoColl->getDb();
    }

    Collection* getCollection() const {
        return _autoColl->getCollection();
    }

    ViewDefinition* getView() const {
        return _autoColl->getView();
    }

    const NamespaceString& getNss() const {
        return _autoColl->getNss();
    }

private:
    // This field is optional, because the code to wait for majority committed snapshot needs to
    // release locks in order to block waiting
    boost::optional<AutoGetCollection> _autoColl;
};

/**
 * Same as AutoGetCollectionForRead, but in addition will add a Top entry upon destruction and
 * ensure the CurrentOp object has the right namespace and has started its timer.
 */
class AutoGetCollectionForReadCommand {
    MONGO_DISALLOW_COPYING(AutoGetCollectionForReadCommand);

public:
    AutoGetCollectionForReadCommand(
        OperationContext* opCtx,
        const NamespaceStringOrUUID& nsOrUUID,
        AutoGetCollection::ViewMode viewMode = AutoGetCollection::ViewMode::kViewsForbidden,
        Date_t deadline = Date_t::max());

    Database* getDb() const {
        return _autoCollForRead.getDb();
    }

    Collection* getCollection() const {
        return _autoCollForRead.getCollection();
    }

    ViewDefinition* getView() const {
        return _autoCollForRead.getView();
    }

    const NamespaceString& getNss() const {
        return _autoCollForRead.getNss();
    }

private:
    AutoGetCollectionForRead _autoCollForRead;
    AutoStatsTracker _statsTracker;
};

/**
 * Opens the database that we want to use and sets the appropriate namespace on the
 * current operation.
 */
class OldClientContext {
    MONGO_DISALLOW_COPYING(OldClientContext);

public:
    OldClientContext(OperationContext* opCtx, const std::string& ns, bool doVersion = true);
    ~OldClientContext();

    Database* db() const {
        return _db;
    }

    /** @return if the db was created by this OldClientContext */
    bool justCreated() const {
        return _justCreated;
    }

    /**
     * Only used by the OldClientWriteContext class below and internally, do not use in any new
     * code.
     */
    OldClientContext(OperationContext* opCtx,
                     const std::string& ns,
                     bool doVersion,
                     Database* db,
                     bool justCreated);

private:
    friend class CurOp;

    const Timer _timer;

    OperationContext* const _opCtx;

    Database* _db;
    bool _justCreated;
};

/**
 * Combines AutoGetOrCreateDb and OldClientContext. If the requested 'ns' exists, the constructed
 * object will have both the database and the collection locked in MODE_IX. Otherwise, the database
 * will be locked in MODE_X and will be created (note, only the database will be created, but not
 * the collection).
 *
 * TODO: Based on its usages, this class should become AutoGetOrCreateCollection whereby the
 * requested collection should be automatically created instead of relying on the callers to perform
 * a check and create it afterwards.
 */
class OldClientWriteContext {
    MONGO_DISALLOW_COPYING(OldClientWriteContext);

public:
    OldClientWriteContext(OperationContext* opCtx, StringData ns);

    Database* db() const {
        return _clientContext->db();
    }

    Collection* getCollection() const {
        return db()->getCollection(_opCtx, _nss);
    }

private:
    OperationContext* const _opCtx;
    const NamespaceString _nss;

    boost::optional<AutoGetOrCreateDb> _autoCreateDb;
    boost::optional<Lock::CollectionLock> _collLock;
    boost::optional<OldClientContext> _clientContext;
};

/**
 * Returns a MODE_IX LockMode if a read is performed under readConcern level snapshot, or a MODE_IS
 * lock otherwise. MODE_IX acquisition will allow a read to participate in two-phase locking.
 */
LockMode getLockModeForQuery(OperationContext* opCtx);

}  // namespace mongo
