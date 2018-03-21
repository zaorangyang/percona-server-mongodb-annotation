/**
 *    Copyright (C) 2018 MongoDB Inc.
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

#include "mongo/db/operation_context.h"

namespace mongo {

/**
 * The WriteUnitOfWork is an RAII type that begins a storage engine write unit of work on both the
 * Locker and the RecoveryUnit of the OperationContext. Any writes that occur during the lifetime of
 * this object will be committed when commit() is called, and rolled back (aborted) when the object
 * is destructed without a call to commit() or release().
 *
 * A WriteUnitOfWork can be nested with others, but only the top level WriteUnitOfWork will commit
 * the unit of work on the RecoveryUnit. If a low level WriteUnitOfWork aborts, any parents will
 * also abort.
 */
class WriteUnitOfWork {
    MONGO_DISALLOW_COPYING(WriteUnitOfWork);

public:
    WriteUnitOfWork() = default;

    WriteUnitOfWork(OperationContext* opCtx);

    ~WriteUnitOfWork();

    /**
     * Creates a top-level WriteUnitOfWork without changing RecoveryUnit or Locker state. For use
     * when the RecoveryUnit and Locker are already in an active state.
     */
    static std::unique_ptr<WriteUnitOfWork> createForSnapshotResume(OperationContext* opCtx);

    /**
     * Releases the OperationContext RecoveryUnit and Locker objects from management without
     * changing state. Allows for use of these objects beyond the WriteUnitOfWork lifespan. Prepared
     * units of work are not allowed be released.
     */
    void release();

    /**
     * Transitions the WriteUnitOfWork to the "prepared" state. The RecoveryUnit state in the
     * OperationContext must be active. The WriteUnitOfWork may not be nested and will invariant in
     * that case. Will throw CommandNotSupported if the storage engine does not support prepared
     * transactions. May throw WriteConflictException.
     *
     * No subsequent operations are allowed except for commit or abort (when the object is
     * destructed).
     */
    void prepare();

    /**
     * Commits the WriteUnitOfWork. If this is the top level unit of work, the RecoveryUnit's unit
     * of work is committed. Commit can only be called once on an active unit of work, and may not
     * be called on a released WriteUnitOfWork.
     */
    void commit();

private:
    OperationContext* _opCtx;

    bool _toplevel;

    bool _committed = false;
    bool _prepared = false;
    bool _released = false;
};

}  // namespace mongo
