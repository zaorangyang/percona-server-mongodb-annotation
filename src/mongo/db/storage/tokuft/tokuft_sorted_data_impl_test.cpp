// tokuft_sorted_data_impl_test.cpp

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

#include <boost/filesystem/operations.hpp>

#include "mongo/db/storage/kv/kv_engine_test_harness.h"
#include "mongo/db/storage/kv/dictionary/kv_engine_impl.h"
#include "mongo/db/storage/kv/dictionary/kv_sorted_data_impl.h"
#include "mongo/db/storage/sorted_data_interface_test_harness.h"
#include "mongo/db/storage/tokuft/tokuft_recovery_unit.h"
#include "mongo/db/storage/tokuft/tokuft_engine_test.h"
#include "mongo/unittest/unittest.h"
#include "mongo/unittest/temp_dir.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

    class TokuFTSortedDataImplHarness : public HarnessHelper {
    public:
	TokuFTSortedDataImplHarness() :
            _kvHarness(createTokuFTEngineHarnessHelper()),
            _engine(_kvHarness->getKVEngine()),
            _seq(0) {
        }

        virtual ~TokuFTSortedDataImplHarness() { }

	virtual std::unique_ptr<SortedDataInterface> newSortedDataInterface(bool unique) {
            std::unique_ptr<OperationContext> opCtx(newOperationContext());
            const std::string ident = mongoutils::str::stream() << "PerconaFTSortedDataInterface-" << _seq++;
            Status status = _engine->createSortedDataInterface(opCtx.get(), ident, NULL);
            invariant(status.isOK());
            KVSortedDataImpl *p = _engine->getKVSortedDataImpl(opCtx.get(), ident, NULL);
            if (unique) {
                p->DuplicatesAreNotAllowed();
            } else {
                p->DuplicatesAreAllowed();
            }

            return std::unique_ptr<SortedDataInterface>(p);
	}

	virtual std::unique_ptr<RecoveryUnit> newRecoveryUnit() {
	    return std::unique_ptr<RecoveryUnit>(_engine->newRecoveryUnit());
	}

    private:
        std::auto_ptr<TokuFTEngineHarnessHelper> _kvHarness;
        KVEngineImpl *_engine;
        int _seq;
    };

    std::unique_ptr<HarnessHelper> newHarnessHelper() {
        return std::unique_ptr<HarnessHelper>(new TokuFTSortedDataImplHarness());
    }

} // namespace mongo
