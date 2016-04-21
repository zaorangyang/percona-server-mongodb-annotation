/**
 *    Copyright (C) 2015 10gen Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include <cstdint>

#include "mongo/db/catalog/collection.h"
#include "mongo/db/catalog/index_catalog.h"
#include "mongo/db/catalog/index_create.h"
#include "mongo/db/client.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/dbhelpers.h"
#include "mongo/db/index/index_descriptor.h"
#include "mongo/db/service_context.h"
#include "mongo/db/service_context_d.h"
#include "mongo/dbtests/dbtests.h"

namespace ValidateTests {

using std::unique_ptr;

static const char* const _ns = "unittests.validate_tests";

/**
 * Test fixture for a write locked test using collection _ns.  Includes functionality to
 * partially construct a new IndexDetails in a manner that supports proper cleanup in
 * dropCollection().
 */
class ValidateBase {
public:
    explicit ValidateBase(bool full) : _ctx(&_txn, _ns), _client(&_txn), _full(full) {
        _client.createCollection(_ns);
    }
    ~ValidateBase() {
        _client.dropCollection(_ns);
        getGlobalServiceContext()->unsetKillAllOperations();
    }
    Collection* collection() {
        return _ctx.getCollection();
    }

protected:
    bool checkValid() {
        ValidateResults results;
        BSONObjBuilder output;
        ASSERT_OK(collection()->validate(&_txn, _full, false, &results, &output));

        //  Check if errors are reported if and only if valid is set to false.
        ASSERT_EQ(results.valid, results.errors.empty());

        if (_full) {
            BSONObj outputObj = output.done();
            bool allIndexesValid = true;
            for (auto elem : outputObj["indexDetails"].Obj()) {
                BSONObj indexDetail(elem.value());
                allIndexesValid = indexDetail["valid"].boolean() ? allIndexesValid : false;
            }
            ASSERT_EQ(results.valid, allIndexesValid);
        }

        return results.valid;
    }

    const ServiceContext::UniqueOperationContext _txnPtr = cc().makeOperationContext();
    OperationContext& _txn = *_txnPtr;
    OldClientWriteContext _ctx;
    DBDirectClient _client;
    bool _full;
};

template <bool full>
class ValidateIdIndexCount : public ValidateBase {
public:
    ValidateIdIndexCount() : ValidateBase(full) {}

    void run() {
        // Create a new collection, insert records {_id: 1} and {_id: 2} and check it's valid.
        Database* db = _ctx.db();
        Collection* coll;
        RecordId id1;
        {
            OpDebug* const nullOpDebug = nullptr;
            WriteUnitOfWork wunit(&_txn);
            ASSERT_OK(db->dropCollection(&_txn, _ns));
            coll = db->createCollection(&_txn, _ns);

            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 1), nullOpDebug, true));
            id1 = coll->getCursor(&_txn)->next()->id;
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 2), nullOpDebug, true));
            wunit.commit();
        }

        ASSERT_TRUE(checkValid());

        RecordStore* rs = coll->getRecordStore();

        // Remove {_id: 1} from the record store, so we get more _id entries than records.
        {
            WriteUnitOfWork wunit(&_txn);
            rs->deleteRecord(&_txn, id1);
            wunit.commit();
        }

        ASSERT_FALSE(checkValid());

        // Insert records {_id: 0} and {_id: 1} , so we get too few _id entries, and verify
        // validate fails.
        {
            WriteUnitOfWork wunit(&_txn);
            for (int j = 0; j < 2; j++) {
                auto doc = BSON("_id" << j);
                ASSERT_OK(
                    rs->insertRecord(&_txn, doc.objdata(), doc.objsize(), /*enforceQuota*/ false));
            }
            wunit.commit();
        }

        ASSERT_FALSE(checkValid());
    }
};

template <bool full>
class ValidateSecondaryIndexCount : public ValidateBase {
public:
    ValidateSecondaryIndexCount() : ValidateBase(full) {}
    void run() {
        // Create a new collection, insert two documents.
        Database* db = _ctx.db();
        Collection* coll;
        RecordId id1;
        {
            OpDebug* const nullOpDebug = nullptr;
            WriteUnitOfWork wunit(&_txn);
            ASSERT_OK(db->dropCollection(&_txn, _ns));
            coll = db->createCollection(&_txn, _ns);
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 1 << "a" << 1), nullOpDebug, true));
            id1 = coll->getCursor(&_txn)->next()->id;
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 2 << "a" << 2), nullOpDebug, true));
            wunit.commit();
        }

        auto status =
            dbtests::createIndexFromSpec(&_txn,
                                         coll->ns().ns(),
                                         BSON("name"
                                              << "a"
                                              << "ns" << coll->ns().ns() << "key" << BSON("a" << 1)
                                              << "background" << false));

        ASSERT_OK(status);
        ASSERT_TRUE(checkValid());

        RecordStore* rs = coll->getRecordStore();

        // Remove a record, so we get more _id entries than records, and verify validate fails.
        {
            WriteUnitOfWork wunit(&_txn);
            rs->deleteRecord(&_txn, id1);
            wunit.commit();
        }

        ASSERT_FALSE(checkValid());

        // Insert two more records, so we get too few entries for a non-sparse index, and
        // verify validate fails.
        {
            WriteUnitOfWork wunit(&_txn);
            for (int j = 0; j < 2; j++) {
                auto doc = BSON("_id" << j);
                ASSERT_OK(
                    rs->insertRecord(&_txn, doc.objdata(), doc.objsize(), /*enforceQuota*/ false));
            }
            wunit.commit();
        }

        ASSERT_FALSE(checkValid());
    }
};

