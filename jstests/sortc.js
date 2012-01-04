// Test sorting with skipping and multiple candidate query plans.

t = db.jstests_sortc;
t.drop();

t.save( {a:1} );
t.save( {a:2} );

function checkA( a, sort, skip ) {
    assert.eq( a, t.find().sort( sort ).skip( skip )[ 0 ].a );
}

function checkSortAndSkip() {
    checkA( 1, {a:1}, 0 );
    checkA( 2, {a:1}, 1 );

    checkA( 2, {a:-1}, 0 );
    checkA( 1, {a:-1}, 1 );

    checkA( 1, {$natural:1}, 0 );
    checkA( 2, {$natural:1}, 1 );

    checkA( 2, {$natural:-1}, 0 );
    checkA( 1, {$natural:-1}, 1 );
}

checkSortAndSkip();

t.ensureIndex( {a:1} );
checkSortAndSkip();
