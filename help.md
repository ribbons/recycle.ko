recycle.ko help
===============

Description
-----------

recycle.ko is a Linux kernel module which moves all files unlinked under a root
directory to a recycle directory.

If a file with the same path already exists in the recycle dir, the current
epoch time in milliseconds is appended to the filename after a dot.

The mtimes of unlinked files are touched to allow simple cron jobs to be used
to clean up files and directories a number of days after deletion, such as the
following:

`find ~/recycled \( -type f -or -type l \) -mtime +30 -delete &&
    find ~/recycled -mindepth 1 -type d -empty -delete`


Arguments
---------

recycle.ko requires the following argument:

### `paths=`*comma separated list of recycle dir paths*

The paths given must be absolute, must exist and must be directories.


Example Usage
-------------

```
$ mkdir -p /tmp/parent/recycled
$ sudo modprobe recycle paths=/tmp/parent/recycled
$ mkdir /tmp/parent/subdir
$ echo test > /tmp/parent/subdir/example
$ rm /tmp/parent/subdir/example
$ ls -l /tmp/parent/subdir
total 0
$ ls /tmp/parent/recycled/subdir/example
/tmp/parent/recycled/subdir/example
$ cat /tmp/parent/recycled/subdir/example
test
```
