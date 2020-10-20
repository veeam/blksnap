# blk-snap

* include/ - libs public headers
* lib/ - lib blk-snap sources
* utils/ - utils for working with snapshot
* module/ - kernel module sources

## About kernel module
This kernel module implements snapshot and changed block tracking functionality.

## Prepare to build
``` bash
sudo apt install gcc cmake uuid-dev libboost-all-dev
```

## How to build
``` bash
mkdir build
cd build
cmake ..
make
```
In directory `bins` you can found utils binaries.




