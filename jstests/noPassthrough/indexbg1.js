// SERVER-13922
if (0) {
// Test background index creation

load( "jstests/libs/slow_weekly_util.js" )

testServer = new SlowWeeklyMongod( "indexbg1" )
db = testServer.getDB( "test" );

parallel = function() {
    return db[ baseName + "_parallelStatus" ];
}

resetParallel = function() {
    parallel().drop();
}

doParallel = function(work) {
    resetParallel();
    print("doParallel: " + work);
    startMongoProgramNoConnect("mongo", "--eval", work + "; db." + baseName + "_parallelStatus.save( {done:1} );", db.getMongo().host);
}

doneParallel = function() {
    return !!parallel().findOne();
}

waitParallel = function() {
    assert.soon( function() { return doneParallel(); }, "parallel did not finish in time", 300000, 1000 );
}

size = 1000 * 1000;
while( 1 ) { // if indexing finishes before we can run checks, try indexing w/ more data
    print( "size: " + size );
    baseName = "jstests_indexbg1";
    fullName = "db." + baseName;
    t = db[ baseName ];
    t.drop();

    var bulk = db.jstests_indexbg1.initializeUnorderedBulkOp();
    for( i = 0; i < size; ++i ) {
        bulk.insert({ i: i });
    }
    assert.writeOK(bulk.execute());
    assert.eq( size, t.count() );
    
    doParallel( fullName + ".ensureIndex( {i:1}, {background:true} )" );
    try {
        // wait for indexing to start
        print("wait for indexing to start");
        assert.soon( function() { return 2 == db.system.indexes.count( {ns:"test."+baseName} ) }, "no index created", 30000, 50 );
        print("started.");
        sleep(1000); // there is a race between when the index build shows up in curop and
                     // when it first attempts to grab a write lock.
        assert.eq( size, t.count() );
        assert.eq( 100, t.findOne( {i:100} ).i );
        q = t.find();
        for( i = 0; i < 120; ++i ) { // getmore
            q.next();
            assert( q.hasNext(), "no next" );
        }
        var ex = t.find( {i:100} ).limit(-1).explain()
        printjson(ex)
        assert.eq( "BasicCursor", ex.cursor, "used btree cursor" );
        assert( ex.nscanned < 1000 , "took too long to find 100: " + tojson( ex ) );


        assert.writeOK(t.remove({ i: 40 }, true )); // table scan
        assert.writeOK(t.update({ i: 10 }, { i :-10 })); // should scan 10

        id = t.find().hint( {$natural:-1} ).next()._id;

        assert.writeOK(t.update({ _id: id }, { i: -2 } ));
        assert.writeOK(t.save({ i: -50 }));
        assert.writeOK(t.save({ i: size + 2 }));

        assert.eq( size + 1, t.count() );

        print( "finished with checks" );
    } catch( e ) {
        // only a failure if we're still indexing
        // wait for parallel status to update to reflect indexing status
        print("caught exception: " + e );
        sleep( 1000 );
        if ( !doneParallel() ) {
            throw e;
        }
        print("but that's OK")
    }

    print( "going to check if index is done" );
    if ( !doneParallel() ) {
        break;
    }
    print( "indexing finished too soon, retrying..." );
    size *= 2;
    assert( size < 200000000, "unable to run checks in parallel with index creation" );
}

print("our tests done, waiting for parallel to finish");
waitParallel();
print("finished");

assert.eq( "BtreeCursor i_1", t.find( {i:100} ).explain().cursor );
assert.eq( 1, t.count( {i:-10} ) );
assert.eq( 1, t.count( {i:-2} ) );
assert.eq( 1, t.count( {i:-50} ) );
assert.eq( 1, t.count( {i:size+2} ) );
assert.eq( 0, t.count( {i:40} ) );
print("about to drop index");
t.dropIndex( {i:1} );
var gle = db.getLastError();
printjson( gle );
assert( !gle );

testServer.stop();
}  // if(0)
