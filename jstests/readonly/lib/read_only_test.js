"use_strict";

function makeDirectoryReadOnly(dir) {
    if (_isWindows()) {
        run("attrib", "+r", dir, "/s");
    } else {
        run("chmod", "-R", "a-w", dir);
    }
}

function makeDirectoryWritable(dir) {
    if (_isWindows()) {
        run("attrib", "-r", dir, "/s");
    } else {
        run("chmod", "-R", "a+w", dir);
    }
}

function StandaloneFixture() {
}

StandaloneFixture.prototype.runLoadPhase = function runLoadPhase(test) {
    this.mongod = MongoRunner.runMongod({});
    this.dbpath = this.mongod.dbpath;

    test.load(this.mongod.getDB("test")[test.name]);
    MongoRunner.stopMongod(this.mongod);
};

StandaloneFixture.prototype.runExecPhase = function runExecPhase(test) {
    try {
        makeDirectoryReadOnly(this.dbpath);

        var options = {
            queryableBackupMode: "",
            noCleanData: true,
            dbpath: this.dbpath
        };

        this.mongod = MongoRunner.runMongod(options);

        test.exec(this.mongod.getDB("test")[test.name]);

        MongoRunner.stopMongod(this.mongod);
    } finally {
        makeDirectoryWritable(this.dbpath);
    }
};

function ShardedFixture() {
    this.nShards = 3;
}

ShardedFixture.prototype.runLoadPhase = function runLoadPhase(test) {
    this.shardingTest = new ShardingTest({
        nopreallocj: true,
        mongos: 1,
        shards: this.nShards
    });

    this.paths = this.shardingTest.getDBPaths();

    jsTest.log("sharding test collection...");

    // Use a hashed shard key so we actually hit multiple shards.
    this.shardingTest.shardColl(test.name, {_id: "hashed"});

    test.load(this.shardingTest.getDB("test")[test.name]);
};

ShardedFixture.prototype.runExecPhase = function runExecPhase(test) {
    jsTest.log("restarting shards...");
    try {
        for (var i = 0; i < this.nShards; ++i) {
            var opts = {
                queryableBackupMode: "",
                dbpath: this.paths[i]
            };

            this.shardingTest.restartMongod(i, opts, () => {
                makeDirectoryReadOnly(this.paths[i]);
            });
        }

        jsTest.log("restarting mongos...");

        this.shardingTest.restartMongos(0);

        test.exec(this.shardingTest.getDB("test")[test.name]);

        this.paths.forEach((path) => {
            makeDirectoryWritable(path);
        });

        this.shardingTest.stop();
    } finally {
        this.paths.forEach((path) => {
            makeDirectoryWritable(path);
        });
    }
};

function runReadOnlyTest(test) {

    printjson(test);

    assert.eq(typeof(test.exec), "function");
    assert.eq(typeof(test.load), "function");
    assert.eq(typeof(test.name), "string");

    var fixtureType = TestData.fixture || "standalone";

    var fixture = null;
    if (fixtureType === "standalone") {
        fixture = new StandaloneFixture();
    } else if (fixtureType === "sharded") {
        fixture = new ShardedFixture();
    } else {
        throw new Error("fixtureType must be one of either 'standalone' or 'sharded'");
    }

    jsTest.log("starting load phase for test: " + test.name);
    fixture.runLoadPhase(test);

    jsTest.log("starting execution phase for test: " + test.name);
    fixture.runExecPhase(test);
}

function * cycleN(arr, N) {
    for (var i = 0; i < N; ++i) {
        yield arr[i % arr.length];
    }
}

function * zip2(iter1, iter2) {
    var n1 = iter1.next();
    var n2 = iter2.next();
    while (!n1.done || !n2.done) {
        var res = [];
        if (!n1.done) {
            res.push(n1.value);
            n1 = iter1.next();
        }
        if (!n2.done) {
            res.push(n2.value);
            n2 = iter2.next();
        }

        yield res;
    }
}
