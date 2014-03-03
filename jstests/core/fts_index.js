// Test that:
// 1. Text indexes properly validate the index spec used to create them.
// 2. Text indexes properly enforce a schema on the language_override field.
// 3. Collections may have at most one text index.
// 4. Text indexes properly handle large documents.

var coll = db.fts_index;
var indexName = "textIndex";
coll.drop();
coll.getDB().createCollection(coll.getName());

//
// 1. Text indexes properly validate the index spec used to create them.
//

// Spec passes text-specific index validation.
assert.writeOK(coll.ensureIndex({a: "text"}, {name: indexName, default_language: "spanish"}));
assert.eq(1, coll.getDB().system.indexes.count({ns: coll.getFullName(), name: indexName}));
coll.dropIndexes();

// Spec fails text-specific index validation ("spanglish" unrecognized).
assert.writeError(coll.ensureIndex({a: "text"}, {name: indexName, default_language: "spanglish"}));
assert.eq(0, coll.system.indexes.count({ns: coll.getFullName(), name: indexName}));
coll.dropIndexes();

// Spec passes general index validation.
assert.writeOK(coll.ensureIndex({"$**": "text"}, {name: indexName}));
assert.eq(1, coll.getDB().system.indexes.count({ns: coll.getFullName(), name: indexName}));
coll.dropIndexes();

// Spec fails general index validation ("a.$**" invalid field name for key).
assert.writeError(coll.ensureIndex({"a.$**": "text"}, {name: indexName}));
assert.eq(0, coll.getDB().system.indexes.count({ns: coll.getFullName(), name: indexName}));
coll.dropIndexes();

//
// 2. Text indexes properly enforce a schema on the language_override field.
//

// Can create a text index on a collection where no documents have invalid language_override.
coll.insert({a: ""});
coll.insert({a: "", language: "spanish"});
assert.writeOK(coll.ensureIndex({a: "text"}));
coll.drop();

// Can't create a text index on a collection containing document with an invalid language_override.
coll.insert({a: "", language: "spanglish"});
assert.writeError(coll.ensureIndex({a: "text"}));
coll.drop();

// Can insert documents with valid language_override into text-indexed collection.
assert.writeOK(coll.ensureIndex({a: "text"}));
coll.insert({a: ""});
assert.writeOK( coll.insert({a: "", language: "spanish"}));
coll.drop();

// Can't insert documents with invalid language_override into text-indexed collection.
assert.writeOK(coll.ensureIndex({a: "text"}));
assert.writeError( coll.insert({a: "", language: "spanglish"}));
coll.drop();

//
// 3. Collections may have at most one text index.
//
assert.writeOK(coll.ensureIndex({a: 1, b: "text", c: 1}));
assert.eq(2, coll.getIndexes().length);

// ensureIndex() becomes a no-op on an equivalent index spec.
assert.writeOK(coll.ensureIndex({a: 1, b: "text", c: 1}));
assert.eq(2, coll.getIndexes().length);
assert.writeOK(coll.ensureIndex({a: 1, b: "text", c: 1}, {background: true}));
assert.eq(2, coll.getIndexes().length);
assert.writeOK(coll.ensureIndex({a: 1, _fts: "text", _ftsx: 1, c: 1}, {weights: {b: 1}}));
assert.eq(2, coll.getIndexes().length);
assert.writeOK(coll.ensureIndex({a: 1, b: "text", c: 1}, {default_language: "english"}));
assert.eq(2, coll.getIndexes().length);
assert.writeOK(coll.ensureIndex({a: 1, b: "text", c: 1}, {textIndexVersion: 2}));
assert.eq(2, coll.getIndexes().length);
assert.writeOK(coll.ensureIndex({a: 1, b: "text", c: 1}, {language_override: "language"}));
assert.eq(2, coll.getIndexes().length);

// ensureIndex() fails if a second text index would be built.
assert.writeError(coll.ensureIndex({a: 1, _fts: "text", _ftsx: 1, c: 1}, {weights: {d: 1}}));
assert.writeError(coll.ensureIndex({a: 1, b: "text", c: 1}, {default_language: "none"}));
assert.writeError(coll.ensureIndex({a: 1, b: "text", c: 1}, {textIndexVersion: 1}));
assert.writeError(coll.ensureIndex({a: 1, b: "text", c: 1}, {language_override: "idioma"}));
assert.writeError(coll.ensureIndex({a: 1, b: "text", c: 1}, {weights: {d: 1}}));
assert.writeError(coll.ensureIndex({a: 1, b: "text", d: 1}));
assert.writeError(coll.ensureIndex({a: 1, d: "text", c: 1}));
assert.writeError(coll.ensureIndex({b: "text"}));
assert.writeError(coll.ensureIndex({b: "text", c: 1}));
assert.writeError(coll.ensureIndex({a: 1, b: "text"}));

coll.dropIndexes();

//
// 4. Text indexes properly handle large keys.
//

assert.writeOK(coll.ensureIndex({a: "text"}));

var longstring = "";
var longstring2 = "";
for(var i = 0; i < 1024 * 1024; ++i) {
    longstring = longstring + "a";
    longstring2 = longstring2 + "b";
}
coll.insert({a: longstring});
coll.insert({a: longstring2});
assert.eq(1, coll.find({$text: {$search: longstring}}).itcount(), "long string not found in index");

coll.drop();
