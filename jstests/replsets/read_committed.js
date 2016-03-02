/**
 * Test basic read committed functionality, including:
 *  - Writes with writeConcern 'majority' should be visible once the write completes.
 *  - With the only data-bearing secondary down, committed reads should not include newly inserted
 *    data.
 *  - When data-bearing node comes back up and catches up, writes should be readable.
 */

load("jstests/replsets/rslib.js");  // For startSetIfSupportsReadMajority.

(function() {
    "use strict";

    function log(arg) {
        jsTest.log(tojson(arg));
    }

    // Set up a set and grab things for later.
    var name = "read_committed";
    var replTest =
        new ReplSetTest({name: name, nodes: 3, nodeOptions: {enableMajorityReadConcern: ''}});

    if (!startSetIfSupportsReadMajority(replTest)) {
        jsTest.log("skipping test since storage engine doesn't support committed reads");
        return;
    }

    var nodes = replTest.nodeList();
    var config = {
        "_id": name,
        "members": [
            {"_id": 0, "host": nodes[0]},
            {"_id": 1, "host": nodes[1], priority: 0},
            {"_id": 2, "host": nodes[2], arbiterOnly: true}
        ]
    };
    updateConfigIfNotDurable(config);
    replTest.initiate(config);

    // Get connections and collection.
    var primary = replTest.getPrimary();
    var secondary = replTest.liveNodes.slaves[0];
    var secondaryId = replTest.getNodeId(secondary);
    var db = primary.getDB(name);
    var t = db[name];

    function doRead(readConcern) {
        readConcern.maxTimeMS = 3000;
        var res = assert.commandWorked(t.runCommand('find', readConcern));
        var docs = (new DBCommandCursor(db.getMongo(), res)).toArray();
        assert.gt(docs.length, 0, "no docs returned!");
        return docs[0].state;
    }

    function doDirtyRead() {
        log("doing dirty read");
        var ret = doRead({"readConcern": {"level": "local"}});
        log("done doing dirty read.");
        return ret;
    }

    function doCommittedRead() {
        log("doing committed read");
        var ret = doRead({"readConcern": {"level": "majority"}});
        log("done doing committed read.");
        return ret;
    }

    // Do a write, wait for it to replicate, and ensure it is visible.
    assert.writeOK(
        t.save({_id: 1, state: 0}, {writeConcern: {w: "majority", wtimeout: 60 * 1000}}));
    assert.eq(doDirtyRead(), 0);
    assert.eq(doCommittedRead(), 0);

    replTest.stop(secondaryId);

    // Do a write and ensure it is only visible to dirty reads
    assert.writeOK(t.save({_id: 1, state: 1}));
    assert.eq(doDirtyRead(), 1);
    assert.eq(doCommittedRead(), 0);

    // Try the committed read again after sleeping to ensure it doesn't only work for queries
    // immediately after the write.
    sleep(1000);
    assert.eq(doCommittedRead(), 0);

    // Restart the node and ensure the committed view is updated.
    replTest.restart(secondaryId);
    db.getLastError("majority", 60 * 1000);
    assert.eq(doDirtyRead(), 1);
    assert.eq(doCommittedRead(), 1);

}());
