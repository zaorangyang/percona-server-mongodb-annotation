(function() {
    'use strict';

    var conn = MongoRunner.runMongod({});

    var db = conn.getDB('test');
    // Non-admin execution
    assert.commandFailed(db.runCommand({createBackup: 1, path: MongoRunner.dataPath}));

    var adminDB = db.getSiblingDB('admin');

    // Wrong value format
    assert.commandFailed(adminDB.runCommand({createBackup: 1, path: 0}));

    // Non-existent path
    assert.commandFailed(adminDB.runCommand({createBackup: 1, path: '/non-existent/path'}));

    // Relative path
    assert.commandFailed(adminDB.runCommand({createBackup: 1, path: '../usr'}));

    // Non-empty backup folder
    assert.commandFailed(adminDB.runCommand({createBackup: 1, path: '/'}));
})();
