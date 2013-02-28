// this is to make sure that temp collections get cleaned up on restart

testname = 'temp_namespace_sw'
path = '/data/db/'+testname

conn = startMongodEmpty("--port", 30000, "--dbpath", path, "--smallfiles", "--noprealloc", "--nopreallocj");
d = conn.getDB('test')
d.runCommand({create: testname+'temp1', temp: true});
d[testname+'temp1'].ensureIndex({x:1});
d.runCommand({create: testname+'temp2', temp: 1});
d[testname+'temp2'].ensureIndex({x:1});
d.runCommand({create: testname+'keep1', temp: false});
d.runCommand({create: testname+'keep2', temp: 0});
d.runCommand({create: testname+'keep3'});
d[testname+'keep4'].insert({});

assert.eq(d.system.namespaces.count({name: /temp\d$/}) , 2) // collections
assert.eq(d.system.namespaces.count({name: /temp\d\.\$.*$/}) , 4) //indexes (2 _id + 2 x)
assert.eq(d.system.namespaces.count({name: /keep\d$/}) , 4)
stopMongod(30000);

conn = startMongodNoReset("--port", 30000, "--dbpath", path, "--smallfiles", "--noprealloc", "--nopreallocj");
d = conn.getDB('test')
assert.eq(d.system.namespaces.count({name: /temp\d$/}) , 0) // collections
assert.eq(d.system.namespaces.count({name: /temp\d\.\$.*$/}) , 0) //indexes
assert.eq(d.system.namespaces.count({name: /keep\d$/}) , 4)
stopMongod(30000);
