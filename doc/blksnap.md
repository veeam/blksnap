# blksnap - module for snapshots of block devices

## Introduction
There is no novelty in the idea of snapshots for block devices. Even the Linux kernel already has mechanisms for creating snapshots of block devices. First of all, this is Device Mapper which allows you to create persistent and non-persistent snapshots of block devices. There are file systems that support snapshots, such as BTRFS. There are others, but they all have their own features. These features do not allow them to be used as a universal tool for creating backups. That is why different backup vendors offer their own kernel modules for creating snapshots. Unfortunately, none of these modules meet the upstream requirements of the Linux kernel.

The blksnap module was created precisely for the purpose of offering it to the upstream. It provides the ability to create non-persistent snapshots on most modern systems without the requirement to change their configuration.

## Features of the blksnap module
A change tracker is implemented in the module. It allows to determine which blocks were changed during the time between the last snapshot created and any of the previous snapshots of the same generation. This allows to implement the logic of both incremental and differential backups.

An arbitrary range of sectors on any block device can be used to store snapshot changes. The size of the change store can be increased after the snapshot is created by adding new sector ranges. This allows to create storage of differences in individual files on a file system that can occupy the entire space of a block device and increase the storage of differences as needed.

To create images of snapshots of block devices, the module stores blocks of the original block device that have been changed since the snapshot was taken. To do this, the module intercepts write requests and reads blocks that need to be overwritten. This algorithm guarantees the safety of the data of the original block device in case of overflow of the snapshot and even in case of unpredictable critical errors.

A snapshot is created simultaneously for several block devices, ensuring their consistent state in the backup.

The snapshot is created specifically for the block device, and not for the state of the file system. This allows it to be used for cases when a block device is used without a file system, the file system is not supported or is not fully supported.

The listed set of features allows to use it for the purposes of backup and replication of the entire contents of the disk subsystem as a whole. As a result, restoring the system from a backup is much easier.

In addition to the kernel module itself, a set of related software has been developed under the GPL and LGPL licenses. The console tool and the C++ library for module management can be used for integration with other projects. The test suite will allow regression testing after module changes, after bug fixes, or after adding new features. The developed documentation is designed to make the study of the module more comfortable.

## How it works
The blksnap module is a block layer filter. It handles all write I/O requests.

The filter is attached to the block device when the snapshot is created for the first time.

The change tracker marks all overwritten blocks. When creating a snapshot, information about the history of changes on the block device is available.

The module reads the blocks that need to be overwritten and stores them in the change store. When reading from a snapshot image, reading is performed either from the original device or from the change store.

### Change tracking
A change tracker map is created for each block device. One byte of this map corresponds to one block. The block size is set by the module configuration parameters: tracking_block_minimum_shift and tracking_block_maximum_count. The tracking_block_minimum_shift parameter limits the minimum block size for tracking, while tracking_block_maximum_count defines the maximum allowed number of blocks. The default values for these parameters are determined by the module configuration declarations: CONFIG_BLK_SNAP_TRACKING_BLOCK_MINIMUM_SHIFT and CONFIG_BLK_SNAP_TRACKING_BLOCK_MAXIMUM_COUNT. The size of the change tracker block is determined depending on the size of the block device when adding a tracking device, that is, when the snapshot is taken for the first time. The block size must be a power of two.

The byte of the change tracking map stores a number from 0 to 255. This is the sequence number of the snapshot for which there have been changes in the block since the snapshot was taken. Each time a snapshot is taken, the number of the current snapshot increases by one. This number is written to the cell of the change tracking map when writing to the block. Thus, knowing the number of one of the previous snapshots and the number of the last one, we can determine from the change tracking map which blocks have been changed. When the number of the current change has reached the maximum allowed value for the map of 255, when creating the next snapshot, the change tracking map is reset to zero, and the number of the current snapshot is assigned the value 1. The tracker of changes is reset and a new UUID — a unique identifier of the generation of snapshots — is generated. The snapshot generation identifier allows to identify that a change tracking reset has been performed.

