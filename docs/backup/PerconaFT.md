# Backup

This document describes how to build, configure and use Hot Backup for the PerconaFT storage engine.

## Build HotBackup Library

First, checkout the HotBackup library repository.  It's provided as a submodule and should be checked out along with PerconaFT storage engine (refer to the corresponding documentation for the process).
Don't forget to check out proper HotBackup library version.

Second, configure the cmake environment for the Hot Backup Library.  There are some important cmake options
that must be provided:

1. `BUILD_STATIC_LIBRARY` - This option builds a static `.a` version of the HotBackup Library.  This is needed by SCons in MongoDB.
2. `CMAKE_INSTALL_PREFIX` - This option will determine where the `lib` and `include` folders containing the library and the `backup.h` header will go, respectively.  The location of these folders and files is hard coded in the MongoDB SCons files.

In the following example we create a `Debug` build directory within the Hot Backup repository tree. The `Debug` dir is used for configuring the cmake environment and building the library.

```
pushd src/third_party/Percona-TokuBackup/backup
mkdir Debug
cd Debug
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$PWD/../../../install -DBUILD_STATIC_LIBRARY=ON ..
make -j4
make install
popd
```

This will create the Hot Backup static library in `src/third_party/install/lib` and place the C API header in `src/third_party/install/include`. These directories are needed for linking the library with, and compiling HotBackup commands in, `mongod`.

## Building Percona Server for MongoDB

When running SCons, from the top level of the MongoDB source tree, an extra option is needed to build the HotBackup commands. The `--tokubackup` flag must be added to the SCons arguments to build MongoDB. Since HotBackup works for PerconaFT storage engine, --PerconaFT option is also mandatory.

```
scons --tokubackup --PerconaFT mongod
```

**Note:** If you see any errors or warnings most likely SCons cannot find the library or header and the linking or filepaths are incorrect.

## Using HotBackup:

To take a HotBackup of the MongoDB/PerconaFT files in your current `dbpath`, you can execute the following command as an administrator on the `admin` database:

```
> use admin
switched to db admin
> db.runCommand({backupStart:"/my/backup/data/path"})
{ "ok" : 1 }
```

You should receive an `{ "ok" : 1 }` object as a return value if the backup was successful.  If there was an error, you will receive an error message along with failing `ok` status.  It may look something like this:

```
> db.runCommand({backupStart:""})
{ "ok" : 0, "errmsg" : "invalid destination directory: ''" }
```

## Check Progress

For long running backups, with lots of data to copy, it is helpful to see how much data the Hot Backup process has copied.  To see the high level status of a backup use the following command:

```
> db.runCommand({backupStatus:1})
{
        "inProgress" : false,
        "bytesCopied" : NumberLong(0),
        "filesCopied" : 0,
        "ok" : 1
}
```

In this case there is no backup in progress.  The `inProgress` field will return true when there is a Hot Backup currently executing inside the `mongod` server.  The `bytesCopied` and the `filesCopied` fields will increase as the Hot Backup copies the bytes from the source files to the backup destination directory.  

## Control Copy Rate

The rate at which HotBackup copies the files from the source directories (like those in your `dbpath` setting) can be controlled by setting the backup throttle.  This is measured in Bytes-Per-Second.

```
> db.runCommand({backupThrottle:128000})
```

## Sanity Check

Hot Backup relies on capturing UNIX file system calls, like `open`, `truncate`, and `pwrite`.  These symbols must resolve to the Hot Backup library and NOT GLIBC.  Here are a few shell commands that can be run from the root install dir to see if the symbols have been resolved properly:

```
nm mongod | grep 'tokubackup_create_backup'
nm mongod | ag 'T open'
nm mongod | ag 'T close'
nm mongod | ag 'T read'
nm mongod | ag 'T write'
nm mongod | ag 'T pwrite'
nm mongod | ag 'T lseek'
nm mongod | ag 'T rename'
nm mongod | ag 'T unlink'
nm mongod | ag 'T ftruncate'
nm mongod | ag 'T truncate'
nm mongod | ag 'T mkdir'
```
