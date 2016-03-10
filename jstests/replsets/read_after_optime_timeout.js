// Test read after opTime functionality with maxTimeMS.

(function() {
"use strict";

var replTest = new ReplSetTest({ nodes: 2 });
replTest.startSet();

var config = replTest.getReplSetConfig();
// TODO: SERVER-18298 uncomment once implemented.
//config.protocolVersion = 1;
replTest.initiate(config);

var runTest = function(testDB, primaryConn) {
    primaryConn.getDB('test').user.insert({ x: 1 }, { writeConcern: { w: 2 }});

    var localDB = primaryConn.getDB('local');

    var oplogTS = localDB.oplog.rs.find().sort({ $natural: -1 }).limit(1).next().ts;
    var twoSecTS = new Timestamp(oplogTS.getTime() + 2, 0);

    // Test timeout with maxTimeMS < after.opTime
    var res = assert.commandFailed(testDB.runCommand({
        find: 'user',
        filter: { x: 1 },
        after: {
            opTime: { ts: twoSecTS, term: 0 },
            timeoutMS: 10 * 1000
        },
        maxTimeMS: 1000
    }));

    assert.eq(50, res.code); // ErrorCodes::ExceededTimeLimit
    assert.gt(res.waitedMS, 500);
    assert.lt(res.waitedMS, 2500);

    // Test timeout with after.opTime < maxTimeMS
    res = assert.commandFailed(testDB.runCommand({
        find: 'user',
        filter: { x: 1 },
        after: {
            opTime: { ts: twoSecTS, term: 0 },
            timeoutMS: 1 * 1000
        },
        maxTimeMS: 10 * 1000
    }));

    assert.eq(122, res.code); // ErrorCodes::ReadAfterOptimeTimeout
    assert.gt(res.waitedMS, 500);
    assert.lt(res.waitedMS, 2500);

    // Test timeout with maxTimeMS, no after.timeout
    res = assert.commandFailed(testDB.runCommand({
        find: 'user',
        filter: { x: 1 },
        after: {
            opTime: { ts: twoSecTS, term: 0 }
        },
        maxTimeMS: 1 * 1000
    }));

    assert.eq(50, res.code); // ErrorCodes::ExceededTimeLimit
    assert.gt(res.waitedMS, 500);
    assert.lt(res.waitedMS, 2500);

    // Test timeout with after.opTime, no maxTimeMS
    res = assert.commandFailed(testDB.runCommand({
        find: 'user',
        filter: { x: 1 },
        after: {
            opTime: { ts: twoSecTS, term: 0 },
            timeoutMS: 1 * 1000
        }
    }));

    assert.eq(122, res.code); // ErrorCodes::ReadAfterOptimeTimeout
    assert.gt(res.waitedMS, 500);
    assert.lt(res.waitedMS, 2500);

    // Test read on future opTime that will eventually occur.
    var insertFunc = startParallelShell(
        "sleep(2100); db.user.insert({ y: 1 }, { writeConcern: { w: 2 }});",
        primaryConn.port);

    res = assert.commandWorked(testDB.runCommand({
        find: 'user',
        filter: { x: 1 },
        after: {
            opTime: { ts: twoSecTS, term: 0 }
        }
    }));

    assert.eq(null, res.code);
    assert.gt(res.waitedMS, 0);

    insertFunc();
};

var primary = replTest.getPrimary();
runTest(primary.getDB('test'), primary);
runTest(replTest.getSecondary().getDB('test'), primary);

replTest.stopSet();

})();