The change map has two copies. One is active, and it tracks the current changes on the block device. The second one is available for reading while the snapshot is being held, and it contains the history of changes that occured before the snapshot was taken. Copies are synchronized at the moment of taking a snapshot. After the snapshot is released, a second copy of the map is not needed, but it is not released, so as not to allocate memory for it again the next time the snapshot is created.

### Copy-on-write
Data is copied in blocks, or rather in chunks. The term "chunk" is used not to confuse it with change tracker blocks and I/O blocks. In addition, the "chunk" in the blksnap module means about the same as the "chunk" in the dm-snap module.
The size of the chunk is determined by the parameters of the module: chunk_minimum_shift and chunk_maximum_count. The chunk_minimum_shift parameter limits the minimum chunk size, while chunk_maximum_count defines the maximum allowed number of chunks. The default values for these parameters are determined by the module configuration declarations: CONFIG_BLK_SNAP_CHUNK_MINIMUM_SHIFT and CONFIG_BLK_SNAP_CHUNK_MAXIMUM_COUNT. The size of the chunks is determined depending on the size of the block device at the time of taking the snapshot. The size of the chunk must be a power of two.

One chunk is described by the &struct chunk structure. An array of structures is created for each block device. The structure contains all the necessary information to copy the chunks data from the original block device to the difference storage. The same information allows to create the snapshot image. A semaphore is located in the structure, which allows synchronization of threads accessing to the chunk. While the chunk data is being read from the original block device, the thread that initiated the write request is put into the sleeping state.

The feature of the block layer filter is that when handling an I/O request, synchronous I/O requests cannot be executed. Synchronous requests can cause stack overflow in the case of a recursive call to submit_bio_noacct(), and also increase the chance of a mutual blocking situation. Therefore, before calling the filter callback function, the variable current->bio_list is initialized, and the submit_bio_noacct() function in this case adds an I/O request to this linked list. After executing the request processing callback function, all requests are extracted from the current->bio_list list and submit_bio_noacct() is called for them. Therefore, the copy-on-write algorithm is performed asynchronously.

The block layer has another feature. If a read request is sent, and then a write request is sent, then write can be performed first, and only then a read. Even using the REQ_SYNC and REQ_PREFLUSH flags does not provide correct behavior. Maybe it's a mistake, I'm not sure.

Due to these features, the current implementation assumes a repeated entry into the handler. When the callback function is called for the first time, the semaphore of the chunk is locked and a linked list of requests reading the rewritable chunk is formed, after which the block layer passes them to execution and calls the filter callback function again. When the callback function is called for the second time, when trying to lock the chunk semaphore, the process is locked until the chunk data is read from the original block device. If, when executing the callback function, it becomes clear that the data of the chunk has already been copied, then the write request is simply skipped without any delay.

