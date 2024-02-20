# Integration in upstream kernel

* [Introduction](#introduction)
* [Patches submitted](#patches-submitted)
* [Work in progress and news](#work-in-progress-and-news)
* [Documentation](#documentation)
* [Build](#build)
* [Contributing](#contributing)

## Introduction

Work for submit patches to upstream it's a lot (see [1](https://docs.kernel.org/process/5.Posting.html "Posting patches") [2](https://docs.kernel.org/process/submitting-patches.html "Submitting patches: the essential guide to getting your code into the kernel") [3](https://docs.kernel.org/process/submit-checklist.html "Linux Kernel patch submission checklist") )
but once completed and integrated upstream it allows to get great results,
like better source code, possible out-of-the-box support, better performance,
better compatibility (for example relating to architectures with upstream is
possible support any instead only X86 ones of the actual external module),
more possibilities about contributions and testing, etc...

## Patches submitted

[Initial patch](https://lore.kernel.org/linux-block/1655135593-1900-1-git-send-email-sergei.shtepa@veeam.com/) was posted at 13 June 2022.

Thanks to many replies, mainly by Christoph Hellwig and Randy Dunlap but not only,
many improvements have been made.

[A "v1" patch](https://lore.kernel.org/lkml/20221102155101.4550-1-sergei.shtepa@veeam.com/) was posted at 2 November 2022.

Other improvements was done and a documentation was added as pointed out by answers
outside of the patch posted the integrated documentation is very important
and the comments included in the code were not enough.

Thanks to Fabio Fantoni for his for his participation in the "blksnap" project on github
and Jonathan Corbet for his [article](https://lwn.net/Articles/914031/).

A "v2" patch on [patchwork](https://patchwork.kernel.org./project/linux-block/list/?series=703315) or [lore](https://lore.kernel.org/linux-block/20221209142331.26395-1-sergei.shtepa@veeam.com/) was posted at 9 December 2022.

Is also possible use [this branch](https://github.com/SergeiShtepa/linux/commits/blksnap_lk6.1-rc8_v5) of a linux fork git.

Since then, in collaboration with Christoph, work was carried out to optimize
COW algorithms for snapshots, the algorithm for reading images of snapshots,
and the control interface was redesigned.

A "v3" patch on [patchwork](https://patchwork.kernel.org/project/linux-block/list/?series=737222) or [lore](https://lore.kernel.org/linux-block/20230404140835.25166-1-sergei.shtepa@veeam.com/) was posted at 4 April 2023.

Thanks for preparing v4 patch:
- Christoph Hellwig for his significant contribution to the project.
- Fabio Fantoni for his participation in the project, useful advice and faith in the success of the project.
- Donald Buczek for researching the module and user-space tool. His fresh look revealed a number of flaw.
- Bagas Sanjaya for comments on the documentation.

A "v4" patch ([cover](https://lore.kernel.org/lkml/20230609115206.4649-1-sergei.shtepa@veeam.com/) - [patches](https://lore.kernel.org/lkml/20230609115858.4737-1-sergei.shtepa@veeam.com/)) was posted at 9 June 2023.

[A "v5" patch](https://lore.kernel.org/linux-block/ZIcsijGWeyk%2FFjHs@infradead.org/T/#mc6b9e9bb70021d25decba816766a80fe54911539) done immediately afterwards and which contains the majority of response emails, is only a rebase on latest linux-block, was posted at 12 June 2023.

In the v6, the method of saving snapshot difference has been changed.
Why this should have been done, Dave Chinner described in detail in the [comments to the previous version](https://lore.kernel.org/lkml/20230612135228.10702-1-sergei.shtepa@veeam.com/T/#mfe9b8f46833011deea4b24714212230ac38db978).

A "v6" patch on [lore](https://lore.kernel.org/linux-block/14d5d31e-0dbe-8d04-91a6-82a886f8e92a@veeam.com/T/#t) or [patchwork](https://patchwork.kernel.org/project/linux-block/list/?series=804089&archive=both) was posted at 7 December 2023.

Thanks to Christoph Hellwig attention to the project, it was possible to raise the quality of the code.

A "v7" patch on [lore](https://lore.kernel.org/all/20240209160204.1471421-1-sergei.shtepa@linux.dev/) or [patchwork](https://patchwork.kernel.org/project/linux-block/list/?series=824711&archive=both) was posted at 9 February 2024.

## Work in progress and news

There is a work in progress for "v8",
is possible view/test it using the most updated blksnap-* branch from [this](https://github.com/SergeiShtepa/linux/branches) fork of the linux git.

For testing this version, [the blksnap branch master](https://github.com/veeam/blksnap/tree/master) must be used updated with the latest commits,
to have library, tools and tests working with new upstream version.

Latest news are also visible from [here](https://github.com/veeam/blksnap/issues/2)

## Documentation

When blksnap will be integrated upstream the documentation will be available in
https://docs.kernel.org/

For now can be manually generated from kernel source (that include the latest
blksnap patch) with `make htmldocs` and after will be visible opening in browser:

Documentation/output/block/blksnap.html

Documentation/output/block/blkfilter.html

## Build

For example an easy and fast way for build kernel and its packages for Debian and
derivates is:
``` bash
# install prerequisites
sudo apt install wget build-essential bison flex libncurses-dev libssl-dev libelf-dev dwarves
```
take the actual kernel config as base, you can also take another kernel config,
should be copied as .config inside the kernel source folder
``` bash
cp /boot/config-`uname -r` .config
```
adapt the config to latest kernel version, this will ask for any new options
(remember to enable blksnap)
``` bash
make oldconfig
```
or you can instead automatically set default for all new options
``` bash
make olddefconfig
# and after enable blksnap from menu in "Device drivers"->"Block devices"
make menuconfig
```
build it and make deb packages for easy/fast install/remove it
``` bash
make deb-pkg
```
install the generated packages (value inside < > need to be replaced)
``` bash
sudo dpkg -i linux-image-<version>_<arch>.deb
```
for debug is needed also "linux-image-" with "-dbg", others can be installed if/when needed

**Notes:**

- It can take hour(s) to compile a kernel. Be patient.

- Need also a big free space on disk, at least 50 gb of free space is recommended.

## Contributing

Any contribution is appreciated and useful, is possible contribute in many ways,
not only by reviewing patches and contributing with code improvements and fixes,
but also with documentation, testing and report, etc...
See also [CONTRIBUTING](../CONTRIBUTING.md)
