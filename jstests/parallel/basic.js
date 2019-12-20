// perform basic js tests in parallel
load('jstests/libs/parallelTester.js');

Random.setRandomSeed();

var params = ParallelTester.createJstestsLists(4);
var t = new ParallelTester();
for (i in params) {
    t.add(ParallelTester.fileTester, params[i]);
}

t.run("one or more tests failed");
