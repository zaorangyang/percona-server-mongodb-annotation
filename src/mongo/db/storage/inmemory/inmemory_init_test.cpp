/*======
This file is part of Percona Server for MongoDB.

Copyright (c) 2006, 2016, Percona and/or its affiliates. All rights reserved.

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

#include "mongo/platform/basic.h"


#include "mongo/db/service_context.h"
#include "mongo/db/json.h"
#include "mongo/db/storage/storage_engine_metadata.h"
#include "mongo/db/storage/wiredtiger/wiredtiger_global_options.h"
#include "mongo/db/storage/storage_options.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/mongoutils/str.h"

namespace {

using namespace mongo;

const std::string kInMemoryEngineName = "inMemory";

class InMemoryFactoryTest : public mongo::unittest::Test {
private:
    virtual void setUp() {
        ServiceContext* globalEnv = getGlobalServiceContext();
        ASSERT_TRUE(globalEnv);
        ASSERT_TRUE(getGlobalServiceContext()->isRegisteredStorageEngine(kInMemoryEngineName));
        std::unique_ptr<StorageFactoriesIterator> sfi(
            getGlobalServiceContext()->makeStorageFactoriesIterator());
        ASSERT_TRUE(sfi);
        bool found = false;
        while (sfi->more()) {
            const StorageEngine::Factory* currentFactory = sfi->next();
            if (currentFactory->getCanonicalName() == kInMemoryEngineName) {
                found = true;
                factory = currentFactory;
                break;
            }
            found = true;
        }
        ASSERT_TRUE(found);
        _oldOptions = wiredTigerGlobalOptions;
    }

    virtual void tearDown() {
        wiredTigerGlobalOptions = _oldOptions;
        factory = NULL;
    }

    WiredTigerGlobalOptions _oldOptions;

protected:
    const StorageEngine::Factory* factory;
};

void _testValidateMetadata(const StorageEngine::Factory* factory,
                           const BSONObj& metadataOptions,
                           bool directoryPerDB,
                           bool directoryForIndexes,
                           ErrorCodes::Error expectedCode) {
    // It is fine to specify an invalid data directory for the metadata
    // as long as we do not invoke read() or write().
    StorageEngineMetadata metadata("no_such_directory");
    metadata.setStorageEngineOptions(metadataOptions);

    StorageGlobalParams storageOptions;
    storageOptions.directoryperdb = directoryPerDB;
    wiredTigerGlobalOptions.directoryForIndexes = directoryForIndexes;

    Status status = factory->validateMetadata(metadata, storageOptions);
    if (expectedCode != status.code()) {
        FAIL(str::stream()
             << "Unexpected StorageEngine::Factory::validateMetadata result. Expected: "
             << ErrorCodes::errorString(expectedCode) << " but got " << status.toString()
             << " instead. metadataOptions: " << metadataOptions << "; directoryPerDB: "
             << directoryPerDB << "; directoryForIndexes: " << directoryForIndexes);
    }
}

// Do not validate fields that are not present in metadata.
TEST_F(InMemoryFactoryTest, ValidateMetadataEmptyOptions) {
    _testValidateMetadata(factory, BSONObj(), false, false, ErrorCodes::OK);
    _testValidateMetadata(factory, BSONObj(), false, true, ErrorCodes::OK);
    _testValidateMetadata(factory, BSONObj(), true, false, ErrorCodes::OK);
    _testValidateMetadata(factory, BSONObj(), false, false, ErrorCodes::OK);
}

TEST_F(InMemoryFactoryTest, ValidateMetadataDirectoryPerDB) {
    _testValidateMetadata(
        factory, fromjson("{directoryPerDB: 123}"), false, false, ErrorCodes::FailedToParse);
    _testValidateMetadata(
        factory, fromjson("{directoryPerDB: false}"), false, false, ErrorCodes::OK);
    _testValidateMetadata(
        factory, fromjson("{directoryPerDB: false}"), true, false, ErrorCodes::InvalidOptions);
    _testValidateMetadata(
        factory, fromjson("{directoryPerDB: true}"), false, false, ErrorCodes::InvalidOptions);
    _testValidateMetadata(factory, fromjson("{directoryPerDB: true}"), true, false, ErrorCodes::OK);
}

TEST_F(InMemoryFactoryTest, ValidateMetadataDirectoryForIndexes) {
    _testValidateMetadata(
        factory, fromjson("{directoryForIndexes: 123}"), false, false, ErrorCodes::FailedToParse);
    _testValidateMetadata(
        factory, fromjson("{directoryForIndexes: false}"), false, false, ErrorCodes::OK);
    _testValidateMetadata(
        factory, fromjson("{directoryForIndexes: false}"), false, true, ErrorCodes::InvalidOptions);
    _testValidateMetadata(
        factory, fromjson("{directoryForIndexes: true}"), false, false, ErrorCodes::InvalidOptions);
    _testValidateMetadata(
        factory, fromjson("{directoryForIndexes: true}"), true, true, ErrorCodes::OK);
}

void _testCreateMetadataOptions(const StorageEngine::Factory* factory,
                                bool directoryPerDB,
                                bool directoryForIndexes) {
    StorageGlobalParams storageOptions;
    storageOptions.directoryperdb = directoryPerDB;
    wiredTigerGlobalOptions.directoryForIndexes = directoryForIndexes;

    BSONObj metadataOptions = factory->createMetadataOptions(storageOptions);

    BSONElement directoryPerDBElement = metadataOptions.getField("directoryPerDB");
    ASSERT_TRUE(directoryPerDBElement.isBoolean());
    ASSERT_EQUALS(directoryPerDB, directoryPerDBElement.boolean());

    BSONElement directoryForIndexesElement = metadataOptions.getField("directoryForIndexes");
    ASSERT_TRUE(directoryForIndexesElement.isBoolean());
    ASSERT_EQUALS(directoryForIndexes, directoryForIndexesElement.boolean());
}

TEST_F(InMemoryFactoryTest, CreateMetadataOptions) {
    _testCreateMetadataOptions(factory, false, false);
    _testCreateMetadataOptions(factory, false, true);
    _testCreateMetadataOptions(factory, true, false);
    _testCreateMetadataOptions(factory, true, true);
}

}  // namespace
