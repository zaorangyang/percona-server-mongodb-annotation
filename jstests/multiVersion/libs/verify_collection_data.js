// This file contains test helpers to manage and validate collection state.  This is useful for
// round trip testing of entire collections.
//
// There are three stages represented in this file:
// 1.  Data generation - CollectionDataGenerator class.  This contains helpers to generate test data
//                       for a collection.
// 2.  Data persistence - createCollectionWithData function.  This class takes a
//                        CollectionDataGenerator and inserts the generated data into the given
//                        collection.
// 3.  Data validation - CollectionDataValidator class.  This class contains functions for saving
//                       the state of a collection and comparing a collection's state to the
//                       previously saved state.
//
// Common use case:
// 1.  Create a CollectionDataGenerator
// 2.  Save collection data using the createCollectionWithData function
// 3.  Record collection state in an instance of the CollectionDataValidator class
// 4.  Do round trip or other testing
// 5.  Validate that collection has not changed using the CollectionDataValidator class

load('./jstests/multiVersion/libs/data_generators.js');

// Function to actually add the data generated by the given dataGenerator to a collection
createCollectionWithData = function(db, collectionName, dataGenerator) {

    // Drop collection if exists
    // TODO: add ability to control this
    db.getCollection(collectionName).drop();

    print("db.createCollection(\"" + collectionName + "\", " +
          JSON.stringify(dataGenerator.collectionMetadata.get()) + ");");
    assert.eq(db.createCollection(collectionName, dataGenerator.collectionMetadata.get()).ok, 1);

    var collection = db.getCollection(collectionName);

    var numIndexes = 0;
    while (dataGenerator.indexes.hasNext()) {
        var nextIndex = dataGenerator.indexes.next();
        print("collection.ensureIndex(" + JSON.stringify(nextIndex.spec) + ", " +
              JSON.stringify(nextIndex.options) + ");");
        var ensureIndexResult = collection.ensureIndex(nextIndex.spec, nextIndex.options);
        assert.commandWorked(ensureIndexResult);
        numIndexes++;
    }

    // Make sure we actually added all the indexes we think we added.  +1 for the _id index.
    assert.eq(collection.getIndexes().length, numIndexes + 1);

    var numInserted = 0;
    while (dataGenerator.data.hasNext()) {
        var nextDoc = dataGenerator.data.next();
        // Use _id as our ordering field just so we don't have to deal with sorting.  This only
        // matters here since we can use indexes
        nextDoc._id = numInserted;
        print("collection.insert(" + JSON.stringify(nextDoc) + ");");
        var insertResult = collection.insert(nextDoc);
        assert(db.getLastError() == null);
        numInserted++;
    }

    assert.eq(collection.find().count(), numInserted, "counts not equal after inserts");

    return db.getCollection(collectionName);
};

// MongoDB 3.4 introduces new fields into the listCollections result document. This function
// injects expected 3.4 values into a 3.2 document to allow for object comparison.
// TODO: Remove this check post-3.4 release.
var injectExpected34FieldsIf32 = function(collectionInfo, dbVersion) {
    if (dbVersion.startsWith("3.2")) {
        return Object.extend({type: "collection", info: {readOnly: false}}, collectionInfo);
    }

    return collectionInfo;
};

