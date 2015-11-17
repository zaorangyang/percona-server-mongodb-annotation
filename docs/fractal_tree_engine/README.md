
# Fractal Tree Storage Engine

This document describes how to build, enable, configure and test the Percona Fractal Tree (PerconaFT) storage engine.

## Building

In order to build MongoDB with support for the PerconaFT storage engine, we need to checkout the respective source code,
build them, create symbolic links to the respective installation paths, then run SCons from the top level
directory with the `--PerconaFT` option.

#### Git Submodules

Before we build anything, we need to check out the source code for the Fractal Tree library and it's main dependency: the jemalloc library.
We use `git` submodules included within the `src/third_party` directory to point to the two main git source code repositories.  So, let's
initialize and checkout the top of each module:

```
git submodule init
```

You should see something like this on the command line:

```
Submodule 'PerconaFT' (https://github.com/percona/PerconaFT) registered for path 'src/third_party/PerconaFT'
Submodule 'Jemalloc' (https://github.com/percona/jemalloc.git) registered for path 'src/third_party/jemalloc'
```

Then you actually want to pull the code from the repositories:

```
git submodule update
```

You should see the sub-directories updating to reflect the recent changes.  It should look something like this:

```
Cloning into 'src/third_party/PerconaFT'...
remote: Counting objects: 67548, done.
remote: Compressing objects: 100% (9/9), done.
...
Cloning into 'src/third_party/jemalloc'...
remote: Counting objects: 498, done.
remote: Total 498 (delta 0), reused 0 (delta 0), pack-reused 498
```

Now we have to check out the respective tag (or branch) for each Repo.  Typically the three main local repositories on your machine will
share the same release tag, though for experimental builds it may make sense to checkout different branches for each repo.  Assuming
there is a tag called `psmdb-1.0.0`:

```
pushd src/third_party/PerconaFT
git checkout psmdb-1.0.0
popd
pushd src/third_party/jemalloc
git checkout psmdb-1.0.0
popd
```

#### Compile and Install

To compile the Fractal Tree library, we first need to symlink the jemalloc directory into the right place:

```
pushd src/third_party/PerconaFT
ln -s $PWD/../jemalloc third_party/
popd
```

Next we have to configure the cmake build environment.
This generally involves creating a Debug and/or Release directory underneath the root of the Fractal Tree source tree, and passing the respective flags to `cmake`.  Once the build environment is complete, one can run `make install` and the library will be built and placed into the previously created directory, where SCons can find it.

**Note:** The SCons scripts assume that the `src/third_party/install` directory will have `include` and `lib` sub-directories with the respective fractal library components installed.  We have to add these to the CMake args for the final build to work. 

Here is a simple Debug version:

```
pushd src/third_party/PerconaFT
mkdir Debug
cd Debug
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$PWD/../../install ..
make -j4 install
popd
```

That will compile the fractal tree library files, and then install the libraries and associated headers into our special SCons directory.  This will also compile all the tests.  Though building tests takes more time upfront, this is useful for development purposes; if changes to the Fractal Tree library are made, they can be tested quickly in the same environment.

For a streamlined Release build, the commands are similar but many more options are invoked:

```
pushd src/third_party/PerconaFT
mkdir Release
cd Release
cmake \
      -D CMAKE_BUILD_TYPE=Release \
      -D USE_VALGRIND=OFF \
      -D TOKU_DEBUG_PARANOID=OFF \
      -D BUILD_TESTING=OFF \
      -DCMAKE_INSTALL_PREFIX=$PWD/../../install   ..
make -j4 install
popd
```

This will create and install an optimized build of the Fractal Tree library, without building the tests and without debugging instrumentation overhead like `valgrind` qualifiers and paranoid asserts.

#### Build MongoDB

Now that the Fractal Tree library has been built, we can build the MongoDB server components by adding the appropriate arguments:

```
scons --PerconaFT -j4 mongo mongod mongos
```

This will build the storage engine implementation of the fractal tree and link in the fractal tree library dependency.

## Testing

TODO:
