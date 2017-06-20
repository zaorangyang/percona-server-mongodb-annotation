/**
 * Helper functions for testing causal consistency.
 */

load('jstests/replsets/rslib.js');  // For startSetIfSupportsReadMajority.

function assertAfterClusterTimeReadFails(db, collName) {
    assert.commandFailed(db.runCommand(
        {find: collName, readConcern: {level: "majority", afterClusterTime: Timestamp(1, 1)}}));
}

function assertAfterClusterTimeReadSucceeds(db, collName) {
    assert.commandWorked(db.runCommand(
        {find: collName, readConcern: {level: "majority", afterClusterTime: Timestamp(1, 1)}}));
}

function assertDoesNotContainLogicalOrOperationTime(res) {
    assertDoesNotContainLogicalTime(res);
    assertDoesNotContainOperationTime(res);
}

function assertDoesNotContainLogicalTime(res) {
    assert.eq(res.$logicalTime, undefined);
}

function assertDoesNotContainOperationTime(res) {
    assert.eq(res.operationTime, undefined);
}

function assertContainsLogicalAndOperationTime(res, opts) {
    assertContainsLogicalTime(res, opts);
    assertContainsOperationTime(res, opts);
}

function assertContainsLogicalTime(res, opts) {
    assert.hasFields(res, ["$logicalTime"]);
    assert.hasFields(res.$logicalTime, ["clusterTime", "signature"]);
    assert.hasFields(res.$logicalTime.signature, ["hash", "keyId"]);

    if (opts.signed !== undefined) {
        // Signed logical times have a keyId greater than 0.
        if (opts.signed) {
            assert(res.$logicalTime.signature.keyId > NumberLong(0));
        } else {
            assert.eq(res.$logicalTime.signature.keyId, NumberLong(0));
        }
    }

    if (opts.initialized !== undefined) {
        // Initialized operation times are greater than a null timestamp.
        if (opts.initialized) {
            assert.eq(bsonWoCompare(res.$logicalTime.clusterTime, Timestamp(0, 0)), 1);
        } else {
            assert.eq(bsonWoCompare(res.$logicalTime.clusterTime, Timestamp(0, 0)), 0);
        }
    }
}

function assertContainsOperationTime(res, opts) {
    assert.hasFields(res, ["operationTime"]);

    if (opts.initialized !== undefined) {
        // Initialized operation times are greater than a null timestamp.
        if (opts.initialized) {
            assert.eq(bsonWoCompare(res.operationTime, Timestamp(0, 0)), 1);
        } else {
            assert.eq(bsonWoCompare(res.operationTime, Timestamp(0, 0)), 0);
        }
    }
}

function supportsMajorityReadConcern() {
    const rst = new ReplSetTest({nodes: 1, nodeOptions: {enableMajorityReadConcern: ""}});
    if (!startSetIfSupportsReadMajority(rst)) {
        return false;
    }
    return true;
}