// Class to save the state of a collection and later compare the current state of a collection to
// the saved state
function CollectionDataValidator() {
    var _initialized = false;
    var _collectionInfo = {};
    var _indexData = [];
    var _collectionData = [];
    var _dbVersion = "";

    // Returns the options of the specified collection.
    this.getCollectionInfo = function(collection) {
        var infoObj = collection.getDB().getCollectionInfos({name: collection.getName()});
        assert.eq(1, infoObj.length, "expected collection '" + collection.getName() + "'to exist");
        return infoObj[0];
    };

    // Saves the current state of the collection passed in
    this.recordCollectionData = function(collection) {
        // Save the metadata for this collection for later comparison.
        _collectionInfo = this.getCollectionInfo(collection);

        // Save the indexes for this collection for later comparison
        _indexData = collection.getIndexes().sort(function(a, b) {
            if (a.name > b.name)
                return 1;
            else
                return -1;
        });

        // Save the data for this collection for later comparison
        _collectionData = collection.find().sort({"_id": 1}).toArray();

        _dbVersion = collection.getDB().version();

        _initialized = true;

        return collection;
    };

    this.validateCollectionData = function(
        collection, dbVersionForCollection, options = {indexSpecFieldsToSkip: []}) {

        if (!_initialized) {
            throw Error("validateCollectionWithAllData called, but data is not initialized");
        }

        if (!Array.isArray(options.indexSpecFieldsToSkip)) {
            throw new Error("Option 'indexSpecFieldsToSkip' must be an array");
        }

        // Get the metadata for this collection
        var newCollectionInfo = this.getCollectionInfo(collection);

        let colInfo1 = injectExpected34FieldsIf32(_collectionInfo, _dbVersion);
        let colInfo2 = injectExpected34FieldsIf32(newCollectionInfo, dbVersionForCollection);
        assert.docEq(colInfo1, colInfo2, "collection metadata not equal");

        // Get the indexes for this collection
        var newIndexData = collection.getIndexes().sort(function(a, b) {
            if (a.name > b.name)
                return 1;
            else
                return -1;
        });
        for (var i = 0; i < newIndexData.length; i++) {
            let recordedIndex = Object.extend({}, _indexData[i]);
            let newIndex = Object.extend({}, newIndexData[i]);

            options.indexSpecFieldsToSkip.forEach(fieldName => {
                delete recordedIndex[fieldName];
                delete newIndex[fieldName];
            });

            assert.docEq(recordedIndex, newIndex, "indexes not equal");
        }

        // Save the data for this collection for later comparison
        var newCollectionData = collection.find().sort({"_id": 1}).toArray();
        for (var i = 0; i < newCollectionData.length; i++) {
            assert.docEq(_collectionData[i], newCollectionData[i], "data not equal");
        }
        return true;
    };
}

// Tests of the functions and classes in this file
function collectionDataValidatorTests() {
    // TODO: These tests are hackish and depend on implementation details, but they are good enough
    // for now to convince us that the CollectionDataValidator is actually checking something
    var myValidator;
    var myGenerator;
    var collection;

    myGenerator = new CollectionDataGenerator({"capped": true});
    collection = createCollectionWithData(db, "test", myGenerator);
    myValidator = new CollectionDataValidator();
    myValidator.recordCollectionData(collection);
    db.test.dropIndex(db.test.getIndexKeys().filter(function(key) {
        return key.a != null;
    })[0]);
    assert.throws(myValidator.validateCollectionData,
                  [collection],
                  "Validation function should have thrown since we modified the collection");

    myGenerator = new CollectionDataGenerator({"capped": true});
    collection = createCollectionWithData(db, "test", myGenerator);
    myValidator = new CollectionDataValidator();
    myValidator.recordCollectionData(collection);
    db.test.update({_id: 0}, {dummy: 1});
    assert.throws(myValidator.validateCollectionData,
                  [collection],
                  "Validation function should have thrown since we modified the collection");

    myGenerator = new CollectionDataGenerator({"capped": true});
    collection = createCollectionWithData(db, "test", myGenerator);
    myValidator = new CollectionDataValidator();
    myValidator.recordCollectionData(collection);
    assert(myValidator.validateCollectionData(collection), "Validation function failed");

    myGenerator = new CollectionDataGenerator({"capped": false});
    collection = createCollectionWithData(db, "test", myGenerator);
    myValidator = new CollectionDataValidator();
    myValidator.recordCollectionData(collection);
    db.test.dropIndex(db.test.getIndexKeys().filter(function(key) {
        return key.a != null;
    })[0]);
    assert.throws(myValidator.validateCollectionData,
                  [collection],
                  "Validation function should have thrown since we modified the collection");

    myGenerator = new CollectionDataGenerator({"capped": false});
    collection = createCollectionWithData(db, "test", myGenerator);
    myValidator = new CollectionDataValidator();
    myValidator.recordCollectionData(collection);
    db.test.update({_id: 0}, {dummy: 1});
    assert.throws(myValidator.validateCollectionData,
                  [collection],
                  "Validation function should have thrown since we modified the collection");

    myGenerator = new CollectionDataGenerator({"capped": false});
    collection = createCollectionWithData(db, "test", myGenerator);
    myValidator = new CollectionDataValidator();
    myValidator.recordCollectionData(collection);
    assert(myValidator.validateCollectionData(collection), "Validation function failed");

    print("collection data validator tests passed!");
}
