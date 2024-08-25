recycle.ko
==========

A Linux kernel module which moves all files unlinked under a root directory to
a recycle directory.

This module was inspired by the [Samba vfs_recycle
module](https://www.samba.org/samba/docs/current/man-html/vfs_recycle.8.html)
but works with directory trees shared via NFS (or not shared at all).

It works by using ftrace to hook the `security_inode_unlink` function (which is
called before the kernel performs an unlink) and adding a hard-link to the
inode of the file about to be deleted to an equivalent path under the recycle
directory.

It is probably worth bearing in mind that this is my first kernel module so I
can't guarantee that it won't crash your machine or eat all of your data etc.
However, I do run it on the server which stores my personal files - hopefully
that demonstrates my confidence.

Note that this module hasn't been implemented to work with delegated inodes
(e.g. directories mounted via NFS on a client), I'd recommend using it on the
NFS server side instead.
