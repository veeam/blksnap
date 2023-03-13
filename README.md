# blksnap

* doc/ - documentation
* include/ - libraries public headers
* lib/ - libraries sources
* module/ - blksnap kernel module sources
* patches/ - patches for thelinux kernel
* pkg/ - scripts for building packages
* tests/ - Test scripts and test source codes.
* tools/ - source files of tools for working with blksnap

## Kernel module
This kernel module implements snapshot and changed block tracking functionality.
The module is developed with the condition of simply adding it to the upstream.
Therefore, the module is divided into two parts: bdevfilter and blksnap.
bdevfilter provides the ability to intercept I/O units (bio). The main logic
is concentrated in blksnap. The upstream variant does not contain a bdevfilter,
but accesses the kernel to attach and detach the block device filter.

Relating the work in progress for integration in upstream kernel see the specific [README](https://github.com/veeam/blksnap/blob/master/doc/README-upstream-kernel.md)

### How to build
Installing the necessary deb packages.
``` bash
sudo apt install gcc linux-headers-$(uname -r)
```
Or installing the necessary rpm packages.
``` bash
sudo yum install gcc kernel-devel
```
``` bash
cd ./mobule
mk.sh build
```
In directory current directory you can found bdevfilter.ko and blksnap.ko.

### How to install
``` bash
cd ./mobule
mk.sh install
```
### How to create deb package
``` bash
sudo apt install debhelper dkms
# on debian >=12 and ubuntu >= 23.04 is needed dh-dkms, not installed anymore as dkms dep.
sudo apt install dh-dkms
cd ./pkg/deb
./build-blksnap-dkms.sh ${VERSION}
```
### How to create rpm package
There are several variants, look in the ./pkg/rpm directory.

## blksnap tool
The blksnap tool allows to manage the module from the command line.
The program allows to execute individual ioctls of the blksnap module.
The interface of the program may seem inconvenient to the user,
since it is assumed that it will be called by other applications.
### How to build
Installing the necessary deb packages.
``` bash
sudo apt install g++ cmake uuid-dev libboost-all-dev
```
Or installing the necessary rpm packages.
``` bash
sudo yum install g++ cmake libuuid-devel boost-static libstdc++-static
```
Build.
``` bash
cd ./tools/blksnap
mkdir bin
cd bin
cmake ..
make
```

### How to create deb package
``` bash
sudo apt install debhelper
cd ./pkg/deb/blksnap-tools
build.sh ${VERSION}
```

## library
The dynamic C library is not needed to work with blksnap. File
./include/blksnap/blk_snap.h contains everything you need to work with blksnap.
But to demonstrate how to call the ioctl, a static c++ library was created.
The library can also help you quickly create a working prototype.
In the project, the library is used for tests.
### How to build
Installing the necessary deb packages.
``` bash
sudo apt install g++ cmake uuid-dev libboost-all-dev
```
Or installing the necessary rpm packages.
``` bash
sudo yum install g++ cmake libuuid-devel boost-static libstdc++-static
```
Build.
``` bash
cd ./lib/blksnap
mkdir bin
cd bin
cmake ..
make
```
### How to create deb package
``` bash
sudo apt install debhelper
cd ./pkg/deb/blksnap-dev
build.sh ${VERSION}
```

## tests
The test scripts are written in bash and use the blksnap tool to control
the blksnap module. The test scripts allow to check the main functions of
the module. To implement complex test algorithms, С++ tests are implemented.
C++ tests use the static library libblksnap.a and it must be compiled to
build С++ tests.
### How to build
Installing the necessary deb packages.
``` bash
sudo apt install g++ cmake uuid-dev libboost-all-dev libssl-dev
```
Or installing the necessary rpm packages.
``` bash
sudo yum install g++ cmake libuuid-devel boost-static libstdc++-static openssl-static
```
Build.
``` bash
cd ./tests/cpp
mkdir bin
cd bin
cmake ..
make
```
### How to create deb package
``` bash
sudo apt install debhelper
cd ./pkg/deb/blksnap-tests
build.sh ${VERSION}
```
