// tokuft_capped_delete_range_optimizer.h

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

#pragma once

#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

#include <ftcxx/db.hpp>

#include "mongo/db/record_id.h"
#include "mongo/db/storage/key_string.h"
#include "mongo/db/storage/kv/slice.h"

namespace mongo {

    /**
     * Capped collections delete from the back in batches (see KVRecordStoreCapped::deleteAsNeeded),
     * and then notify the KVDictionary that a batch has been deleted and can be optimized.
     * TokuFTCappedDeleteRangeOptimizer manages, for a specific record store, a background thread
     * which will optimize old ranges of deleted data in the background.  We apply backpressure when
     * the optimizer thread gets too far behind the deleted data.
     */
    class TokuFTCappedDeleteRangeOptimizer {
    public:
        TokuFTCappedDeleteRangeOptimizer(const ftcxx::DB &db);

        ~TokuFTCappedDeleteRangeOptimizer();

        /**
         * Notifies the thread that new data has been deleted beyond max, and so everything before
         * max is eligible for optimization.  Also notes the size and number of documents deleted in
         * the current batch (which will be eligible for optimization later).
         *
         * On restart, we forget about whatever deletes were not yet optimized, but since we always
         * optimize from negative infinity, those things will get optimized in the first pass
         * anyway.
         */
        void updateMaxDeleted(const RecordId &max, int64_t sizeSaved, int64_t docsRemoved);

        void run();

        bool running() const;

    private:
        static const KeyString kNegativeInfinity;
        int _magic;

        const ftcxx::DB &_db;
        RecordId _max;

        // The most recently deleted range is not optimizable.  Once we see more deletes, we
        // consider that amount optimizable again.
        int64_t _unoptimizableSize;
        int64_t _optimizableSize;

        bool _running;
        bool _terminated;

        boost::thread _thread;

        mutable boost::mutex _mutex;
        boost::condition_variable _updateCond;
        boost::condition_variable _backpressureCond;
    };

}
