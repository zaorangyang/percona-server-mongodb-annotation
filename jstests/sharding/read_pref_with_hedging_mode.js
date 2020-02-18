/*
 * Intergration test for read preference with hedging mode. The more comprehensive
 * unit test can be found in dbtests/read_preference_test.cpp and s/hedge_options_util_test.cpp.
 *
 * TODO (SERVER-45432): test that hedging is performed as expected.
 * @tags: [requires_fcv_44]
 */
(function() {

const st = new ShardingTest({shards: 2});
const dbName = "foo";
const collName = "bar";
const ns = dbName + "." + collName;
const testDB = st.s.getDB(dbName);

assert.commandWorked(st.s.adminCommand({enableSharding: dbName}));
st.ensurePrimaryShard(dbName, st.shard0.shardName);
assert.commandWorked(st.s.adminCommand({shardCollection: ns, key: {x: 1}}));

// Test "hedge" read preference validation.
assert.commandFailedWithCode(
    testDB.runCommand({query: {count: collName}, $readPreference: {mode: "primary", hedge: {}}}),
    ErrorCodes.InvalidOptions);

// Test "readHedgingMode" server parameter validation.
assert.commandFailedWithCode(st.s.adminCommand({setParameter: 1, readHedgingMode: "invalidMode"}),
                             ErrorCodes.BadValue);

// Test setting maxTimeMS for hedged reads.
assert.commandWorked(st.s.adminCommand({setParameter: 1, maxTimeMSForHedgedReads: 100}));

// Test hedging with maxTimeMS.
assert.commandWorked(st.s.getDB(dbName).runCommand(
    {query: {find: collName, maxTimeMS: 1000}, $readPreference: {mode: "nearest", hedge: {}}}));

// Test hedging without maxTimeMS.
assert.commandWorked(st.s.getDB(dbName).runCommand({
    query: {count: collName},
    $readPreference: {mode: "secondaryPreferred", hedge: {enabled: true}}
}));

// Set "readHedgingMode" to "off", expect no hedging.
st.s.adminCommand({setParameter: 1, readHedgingMode: "off"});

assert.commandWorked(st.s.getDB(dbName).runCommand({
    query: {distinct: collName, key: "x"},
    $readPreference: {mode: "primaryPreferred", hedge: {}}
}));

st.stop();
})();