class ValidateSecondaryIndex : public ValidateBase {
public:
    ValidateSecondaryIndex() : ValidateBase(true) {}
    void run() {
        // Create a new collection, insert three records.
        Database* db = _ctx.db();
        Collection* coll;
        RecordId id1;
        {
            WriteUnitOfWork wunit(&_txn);
            ASSERT_OK(db->dropCollection(&_txn, _ns));
            coll = db->createCollection(&_txn, _ns);
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 1 << "a" << 1), true));
            id1 = coll->getCursor(&_txn)->next()->id;
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 2 << "a" << 2), true));
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 3 << "b" << 3), true));
            wunit.commit();
        }

        auto status =
            dbtests::createIndexFromSpec(&_txn,
                                         coll->ns().ns(),
                                         BSON("name"
                                              << "a"
                                              << "ns" << coll->ns().ns() << "key" << BSON("a" << 1)
                                              << "background" << false));

        ASSERT_OK(status);
        ASSERT_TRUE(checkValid());

        RecordStore* rs = coll->getRecordStore();

        // Update {a: 1} to {a: 9} without updating the index, so we get inconsistent values
        // between the index and the document. Verify validate fails.
        {
            WriteUnitOfWork wunit(&_txn);
            auto doc = BSON("_id" << 1 << "a" << 9);
            auto statusW = rs->updateRecord(
                &_txn, id1, doc.objdata(), doc.objsize(), /*enforceQuota*/ false, NULL);
            ASSERT_OK(statusW.getStatus());
            // Assert the RecordId has not changed after an in-place update.
            ASSERT_EQ(id1, statusW.getValue());
            wunit.commit();
        }

        ASSERT_FALSE(checkValid());
    }
};

class ValidateIdIndex : public ValidateBase {
public:
    ValidateIdIndex() : ValidateBase(true) {}

    void run() {
        // Create a new collection, insert records {_id: 1} and {_id: 2} and check it's valid.
        Database* db = _ctx.db();
        Collection* coll;
        RecordId id1;
        {
            WriteUnitOfWork wunit(&_txn);
            ASSERT_OK(db->dropCollection(&_txn, _ns));
            coll = db->createCollection(&_txn, _ns);

            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 1), true));
            id1 = coll->getCursor(&_txn)->next()->id;
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 2), true));
            wunit.commit();
        }

        ASSERT_TRUE(checkValid());

        RecordStore* rs = coll->getRecordStore();

        // Update {_id: 1} to {_id: 9} without updating the index, so we get inconsistent values
        // between the index and the document. Verify validate fails.
        {
            WriteUnitOfWork wunit(&_txn);
            auto doc = BSON("_id" << 9);
            auto statusW = rs->updateRecord(
                &_txn, id1, doc.objdata(), doc.objsize(), /*enforceQuota*/ false, NULL);
            ASSERT_OK(statusW.getStatus());
            // Assert the RecordId has not changed after an in-place update.
            ASSERT_EQ(id1, statusW.getValue());
            wunit.commit();
        }

        ASSERT_FALSE(checkValid());

        // Revert {_id: 9} to {_id: 1} and verify that validate succeeds.
        {
            WriteUnitOfWork wunit(&_txn);
            auto doc = BSON("_id" << 1);
            auto statusW = rs->updateRecord(
                &_txn, id1, doc.objdata(), doc.objsize(), /*enforceQuota*/ false, NULL);
            ASSERT_OK(statusW.getStatus());
            id1 = statusW.getValue();
            wunit.commit();
        }

        ASSERT_TRUE(checkValid());

        // Remove the {_id: 1} document and insert a new document without an index entry, so there
        // will still be the same number of index entries and documents, but one document will not
        // have an index entry.
        {
            WriteUnitOfWork wunit(&_txn);
            rs->deleteRecord(&_txn, id1);
            auto doc = BSON("_id" << 3);
            ASSERT_OK(rs->insertRecord(&_txn, doc.objdata(), doc.objsize(), /*enforceQuota*/ false)
                          .getStatus());
            wunit.commit();
        }

        ASSERT_FALSE(checkValid());
    }
};

