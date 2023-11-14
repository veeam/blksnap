# Snapshots of block devices using the blksnap kernel module

* [Introduction](#introduction)
* [Features](#features)
* [How it works](#how-it-works)
  - [Change tracking](#change-tracking)
  - [Copy-on-write](#copy-on-write)
  - [Difference storage](#difference-storage)
* [How to use it](#how-to-use-it)
  - [Using ioctl](#using-ioctl)
  - [Static C++ library](#static-c-library)
  - [Blksnap console tool](#blksnap-console-tool)
  - [Regression tests](#regression-tests)
* [License](#license)

## Introduction

There is no novelty in the idea of creating snapshots of block devices. The Linux kernel already has mechanisms for creating snapshots. First of all, this is Device Mapper, which allows you to create persistent and non-persistent snapshots of block devices. There are file systems that support snapshots, such as BTRFS. There are other mechanisms, but they all have their own features. These features do not allow them to be used as a universal tool for creating backups. That is why different backup vendors offer their own kernel modules for creating snapshots.

The blksnap module has been part of the Linux kernel since version 6.?(the patch is under consideration at the time of editing this document). It provides the ability to create non-persistent snapshots on most modern systems without the requirement to change their configuration.

## Features

A change tracker is implemented in the module. It allows to determine which blocks were changed during the time between the last snapshot created and any of the previous snapshots of the same generation. This allows to implement the logic of both incremental and differential backups.

To create images of snapshots of block devices, the module stores blocks of the original block device that have been changed since the snapshot was taken. The blksnap module guarantees the safety of the data of the original block device in case of overflow of the snapshot and even in case of unpredictable critical errors.

A snapshot is created simultaneously for several block devices, ensuring their consistent state in the backup.

The snapshot is created specifically for the block device, and not for the state of the file system. This allows it to be used for cases when a block device is used without a file system, the file system is not supported or is not fully supported.

The listed set of features allows to use it for the purposes of backup and replication of the entire contents of the disk subsystem as a whole. As a result, restoring the system from a backup is much easier.

## How it works

The blksnap module is a block layer filter. It handles all write I/O units. The filter is attached to the block device when the snapshot is created for the first time.
The change tracker marks all overwritten blocks. When creating a snapshot from the change tracker, information about the history of changes on the block device is available.
When handling a request to record the original device, the module reads the blocks that need to be overwritten and stores them in the difference storage. When reading from a snapshot image, reading is performed either from the original device or from the difference storage.

### Change tracking

A change tracker map is created for each block device. One byte of this map corresponds to one block. The block size is set by the module parameters: tracking_block_minimum_shift, tracking_block_maximum_shift and tracking_block_maximum_count. The tracking_block_minimum_shift parameter limits the minimum block size for tracking, while tracking_block_maximum_count defines the maximum allowed number of blocks. The size of the change tracker block is determined depending on the size of the block device when adding a tracking device, that is, when the snapshot is taken for the first time. The block size must be a power of two. However, the block size should not exceed tracking_blocker_maximum_shift. If the block size reaches this value, then their number may exceed tracking_block_maximum_count.

The byte of the change tracking map stores a number from 0 to 255. This is the sequence number of the snapshot for which there have been changes in the block since the snapshot was taken. Each time a snapshot is taken, the number of the current snapshot increases by one. This number is written to the cell of the change tracking map when writing to the block. Thus, knowing the number of one of the previous snapshots and the number of the last one, we can determine from the change tracking map which blocks have been changed. When the number of the current change has reached the maximum allowed value for the map of 255, when creating the next snapshot, the change tracking map is reset to zero, and the number of the current snapshot is assigned the value 1. The tracker of changes is reset and a new UUID — a unique identifier of the generation of snapshots — is generated. The snapshot generation identifier allows to identify that a change tracking reset has been performed.

There are two instances of the change tracker map. One is active, and it tracks the current changes on the block device. The second map is available for reading while the snapshot is being held, and it contains the history of changes that occurred before the snapshot was taken. Maps are synchronized at the moment of taking a snapshot. After the snapshot is released, the second card is not used.

### Copy-on-write

Data is copied in blocks, or rather in chunks. The term "chunk" is used not to confuse it with change tracker blocks and I/O blocks. In addition, the "chunk" in the blksnap module means about the same as the "chunk" in the dm-snap module.
The size of the chunk is determined by the parameters of the module: chunk_minimum_shift, chunk_maximum_shift and chunk_maximum_count. The chunk_minimum_shift parameter limits the minimum chunk size, while chunk_maximum_count defines the maximum allowed number of chunks. The size of the chunks is determined depending on the size of the block device at the time of taking the snapshot. The size of the chunk must be a power of two. However, the chunk size should not exceed chunk_maximum_shift. If the chunk size reaches this value, then their number may exceed chunk_maximum_count.

One chunk is described by the "struct chunk". An array of structures is created for each block device. The structure contains all the necessary information to copy the chunks data from the original block device to the difference storage. The same information allows to create the snapshot image. A semaphore is located in the structure, which allows synchronization of threads accessing to the chunk. While the chunk data is being read from the original block device, the thread that initiated the write request is put into the sleeping state.

There is also a drawback. Since an entire chunk is copied when overwriting even one sector, a situation of rapid filling of the difference storage when writing data to a block device in small portions in random order is possible. This situation is possible in case of great file system fragmentation. At the same time, performance of the machine in this case is severely degraded even without the blksnap module. Therefore, this problem does not occur on real servers, although it can easily be created by artificial tests.

### Difference storage

For one snapshot, a single difference storage is created that is common to all snapshot block devices.
The snapshot difference storage can use:
- a file on a regular file system
- block device
- file on tmpfs.

The file or block device must be opened with the O_EXCL flag to ensure exclusive access of the module to storage.

#### A file on a regular file system

A file on a regular file system can be applied to most systems. For file systems that support fallocate(), a dynamic increase in the difference storage is available. There is no need to allocate a large file before creating a snapshot. The kernel module itself increased its size as needed, but within the specified limit.
However, a file on a regular file system cannot be applied if it is located on a block device for which a snapshot is being created. This means that there must be a file system on the system that is not involved in the backup.

#### Block device

This version of the difference storage allows to get the maximum possible performance, but requires reserved disk space.
Exclusive access to the block device ensures that there are no mounted file systems on it. Dynamic increase of the difference storage does not function in this case. The storage is limited to one block device.

#### File on tmpfs

A file on tmpfs can be applied if there is no free disk space and all file systems participate in the backup. In this case, the difference storage is located in virtual memory, that is, in RAM and the swap file (partition). For low-load systems, this variant may be acceptable. High-load servers may require a swap of a fairly large size, otherwise there may not be enough virtual memory to difference storages, which may lead to an overflow of the snapshot.

## How to use it

In addition to the kernel module, a set of related software was developed under the GPL and LGPL licenses.
The console tool and the C++ library for module management can be used for integration with other projects.

Depending on the needs and the selected license, you can choose different options for managing the module:
1. directly via ioctl
2. using a static C++ library
3. using the blksnap console tool.

### Using ioctl

The kernel includes the header files uapi/linux/blk-filter.h and uapi/linux/blksnap.h. The filter is controlled using ioctl: *BLKFILTER_ATTACH*, *BLKFILTER_DETACH*, *BLKFILTER_CTL*. They allow to attach the filter to a block device, detach it and send control commands to the filter. In addition, the blksnap module creates a /dev/blksnap-control file. With its help, commands for managing snapshots are transmitted to the module. A detailed description of the block device filter interface can be found in the kernel documentation Documentation/block/blkfilter.rst or [online](https://www.kernel.org/doc/html/latest/block/blkfilter.html). Description of the blksnap module interface in the kernel documentation Documentation/block/blksnap.rst or [online](https://www.kernel.org/doc/html/latest/block/blksnap.html).

### Static C++ library

The library was created to simplify the creation of tests in C++. The library is an example of using the module's ioctl interface.

#### class blksnap::CTracker

The сlass *blksnap::CTracker* from ([include/blksnap/Tracker.h](../include/blksnap/Tracker.h)) is a thin C++ wrapper for the block device filter interface, which is implemented in the kernel as an ioctl:
- *BLKFILTER_ATTACH*
- *BLKFILTER_DETACH*
- *BLKFILTER_CTL*.

Methods of the class:
- *Attach* - attachs the block layer filter 'blksnap'
- *Detach* - detach filter
- *CbtInfo* - provides the status of the change tracker for the block device
- *ReadCbtMap* - reads the block device change tracker table
- *MarkDirtyBlock* - sets the 'dirty blocks' of the change tracker
- *SnapshotAdd* - adds a block device to the snapshot
- *SnapshotInfo* - allows to get the snapshot status of a block device.

#### class blksnap::CSnapshot

The class *blksnap::CSnapshot* from ([include/blksnap/Snapshot.h](../include/blksnap/Snapshot.h)) is a thin C++ wrapper for the blknsnap module management interface.
Implements ioctl calls:
- *IOCTL_BLKSNAP_VERSION*
- *IOCTL_BLKSNAP_SNAPSHOT_CREATE*
- *IOCTL_BLKSNAP_SNAPSHOT_COLLECT*
- *IOCTL_BLKSNAP_SNAPSHOT_TAKE*
- *IOCTL_BLKSNAP_SNAPSHOT_DESTROY*
- *IOCTL_BLKSNAP_SNAPSHOT_WAIT_EVENT*.

Static methods of the class:
- *Collect* - allows to get a list of UUIDs of all snapshots of the blksnap module
- *Version* - get the module version
- *Create* - creates an instance of the *blksnap::C Snapshot* class, while the module creates a snapshot to which devices can be added
- *Open* - creates an instance of the *blksnap::CSnapshot* class for an existing snapshot by its UUID.

Methods of the class:
- *Take* - take snapshot
- *Destroy* - destroy snapshot
- *WaitEvent* - allows to receive events about changes in the state of snapshot
- *Id* - requests a snapshot UUID.

#### class blksnap::ISession

The class *blksnap::ISession* from ([include/blksnap/Session.h](../include/blksnap/Session.h)) creates a snapshot session.
The static method *Create* creates an instance of the class that creates, takes and holds the snapshot. The class contains a worker thread that checks the snapshot status and stores them in a queue when events are received. The *GetError* method allows to read a message from this queue. The class destructor destroys the snapshot.

#### class blksnap::ICbt

The class *blksnap::ICbt* from ([include/blksnap/Cbt.h](../include/blksnap/Cbt.h)) allows to access the data of the change tracker.
The static method *Create* creates an object to interact with the block device change tracker.

Метод класса:
- *GetCbtInfo* - provides information about the current state of the change tracker for a block device
- *GetCbtData* - allow to read the table of changes
- *GetImage* - provide the name of the block device for the snapshot image
- *GetError* - allows to check the snapshot status of a block device.

#### struct blksnap::SRange

The struct *blksnap::SRange* from ([include/blksnap/Sector.h](../include/blksnap/Sector.h)) describes the area of the block device, combines the offset from the beginning of the block device and the size of the area in the form of the number of sectors.

#### blksnap::Version

The function *blksnap::Version* from ([include/blksnap/Service.h](include/blksnap/Service.h)) allows to get the version of the kernel module.

### Blksnap console tool

The tool contains detailed built-in help. Calling "blksnap --help" allows you to get a list of commands. When requesting "blksnap \<command name\> --help", a description of the command is output. Page [man](./blksnap.8) may also be useful. Use the built-in documentation.

### Regression tests

The test suite allows regression testing of the blksnap module. The tests are created using bash scripts and C++.
Tests on bash scripts are quite simple. They check the basic basic functionality. Interaction with the kernel module is carried out using the blksnap tool.
C++ tests implement more complex verification algorithms. Documentation for C++ tests is available:
- [boundary](./tests/boundary.md)
- [corrupt](./tests/corrupt.md)

## License

The kernel module, like the Linux kernel, has a GPL-2 license.
The blksnap console tool has a GPL-2+ license.
The libraries are licensed LGPL-3+.
