// kv_recovery_unit.h

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

#include "mongo/db/record_id.h"
#include "mongo/db/storage/recovery_unit.h"

namespace mongo {

    /**
     * The KVDictionary layer takes care of capped id management (visible_id_tracker.h), which
     * requires a few hooks in to the RecoveryUnit.  You only need to inherit from this if your
     * engine supports doc locking (if it doesn't, hasSnapshot is sort of nonsensical).
     */
    class KVRecoveryUnit : public RecoveryUnit {

        /**
         * The lowest invisible RecordId for this transaction.  Used by multiple RecordIterators
         * once one is established.
         */
        RecordId _lowestInvisible;

    public:
        ~KVRecoveryUnit() {}

        /**
         * Check whether the recovery unit has a snapshot.  Used by the capped iterator to check
         * whether to set the lowest invisible id.
         */
        virtual bool hasSnapshot() const = 0;

        void setLowestInvisible(const RecordId& id) {
            _lowestInvisible = id;
        }

        const RecordId &getLowestInvisible() const {
            return _lowestInvisible;
        }

        /**
         * A KVRecoveryUnit may be asked to create a new RecoveryUnit
         * suitable for its engine (for capped deletes, see
         * KVRecordStoreCapped::deleteAsNeeded).
         */
        virtual KVRecoveryUnit *newRecoveryUnit() const = 0;
    };

} // namespace mongo