class ValidateMultiKeyIndex : public ValidateBase {
public:
    ValidateMultiKeyIndex() : ValidateBase(true) {}

    void run() {
        // Create a new collection, insert three records and check it's valid.
        Database* db = _ctx.db();
        Collection* coll;
        RecordId id1;
        // {a: [b: 1, c: 2]}, {a: [b: 2, c: 2]}, {a: [b: 1, c: 1]}
        auto doc1 = BSON("_id" << 1 << "a" << BSON_ARRAY(BSON("b" << 1) << BSON("c" << 2)));
        auto doc1_b = BSON("_id" << 1 << "a" << BSON_ARRAY(BSON("b" << 2) << BSON("c" << 2)));
        auto doc1_c = BSON("_id" << 1 << "a" << BSON_ARRAY(BSON("b" << 1) << BSON("c" << 1)));

        // {a: [b: 2]}
        auto doc2 = BSON("_id" << 2 << "a" << BSON_ARRAY(BSON("b" << 2)));
        // {a: [c: 1]}
        auto doc3 = BSON("_id" << 3 << "a" << BSON_ARRAY(BSON("c" << 1)));
        {
            WriteUnitOfWork wunit(&_txn);
            ASSERT_OK(db->dropCollection(&_txn, _ns));
            coll = db->createCollection(&_txn, _ns);


            ASSERT_OK(coll->insertDocument(&_txn, doc1, true));
            id1 = coll->getCursor(&_txn)->next()->id;
            ASSERT_OK(coll->insertDocument(&_txn, doc2, true));
            ASSERT_OK(coll->insertDocument(&_txn, doc3, true));
            wunit.commit();
        }

        ASSERT_TRUE(checkValid());

        // Create multi-key index.
        auto status =
            dbtests::createIndexFromSpec(&_txn,
                                         coll->ns().ns(),
                                         BSON("name"
                                              << "multikey_index"
                                              << "ns" << coll->ns().ns() << "key"
                                              << BSON("a.b" << 1) << "background" << false));

        ASSERT_OK(status);
        ASSERT_TRUE(checkValid());

        RecordStore* rs = coll->getRecordStore();

        // Update a document's indexed field without updating the index.
        {
            WriteUnitOfWork wunit(&_txn);
            auto statusW = rs->updateRecord(
                &_txn, id1, doc1_b.objdata(), doc1_b.objsize(), /*enforceQuota*/ false, NULL);
            ASSERT_OK(statusW.getStatus());
            id1 = statusW.getValue();
            wunit.commit();
        }

        ASSERT_FALSE(checkValid());

        // Update a document's non-indexed field without updating the index.
        // Index validation should still be valid.
        {
            WriteUnitOfWork wunit(&_txn);
            auto statusW = rs->updateRecord(
                &_txn, id1, doc1_c.objdata(), doc1_c.objsize(), /*enforceQuota*/ false, NULL);
            ASSERT_OK(statusW.getStatus());
            wunit.commit();
        }

        ASSERT_TRUE(checkValid());
    }
};

class ValidateSparseIndex : public ValidateBase {
public:
    ValidateSparseIndex() : ValidateBase(true) {}

    void run() {
        // Create a new collection, insert three records and check it's valid.
        Database* db = _ctx.db();
        Collection* coll;
        RecordId id1;
        {
            WriteUnitOfWork wunit(&_txn);
            ASSERT_OK(db->dropCollection(&_txn, _ns));
            coll = db->createCollection(&_txn, _ns);

            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 1 << "a" << 1), true));
            id1 = coll->getCursor(&_txn)->next()->id;
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 2 << "a" << 2), true));
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 3 << "b" << 1), true));
            wunit.commit();
        }

        // Create a sparse index.
        auto status =
            dbtests::createIndexFromSpec(&_txn,
                                         coll->ns().ns(),
                                         BSON("name"
                                              << "sparse_index"
                                              << "ns" << coll->ns().ns() << "key" << BSON("a" << 1)
                                              << "background" << false << "sparse" << true));

        ASSERT_OK(status);
        ASSERT_TRUE(checkValid());

        RecordStore* rs = coll->getRecordStore();

        // Update a document's indexed field without updating the index.
        {
            WriteUnitOfWork wunit(&_txn);
            auto doc = BSON("_id" << 2 << "a" << 3);
            auto statusW = rs->updateRecord(
                &_txn, id1, doc.objdata(), doc.objsize(), /*enforceQuota*/ false, NULL);
            ASSERT_OK(statusW.getStatus());
            wunit.commit();
        }

        ASSERT_FALSE(checkValid());
    }
};

