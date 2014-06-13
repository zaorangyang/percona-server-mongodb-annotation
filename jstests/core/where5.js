// Tests toString() on _v8_function in object constructor.

var t = db.where5;

t.drop();

t.save({a: 1});

// Prints information on the document's _id field.
function printIdConstructor(doc) {
    // If doc is undefined, this function is running inside server.
    if (!doc) {
        doc = this;
    }

    // This used to crash.
    doc._id.constructor._v8_function.toString();

    // Predicate for matching document in collection.
    return true;
}

print('Running JS function in server...');
assert.eq(t.find({$where: printIdConstructor}).itcount(), 1);

print('Running JS function in client...');
var doc = t.findOne();
printIdConstructor(doc);
