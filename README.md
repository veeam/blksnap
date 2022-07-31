# blksnap

* doc/ - documentation
* include/ - libraries public headers
* lib/ - libraries sources
* module/ - blksnap kernel module sources
* patches/ - patches for thelinux kernel
* pkg/ - scripts for building packages
* tests/ - Test scripts and test source codes.
* tools/ - source files of tools for working with blksnap

## kernel module blksnap
This kernel module implements snapshot and changed block tracking functionality.
The module is developed with the condition of simply adding it to the upstream.
Therefore, the module is divided into two parts: bdevfilter and blksnap.
bdevfilter provides the ability to intercept I/O units (bio). The main logic
is concentrated in blksnap. The upstream variant does not contain a bdevfilter,
but accesses the kernel to attach and detach the block device filter.

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
mk.sh install-flt
mk.sh install
```
### How to create deb package
``` bash
sudo apt install debhelper dkms
cd ./pkg/deb/blksnap-dkms
./build.sh ${VERSION}
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
cd ./pkg/deb/blksnap-tools
./build.sh ${VERSION}
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
cd ./pkg/deb/blksnap-dev
./build.sh ${VERSION}
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
cd ./pkg/deb/blksnap-tests
./build.sh ${VERSION}
```
### How to run all usage tests
``` bash
./all.sh
# or if installed with packages
/opt/blksnap/tests/all.sh
# or for logging the output to a file
/opt/blksnap/tests/all.sh | tee -a blksnap_test_$(date -u '+%Y-%m-%d_%H:%M:%S').log
```
