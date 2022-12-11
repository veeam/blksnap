# BLKSNAP - Block Devices Snapshots Module

* [Extended description and features](doc/blksnap.md)
* [Repository structure](#repository-structure)
* [Licensing](#licensing)
* [Kernel module](#kernel-module)
* [Upstream kernel integration](https://github.com/veeam/blksnap/blob/master/doc/README-upstream-kernel.md)
* [Tools](#tools)
* [Library](#library)
* [Tests](#tests)
  - Details:
    * [Boundary](doc/tests/boundary.md)
    * [Corrupt](doc/tests/corrupt.md)
    * [Diff storage](doc/tests/diff_storage.md)
* [Compatibility notes](#compatibility-notes)

## Repository structure

* doc/ - Documentation
* include/ - Libraries public headers
* lib/ - Libraries sources
* module/ - Sources of kernel module
* patches/ - Patches for the upstream linux kernel
* pkg/ - Scripts for building deb and rpm packages
* tests/ - Test scripts and tests source code
* tools/ - Source files of tools for working with blksnap

## Licensing

Kernel module is GPL-2 only, tools and tests are GPL-2+, library and include are LGPL-3+.

Copyright (C) 2022 Veeam Software Group GmbH

This project use [SPDX License Identifier](https://spdx.dev/ids/) in source files header.


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
cd ./module
mk.sh build
```
In directory current directory you can found bdevfilter.ko and blksnap.ko.

### How to install
``` bash
cd ./module
mk.sh install-flt
mk.sh install
```
### How to create deb package
``` bash
sudo apt install debhelper dkms
cd ./pkg/deb
./build-blksnap-dkms.sh ${VERSION}
```
### How to create rpm package
There are several variants, look in the ./pkg/rpm directory.

## Tools
The blksnap tools allows you to manage the module from the command line.
The program allows for execution of individual ioctls of the blksnap module.
The interface of the program may seem inconvenient to the user,
since it is assumed that it will be called by other applications.
### How to build
Installing the necessary deb packages.
``` bash
sudo apt install g++ cmake uuid-dev libboost-program-options-dev libboost-filesystem-dev
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
cd ./pkg/deb
./build-blksnap-tools.sh ${VERSION}
```

## Library
The dynamic C library is not needed to work with blksnap. File
./include/blksnap/blk_snap.h contains everything you need to work with blksnap.
But to demonstrate how to call the ioctl, a static c++ library was created.
The library can also help you quickly create a working prototype.
In the project, the library is used for tests.
### How to build
Installing the necessary deb packages.
``` bash
sudo apt install g++ cmake uuid-dev libboost-filesystem-dev
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
cd ./pkg/deb
./build-blksnap-dev.sh ${VERSION}
```

## Tests
The test scripts are written in bash and use the blksnap tool to control
the blksnap module. The test scripts allow to check the main functions of
the module. To implement complex test algorithms, С++ tests are implemented.
C++ tests use the static library libblksnap.a and it must be compiled to
build С++ tests.
### How to build
Installing the necessary deb packages.
``` bash
sudo apt install g++ cmake uuid-dev libboost-program-options-dev libboost-filesystem-dev libssl-dev
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
cd ./pkg/deb
./build-blksnap-tests.sh ${VERSION}
```
### How to run all usage tests
``` bash
# change working directory to the tests one, for example for debian package is /opt/blksnap/tests
cd /opt/blksnap/tests
# execute all tests script
sudo ./all.sh
# or for logging the output to a file
sudo ./all.sh 2>&1 | tee -a /tmp/blksnap_test_$(date -u '+%Y-%m-%d_%H:%M:%S').log
```
## Compatibility notes
- blksnap kernel module support kernel versions >= 5.10, support only X86 archs, blksnap for upstream instead can support any arch (other archs need to be tested)
- all supported debian and ubuntu supported versions are supported but with some notes:
  - not all have debian/ubuntu versions have official packages of kernel >= 5.10, so an unofficial or custom ones more updated are needed, with blksnap-dkms should be still possible easy/fast build/install blksnap module on them (is also possible build/install it manually without dkms)
  - debian 8 and ubuntu 14.04 needs to install cmake 3 from backports to build