This algorithm allows to efficiently perform backups of systems that run Round Robin Database [RRD](https://www.loriotpro.com/Products/On-line_Documentation_V5/LoriotProDoc_EN/V22-RRD_Collector_RRD_Manager/V22-A1_Introduction_RRD_EN.htm). Such databases can be overwritten several times during backup of the machine. Of course, the value of the RRD data backup of the monitoring system can be questioned. However, it is often a task to make a backup copy of the entire enterprise infrastructure in full, so that to restore or replicate it entirely in case of problems.

But there is also a drawback. Since an entire chunk is copied when overwriting even one sector, a situation of rapid filling of the difference storage when writing data to a block device in small portions in random order is possible. This situation is possible in case of great file system fragmentation. At the same time, performance of the machine in this case is severely degraded even without the blksnap module. Therefore, this problem does not occur on real servers, although it can easily be created by artificial tests.

### Difference storage
Before considering how the blksnap module organizes the difference storage, let's look at other similar solutions.

BTRFS implements snapshots at the file system level. If a file is overwritten after the snapshot is taken, then the new data is stored in new blocks. The old blocks remain and are used for the snapshot image. Therefore, if the snapshot overflows, there is no space to store the new up-to-date data, and the system loses its operability.

Device Mapper implements snapshot support using dm-snap. It implements the logic of a snapshot of a block device. The copy-on-write algorithm is about the same as that of the blksnap module. Before overwriting the data on the original device, it is read and stored in a block device specially allocated for this purpose. In practice, this means that when taking a snapshot from several block devices, you need to have one or several empty block devices, and need to allocate areas on them for each device from which the snapshot is taken. The first problem is that the system may not have free disk space for storing differences. If there is free space on the disk, then the question arises: "Is there enough free disk space, and how to divide it between block devices?". You can divide this space equally or proportionally to the size. But the load on different disks is usually unevenly distributed. As a result, snapshot overflow occurs for one of the block devices, while for the others all the reserved space may remain free. It turns out that the reserved space is used suboptimally.

The difference storage of the blksnap module does not have the listed disadvantages.
1. Copying when writing differences to the storage saves the old data needed for snapshot images. Therefore, when the snapshot is overflowing, the snapshot images become inconsistent, the backup process fails, but the system remains operational without data loss.
2. The difference storage is common to all block devices in the snapshot. There is no need to distribute the difference storage area between block devices.
3. The difference storage is a pool of disk space areas on different block devices. That is, the load of storing changes can be distributed.
4. There is no need to allocate large disk space immediately before taking a snapshot. Even while the snapshot is being held, the difference storage can be expanded.

Thanks to the listed features of the blksnap module difference storage, we do not need to allocate free disk space in advance. It is enough to have free space on the file system. Areas of disk space can be allocated using fallocate(). Unfortunately, not all file systems support this system call, but the most common XFS and EXT4 support it (as well as BTRFS, but conversion of virtual offsets to physical ones is required). When holding a snapshot, user-space process can poll its status using a special ioctl. When free space in the difference storage runs out, the module notifies the user process about this, and the user process can prepare a new area and transfer it to the module to expand the difference storage.

## How to use it
Depending on the needs and the selected license, you can choose different options for managing the module:
1. Directly via ioctl
2. Using a static C++ library
3. Using the blksnap console tool

### Using ioctl
The kernel module provides the blk_snap.h header file. It describes all available ioctls and structures for interacting with the module. Each ioctl and structure is documented in detail. This should be enough to understand the interface. If you think that something is missing, I suggest expanding the description in the header file.
In addition, the libblksnap library and the blksnap tool use this interface, so in case of difficulty, you can use their source code as an example.

### Static C++ library
The library was created primarily to simplify creation of tests in C++. This is also a good example of using the ioctl interface of the module. You can use it directly in the GPL-2+ application or make an LGPL-2+ library, with which a proprietary application will be dynamically linked.

The library interface is quite simple and intuitive. There are tests that apply it. They can be an example of using the library. Therefore, I will briefly describe only the purpose of the key classes.
* The blksnap::CBlksnap (include/blksnap/Blksnap.h) class is a thin C++ wrapper for the ioctl interface of the kernel module. Abstraction at this level may be enough for you.
* The blksnap::Session (include/blksnap/Session.h) interface. Its static Create() method creates a snapshot object and holds it. The snapshot is released when this object is released. The object itself contains implementation of the algorithm for allocating new portions for the repository of changes and processing events from the module. For the method to work, it is enough to create a session and use the GetImageDevice() call to get the name of the snapshot image block device. It is very suitable for quick prototyping of an application.
* The blksnap::ICbt (include/blksnap/Cbt.h) interface allows to access the change tracker data.
* The include/blksnap/Service.h file contains the function of getting the kernel module version and may contain other functionality for debugging the module.

### blksnap console tool
In accordance with the "the best documentation is code" paradigm, the tool contains a detailed built-in help. Calling "blksnap --help" allows to get a list of commands. When requesting "blksnap \<command name\> --help", a description of the command is output. If you think that this documentation is not enough, I suggest adding the necessary information to the built-in help system of the tool.

If you still have questions about how to use the tool, then you can use tests written in bash as an example.

## What's next
* Ensure the operability of the module and related software on different architectures. At the moment, it is tested to work on amd64. Testing on the arm64le architecture was quite successful. A system crash was found on the ppc64le architecture. Testing on other architectures has not been performed yet.
* For the blksnap console tool, add the ability to return data in the form of a json structure. This should facilitate integration with other programs, such as written on pythons.
* For the blksnap console tool, it would be great to make a man page.
