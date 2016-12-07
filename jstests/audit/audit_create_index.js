// test that createIndex gets audited

if (TestData.testData !== undefined) {
    load(TestData.testData + '/audit/_audit_helpers.js');
} else {
    load('jstests/audit/_audit_helpers.js');
}

var testDBName = 'audit_create_index';

auditTest(
    'createIndex',
    function(m) {
        testDB = m.getDB(testDBName);
        assert.commandWorked(testDB.dropDatabase());

        assert.commandWorked(testDB.coll.createIndex({ a: 1 }, { name: 'cold', background: false }));

        assert.commandWorked(testDB.coll.createIndex({ b: 1 }, { name: 'hot', background: true }));

        beforeLoad = Date.now();
        auditColl = getAuditEventsCollection(m, testDBName);

        assert.eq(1, auditColl.count({
            atype: "createIndex",
            ts: withinFewSecondsBefore(beforeLoad),
            'params.ns': testDBName + '.coll',
            'params.indexSpec.key': { a: 1 },
            'params.indexName': 'cold',
            result: 0,
        }), "FAILED background=false, audit log: " + tojson(auditColl.find().toArray()));

        assert.eq(1, auditColl.count({
            atype: "createIndex",
            ts: withinFewSecondsBefore(beforeLoad),
            'params.ns': testDBName + '.coll' ,
            'params.indexSpec.key': { b: 1 },
            'params.indexName': 'hot',
            result: 0,
        }), "FAILED background=true, audit log: " + tojson(auditColl.find().toArray()));
    },
    { /* no special mongod options */ }
);
