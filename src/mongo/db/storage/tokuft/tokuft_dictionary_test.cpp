// kv_dictionary_test_harness.h

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
            auto_ptr<OperationContext> opCtx(new OperationContextNoop(newRecoveryUnit()));

            const string ident = mongoutils::str::stream() << "TokuFTDictionary-" << _seq++;
            Status status = _engine->createKVDictionary(opCtx.get(), ident, KVDictionary::Comparator::useMemcmp());
            invariant(status.isOK());

	    return _engine->getKVDictionary(opCtx.get(), ident, KVDictionary::Comparator::useMemcmp());
	}

	virtual RecoveryUnit* newRecoveryUnit() {
	    return _engine->newRecoveryUnit();
	}

    private:
        auto_ptr<KVHarnessHelper> _kvHarness;
        TokuFTEngine *_engine;
        int _seq;
    };

    HarnessHelper* newHarnessHelper() {
        return new TokuFTDictionaryHarnessHelper();
    }
}
