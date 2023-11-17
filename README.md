| :warning: Important note |
|:---------------------------|
| Master branch is compatible only with latest work for [upstream kernel integration](https://github.com/veeam/blksnap/blob/master/doc/README-upstream-kernel.md#work-in-progress-and-news) |
| For older blksnap version based on external module (actually used in production) see these branches: [VAL-6.1](https://github.com/veeam/blksnap/tree/VAL-6.1), [VAL-6.0](https://github.com/veeam/blksnap/tree/VAL-6.0), [stable-1.0](https://github.com/veeam/blksnap/tree/stable-v1.0) |
| :information_source: To Veeam agent for linux users: |
| If you need only kernel module updated with latest kernel versions support and latest fixes for it [build kernel module](#how-to-build) from [VAL-6.0](https://github.com/veeam/blksnap/tree/VAL-6.0) or [VAL-6.1](https://github.com/veeam/blksnap/tree/VAL-6.1) based on your Veeam agent for linux version |

# BLKSNAP - Block Devices Snapshots

* [Extended description and features](doc/blksnap.md)
* [Repository structure](#repository-structure)
* [Licensing](#licensing)
* [Upstream kernel integration](#kernel-integration)
* [Tools](#tools)
* [Library](#library)
* [Tests](#tests)
  - Details:
    * [Boundary](doc/tests/boundary.md)
    * [Corrupt](doc/tests/corrupt.md)
    * [Diff storage](doc/tests/diff_storage.md)
* [Compatibility notes](#compatibility-notes)
* [Contributing](CONTRIBUTING.md)

## Repository structure

* doc/ - Documentation
* include/ - Libraries public headers
* lib/ - Libraries sources
* patches/ - Patches for the upstream linux kernel
* pkg/ - Scripts for building deb and rpm packages
* tests/ - Test scripts and tests source code
* tools/ - Source files of tools for working with blksnap

## Licensing

Kernel module is GPL-2 only, tools and tests are GPL-2+, library and include are LGPL-3+.

Copyright (C) 2022 Veeam Software Group GmbH

This project use [SPDX License Identifier](https://spdx.dev/ids/) in source files header.

## Kernel integration
For details about the work in progress for integration in upstream kernel see the
specific [README](https://github.com/veeam/blksnap/blob/master/doc/README-upstream-kernel.md)

## Tools
The blksnap tools allows you to manage the module from the command line.
The program allows for execution of individual ioctls of the blksnap module.
The interface of the program may seem inconvenient to the user,
since it is assumed that it will be called by other applications.
### How to build
See [how to build library, tools and tests](#how-to-build-library-tools-and-tests)

## Library
The dynamic C library is not needed to work with blksnap. File
./include/blksnap/blksnap.h contains everything you need to work with blksnap.
But to demonstrate how to call the ioctl, a static c++ library was created.
The library can also help you quickly create a working prototype.
In the project, the library is used for tests.
### How to build
See [how to build library, tools and tests](#how-to-build-library-tools-and-tests)

## Tests
The test scripts are written in bash and use the blksnap tool to control
the blksnap module. The test scripts allow to check the main functions of
the module. To implement complex test algorithms, С++ tests are implemented.
C++ tests use the static library libblksnap.a and it must be compiled to
build С++ tests.
### How to build
See [how to build library, tools and tests](#how-to-build-library-tools-and-tests)

### How to run all usage tests
``` bash
# change working directory to the tests one, for example for debian package is /opt/blksnap/tests
cd /opt/blksnap/tests
# execute all tests script
sudo ./all.sh
# or for logging the output to a file
sudo ./all.sh 2>&1 | tee -a /tmp/blksnap_test_$(date -u '+%Y-%m-%d_%H-%M-%S').log
```

## How to build library, tools and tests
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
cmake .
make
```
Install (but it is recommended to use packages instead, for example the [debian](#how-to-create-dev-tools-and-tests-deb-packages) ones)
```
sudo make install
```
Uninstall (if needed)
```
sudo make uninstall
```

### How to create dev, tools and tests deb packages
``` bash
sudo apt install g++ cmake uuid-dev libboost-program-options-dev libboost-filesystem-dev libssl-dev debhelper
cd ./pkg/deb
./build-blksnap.sh ${VERSION}
```

## Compatibility notes
- blksnap kernel module for upstream can support any arch (other archs beyond X86 archs needs more testing)
- all supported debian and ubuntu supported versions are supported but with some notes:
  - debian 8 and ubuntu 14.04 needs to install cmake 3 from backports to build
