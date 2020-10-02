# blk-snap

* include/ - libs public headers
* lib/ - lib blk-snap sources
* utils/ - utils for working with snapshot
* module/ - kernel module sources

## About kernel module
This kernel module implements snapshot and changed block tracking functionality.

## How to build
``` bash
mkdir build
cd build
cmake ..
make
```
In directory `bins` you can found utils binaries.


## Requesting
--------------------------------------------------------------------------
Hello everyone! Requesting for your comments and suggestions.

We propose a new kernel module - blk-snap.

This module implements snapshot and changed block tracking functionality.
It is intended to create backup copies of any block devices without usage
of device-mapper.
Snapshots are temporary and are destroyed after the backup process has
finished. Changed block tracking allows for incremental and differential
backup copies.

blk-snap uses block layer filter API.
Block layer filter API provides a callback to intercept bio-requests.
If a block device disappears for whatever reason, send a synchronous
request to remove the device from filtering.

Previously, we have already offered a patch with the implementation of the
block layer filter api (https://patchwork.kernel.org/patch/11741447/).
Link to that discussion:
https://www.mail-archive.com/linux-kernel@vger.kernel.org/msg2290127.html.
Now we also offer the in-tree module.

blk-snap kernel module is a product of a deep refactoring of the
out-of-tree kernel veeamsnap (https://github.com/veeam/veeamsnap/) module:
* all conditional compilation branches that served for the purpose of
compatibility with older kernels have been removed;
* linux kernel code style has been applied;
* blk-snap mostly takes advantage of the existing kernel code instead of
reinventing the wheel;
* all redundant code (such as persistent cbt and snapstore collector) has
been removed.

We would appreciate your feedback!

Several important things are still have to be done:
* refactoring the module interface for interaction with a user-space code,
it is already clear that the implementation of some calls can be improved;
* haven't yet tested the build on architectures other than x86_64;
* the user-space library is not ready yet and tool to control the module
is to be created;
* autotests need to be created;
* regression testing has been conducted in a cursory manner, so there is
a chance for mistakes.
--------------------------------------------------------------------------
