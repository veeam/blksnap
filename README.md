# blk-snap

* include/ - libs public headers
* lib/ - lib blk-snap sources
* utils/ - utils for working with snapshot
* module/ - kernel module sources

## About kernel module
This kernel module implements snapshot and changed block tracking functionality.

## Prepare to build
``` bash
sudo apt install gcc g++ cmake uuid-dev libboost-all-dev libssl-dev linux-headers-generic
```
## Install Catch2
Download and unpack Catch2
```bash
cd ~/Downloads
wget https://github.com/catchorg/Catch2/archive/v2.13.2.tar.gz
tar -xf ./v2.13.2.tar.gz
```
make
```bash
cd Catch2-2.13.2
mkdir build
cd build
cmake ..
make -j `nproc`
```
Install
```bash
sudo make install
```


## How to build
Return to blk-snap directory and execute
``` bash
mkdir build
cd build
cmake ..
make
```
In directory `./bins` you can found utils binaries.




