# Backup

This document describes how to build, configure and use Hot Backup for the PerconaFT storage engine.

## Build HotBackup Library

First, checkout the HotBackup library repository.  This can be placed anywhere in your build tree, so long
as it is linked properly into the MongoDB source tree after it is built.

Second, configure the cmake environment for the Hot Backup Library.  There are some important cmake options
that must be provided:

1. `BUILD_STATIC_LIBRARY` - This option builds a static `.a` version of the HotBackup Library.  This is needed by SCons in MongoDB.
2. `CMAKE_INSTALL_PREFIX` - This option will determine where the `lib` and `include` folders containing the library and the `backup.h` header will go, respectively.  The location of these folders and files is hard coded in the MongoDB SCons files.

In the following example we create both a `Debug` build directory and an `install` directory within the Hot Backup repository tree.  The `Debug` dir is used for configuring the cmake environment and building the library.  The `install` directory is used by cmake and `make install` for creating the `lib` and `include` directories.  The entire `install` directory can be then linked into the MongoDB source tree.

```
cd Percona-TokuBackup
cd backup
mkdir Debug
mkdir install
cd Debug
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$PWD/../install -DBUILD_STATIC_LIBRARY=ON ..
make -j4
make install
```

This will create the Hot Backup static library in `install/lib` and place the C API header in `install/include`. Again, these directories are needed for linking the library with, and compiling HotBackup commands in, `mongod`.

## Link HotBackup files into MongoDB

The resultant HotBackup library static library `lib/libHotBackup.a` and `include/backup.h` header file must be placed, or linked, into the `src/third_party/install` directory within the MongoDB source tree.

Assuming you have the Percona-Server-MongoDB repository checked out at the same level as the Hot Backup repository:

```
cd percona-server-mongodb
ln -s $PWD/../Percona-TokuBackup/backup/install src/third_party/install
```

## Building Percona Server for MongoDB

When running SCons, from the top level of the MongoDB source tree, an extra option is needed to build the HotBackup commands.  The `--tokubackup` flag must be added to the SCons arguments to build MongoDB.

```
scons --tokubackup mongod
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