class ValidatePartialIndex : public ValidateBase {
public:
    ValidatePartialIndex() : ValidateBase(true) {}

    void run() {
        // Create a new collection, insert two records and check it's valid.
        Database* db = _ctx.db();
        Collection* coll;
        RecordId id1;
        {
            WriteUnitOfWork wunit(&_txn);
            ASSERT_OK(db->dropCollection(&_txn, _ns));
            coll = db->createCollection(&_txn, _ns);

            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 1 << "a" << 1), true));
            id1 = coll->getCursor(&_txn)->next()->id;
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 2 << "a" << 2), true));
            wunit.commit();
        }

        // Create a partial index.
        auto status =
            dbtests::createIndexFromSpec(&_txn,
                                         coll->ns().ns(),
                                         BSON("name"
                                              << "partial_index"
                                              << "ns" << coll->ns().ns() << "key" << BSON("a" << 1)
                                              << "background" << false << "partialFilterExpression"
                                              << BSON("a" << BSON("$gt" << 1))));

        ASSERT_OK(status);
        ASSERT_TRUE(checkValid());

        RecordStore* rs = coll->getRecordStore();

        // Update an unindexed document without updating the index.
        {
            WriteUnitOfWork wunit(&_txn);
            auto doc = BSON("_id" << 1);
            auto statusW = rs->updateRecord(
                &_txn, id1, doc.objdata(), doc.objsize(), /*enforceQuota*/ false, NULL);
            ASSERT_OK(statusW.getStatus());
            wunit.commit();
        }

        ASSERT_TRUE(checkValid());
    }
};

class ValidateCompoundIndex : public ValidateBase {
public:
    ValidateCompoundIndex() : ValidateBase(true) {}

    void run() {
        // Create a new collection, insert five records and check it's valid.
        Database* db = _ctx.db();
        Collection* coll;
        RecordId id1;
        {
            WriteUnitOfWork wunit(&_txn);
            ASSERT_OK(db->dropCollection(&_txn, _ns));
            coll = db->createCollection(&_txn, _ns);

            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 1 << "a" << 1 << "b" << 4), true));
            id1 = coll->getCursor(&_txn)->next()->id;
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 2 << "a" << 2 << "b" << 5), true));
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 3 << "a" << 3), true));
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 4 << "b" << 6), true));
            ASSERT_OK(coll->insertDocument(&_txn, BSON("_id" << 5 << "c" << 7), true));
            wunit.commit();
        }

        // Create two compound indexes, one forward and one reverse, to test
        // validate()'s index direction parsing.
        auto status = dbtests::createIndexFromSpec(&_txn,
                                                   coll->ns().ns(),
                                                   BSON("name"
                                                        << "compound_index_1"
                                                        << "ns" << coll->ns().ns() << "key"
                                                        << BSON("a" << 1 << "b" << -1)
                                                        << "background" << false));
        ASSERT_OK(status);

        status = dbtests::createIndexFromSpec(&_txn,
                                              coll->ns().ns(),
                                              BSON("name"
                                                   << "compound_index_2"
                                                   << "ns" << coll->ns().ns() << "key"
                                                   << BSON("a" << -1 << "b" << 1) << "background"
                                                   << false));

        ASSERT_OK(status);
        ASSERT_TRUE(checkValid());

        RecordStore* rs = coll->getRecordStore();

        // Update a document's indexed field without updating the index.
        {
            WriteUnitOfWork wunit(&_txn);
            auto doc = BSON("_id" << 1 << "a" << 1 << "b" << 3);
            auto statusW = rs->updateRecord(
                &_txn, id1, doc.objdata(), doc.objsize(), /*enforceQuota*/ false, NULL);
            ASSERT_OK(statusW.getStatus());
            wunit.commit();
        }

        ASSERT_FALSE(checkValid());
    }
};

class ValidateTests : public Suite {
public:
    ValidateTests() : Suite("validate_tests") {}

    void setupTests() {
        // Add tests for both full validate and non-full validate.
        add<ValidateIdIndexCount<true>>();
        add<ValidateIdIndexCount<false>>();
        add<ValidateSecondaryIndexCount<true>>();
        add<ValidateSecondaryIndexCount<false>>();

        // These tests are only needed for full validate.
        add<ValidateIdIndex>();
        add<ValidateSecondaryIndex>();
        add<ValidateMultiKeyIndex>();
        add<ValidateSparseIndex>();
        add<ValidateCompoundIndex>();
        add<ValidatePartialIndex>();
    }
} validateTests;
}  // namespace ValidateTests
