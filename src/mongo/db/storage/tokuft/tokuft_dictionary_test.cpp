// kv_dictionary_test_harness.h

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

#include "mongo/db/storage/kv/kv_engine_test_harness.h"
#include "mongo/db/storage/kv/dictionary/kv_dictionary_test_harness.h"
#include "mongo/db/storage/tokuft/tokuft_dictionary.h"
#include "mongo/db/storage/tokuft/tokuft_engine.h"

namespace mongo {

    class TokuFTDictionaryHarnessHelper : public HarnessHelper {
    public:
	TokuFTDictionaryHarnessHelper() :
            _kvHarness(KVHarnessHelper::create()),
            _engine(dynamic_cast<TokuFTEngine *>(_kvHarness->getEngine())),
            _seq(0) {
            invariant(_engine != NULL);
        }

        virtual ~TokuFTDictionaryHarnessHelper() { }

	virtual KVDictionary* newKVDictionary() {
            std::auto_ptr<OperationContext> opCtx(new OperationContextNoop(newRecoveryUnit()));

            const std::string ident = mongoutils::str::stream() << "TokuFTDictionary-" << _seq++;
            Status status = _engine->createKVDictionary(opCtx.get(), ident, KVDictionary::Encoding(), BSONObj());
            invariant(status.isOK());

	    return _engine->getKVDictionary(opCtx.get(), ident, KVDictionary::Encoding(), BSONObj());
	}

	virtual RecoveryUnit* newRecoveryUnit() {
	    return _engine->newRecoveryUnit();
	}

    private:
        std::auto_ptr<KVHarnessHelper> _kvHarness;
        TokuFTEngine *_engine;
        int _seq;
    };

    HarnessHelper* newHarnessHelper() {
        return new TokuFTDictionaryHarnessHelper();
    }
}
