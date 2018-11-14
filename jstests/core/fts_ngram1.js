// This is for ngram fts by matt.lee
// @tags: [assumes_no_implicit_index_creation]
(function() {
    "use strict";

    const coll = db.text1;
    coll.drop();

    assert.commandWorked(coll.createIndex({name:"text"}, {default_language: "ngram", name: "x_ngramtext"}));

    assert.writeOK(coll.insert({ _id: 1, name: "MongoDB는 좋은 비관계형 데이터베이스 서버입니다" }));

    var res_count;

    // Found
    res_count = coll.find({ $text: { $search: "go" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd 'go'");

    res_count = coll.find({ $text: { $search: "od" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd 'od'");

    res_count = coll.find({ $text: { $search: "좋은" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd '좋은'");

    res_count = coll.find({ $text: { $search: "계형" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd '계형'");

    res_count = coll.find({ $text: { $search: "데이" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd '데이'");

    res_count = coll.find({ $text: { $search: "이터" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd '이터'");

    res_count = coll.find({ $text: { $search: "터베" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd '터베'");

    res_count = coll.find({ $text: { $search: "베이" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd '베이'");

    res_count = coll.find({ $text: { $search: "이스" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd '이스'");

    res_count = coll.find({ $text: { $search: "서버" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd '서버'");

    res_count = coll.find({ $text: { $search: "버입" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd '버입'");

    res_count = coll.find({ $text: { $search: "입니" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd '입니'");

    res_count = coll.find({ $text: { $search: "니다" } }).count();
    assert.eq(1, res_count, "Not found for keyworkd '니다'");

    // Not found
    res_count = coll.find({ $text: { $search: "나다" } }).count();
    assert.eq(0, res_count, "Not found for keyworkd '나다'");

    // Index meta info
    const index = coll.getIndexes().find(index => index.name === "x_ngramtext");
    assert.neq(index, undefined);
    assert.gte(index.textIndexVersion, 3, tojson(index));
    assert.eq(index.default_language, "ngram", tojson(index));
}());
