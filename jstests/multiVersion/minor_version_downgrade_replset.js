// Test the downgrade of a replica set from latest version
// to last-stable version succeeds, while reads and writes continue.
// @tags: [requires_mmapv1]

load('./jstests/multiVersion/libs/multi_rs.js');
load('./jstests/libs/test_background_ops.js');

// 3.2.1 is the final version to use the old style replSetUpdatePosition command.
var oldVersion = "3.2.1";
var newVersion = "latest";

var name = "replsetdowngrade";
var nodes = {
    n1: {binVersion: newVersion},
    n2: {binVersion: newVersion},
    n3: {binVersion: newVersion}
};

// SERVER-25132 - Only runs in mmapv1
var storageEngine = "mmapv1";
var rst = new ReplSetTest({name: name, nodes: nodes, nodeOptions: {storageEngine: storageEngine}});
rst.startSet();
var replSetConfig = rst.getReplSetConfig();
replSetConfig.protocolVersion = 0;
rst.initiate(replSetConfig);

var primary = rst.getPrimary();
var coll = "test.foo";

// We set the featureCompatibilityVersion to 3.2 so that the default index version becomes v=1. We
// do this prior to writing any data to the replica set so that any indexes created during this test
// are compatible with 3.2. This effectively allows us to emulate upgrading to the latest version
// with existing data files and then trying to downgrade back to 3.2.
assert.commandWorked(primary.adminCommand({setFeatureCompatibilityVersion: "3.2"}));

jsTest.log("Inserting documents into collection.");
for (var i = 0; i < 10; i++) {
    primary.getCollection(coll).insert({_id: i, str: "hello world"});
}

function insertDocuments(rsURL, coll) {
    var coll = new Mongo(rsURL).getCollection(coll);
    var count = 10;
    while (!isFinished()) {
        assert.writeOK(coll.insert({_id: count, str: "hello world"}));
        count++;
    }
}

jsTest.log("Starting parallel operations during downgrade..");
var joinFindInsert = startParallelOps(primary, insertDocuments, [rst.getURL(), coll]);

jsTest.log("Downgrading replica set..");
rst.upgradeSet({binVersion: oldVersion, storageEngine: storageEngine});
jsTest.log("Downgrade complete.");

primary = rst.getPrimary();
printjson(rst.status());

joinFindInsert();
rst.stopSet();
