This project should implement the task of creating snapshots of a block
device for an OS based on the Linux kernel for backup purposes.
On the kernel side, the blksnap kernel module should be used.

This project should implement:
1. A library in C for managing the blksnap kernel module
2. A console program for managing snapshots and other features of the
   blksnap module
3. Regression tests for checking the main execution branches of the
   console application, the library and the kernel module
4. Scripts for building packages for deb and rpm
5. Documentation

The blksnap kernel module should provide the following features:
1. Create snapshots of any block devices of the Linux kernel
2. Create snapshots for several block devices simultaneously
3. Track changes on the block devices during the time between the creation
   of snapshots and provide the user level with a map of these changes
4. Ensure data integrity for the block device even in case of critical
   errors in the operation of the library and the blksnap kernel module
5. Allow to use any disk space to store snapshot changes and
   dynamically expand it while holding snapshots of block devices
