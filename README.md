# blk_snap

* include/ - libraries public headers
* lib/ - libraries sources
* utils/ - source files of utils for working with blksnap
* module/ - blksnap kernel module sources

## About kernel module
This kernel module implements snapshot and changed block tracking functionality.

## Prepare to build
``` bash
sudo apt install gcc g++ cmake uuid-dev libboost-all-dev libssl-dev linux-headers-generic
```

```
Install
```bash
sudo make install
```

## How to build
Return to blk_snap directory and execute
``` bash
mkdir build
cd build
cmake ..
make
```
In directory `./bins` you can found utils binaries.




