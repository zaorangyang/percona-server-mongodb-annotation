// kv_heap_recovery_unit.cpp

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

#include "mongo/base/checked_cast.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/storage/kv_heap/kv_heap_dictionary.h"
#include "mongo/db/storage/kv_heap/kv_heap_recovery_unit.h"

namespace mongo {

    void KVHeapRecoveryUnit::commitUnitOfWork() {
        for (std::vector< std::shared_ptr<Change> >::iterator it = _ops.begin();
             it != _ops.end(); ++it) {
            Change *op = it->get();
            op->commit();
        }
        _ops.clear();
    }

    void KVHeapRecoveryUnit::commitAndRestart() {
        commitUnitOfWork();
    }

    void KVHeapRecoveryUnit::endUnitOfWork() {
        for (std::vector< std::shared_ptr<Change> >::reverse_iterator it = _ops.rbegin();
             it != _ops.rend(); ++it) {
            Change *op = it->get();
            op->rollback();
        }
        _ops.clear();
    }

    void KVHeapRecoveryUnit::registerChange(Change* change) {
        _ops.push_back(std::shared_ptr<Change>(change));
    }

    KVHeapRecoveryUnit* KVHeapRecoveryUnit::getKVHeapRecoveryUnit(OperationContext* opCtx) {
        return checked_cast<KVHeapRecoveryUnit*>(opCtx->recoveryUnit());
    }

    void InsertOperation::rollback() {
        if (_wasDeleted) {
            _dict->rollbackInsertByDeleting(_key);
        } else {
            _dict->rollbackInsertToOldValue(_key, _value);
        }
    }

    void DeleteOperation::rollback() {
        _dict->rollbackDelete(_key, _value);
    }

} // namespace mongo
