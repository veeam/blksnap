# Integration in upstream kernel

Work for submit patches to upstream it's a lot (see [1](https://docs.kernel.org/process/5.Posting.html "Posting patches") [2](https://docs.kernel.org/process/submitting-patches.html "Submitting patches: the essential guide to getting your code into the kernel") [3](https://docs.kernel.org/process/submit-checklist.html "Linux Kernel patch submission checklist") )
but once completed and integrated upstream it allows to get great results,
like better source code, possible out-of-the-box support, better performance,
better compatibility (for example relating to architectures with upstream is
possible support any instead only X86 ones of the actual external module),
more possibilities about contributions and testing, etc...

## Patches submitted

[Initial patch](https://lore.kernel.org/linux-block/1655135593-1900-1-git-send-email-sergei.shtepa@veeam.com/) was posted at 13 June 2022.

Thanks to many replies, mainly by Christoph Hellwig and Randy Dunlap but not only
many improvements have been made.

[A "v1" patch](https://lore.kernel.org/lkml/20221102155101.4550-1-sergei.shtepa@veeam.com/) was posted at 2 November 2022.

Other improvements was done and a documentation was added as pointed out by answers
outside of the patch posted the integrated documentation is very important
and the comments included in the code were not enough.

The next version of the patch that will be posted is a work in progress but
seems near ready and is available [here](https://github.com/veeam/blksnap/tree/master/patches/lk6.1-rc8-v5)

Is also possible use the [latest branch on Sergei Shtepa linux git](https://github.com/SergeiShtepa/linux/commits/blksnap_lk6.1-rc8_v5)

Latest news is also visible from [here](https://github.com/veeam/blksnap/issues/2)

## Documentation

When blksnap will be integrated upstream the documentation will be available in
https://docs.kernel.org/

For now can be manually generated from kernel source (including the latest
blksnap patch) with `make htmldocs` and after will be visible opening in browser:

Documentation/output/block/blksnap.html

Documentation/output/block/blkfilter.html

## Build

For example an easy and fast way for build kernel packages for Debian and
derivates is:
``` bash
# install prerequisites
sudo apt install wget build-essential bison flex libncurses-dev libssl-dev libelf-dev
# use actual kernel config as base, copy inside the source folder
cp /boot/config-`uname -r` .config
# adapt to lastest kernel version, remember to enable blksnap
make oldconfig
# this will ask for any new config so in major of cases can be better make default for the new
make olddefconfig
# and after enable blksnap from menu
make menuconfig
# and finally build it and make deb packages for easy/fast install it
make deb-pkg
```

**Notes:**

- It can take hour(s) to compile a kernel. Be patient.

- Need also a big free space on disk, at least 50 gb of free space is recommended.

## Contributing

Any contribution is appreciated and useful, is possible contribute in many ways,
not only by reviewing patches and contributing with code improvements and fixes,
but also with documentation, testing and report, etc...
