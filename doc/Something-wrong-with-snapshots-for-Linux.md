#Something Wrong with Snapshots for Linux

Hi all!

It would seem that snapshots have existed in Linux for a long time.
There is a Device Mapper and its [snapshots](https://docs.kernel.org/admin-guide/device-mapper/snapshot.html).
There are BTRFS with [snapshot](https://lwn.net/Articles/579009/) support.
Why does the developer of a popular backup tool offer a ["new bike"](https://lore.kernel.org/linux-block/20221209142331.26395-1-sergei.shtepa@veeam.com/)?
I propose to understand the reasons.

To do this, let's look at the problem through the eyes of a backup developer. Moreover, backup tools that are targeted at the enterprise user. First, let's try to understand how a "enterprise user" differs from a "home user".

## Enterprise/Home User Differences

Suppose your HDD or SSD is broken on your PC. To restore the system to a new SSD, you will install a fresh version of your favorite distribution, add only the most necessary applications (install the rest as needed), perhaps use your favorite scripts to configure environment. After that, you will restore the data from the /home/ section from the archive. If suddenly the new system works a little differently - I'm sure you will quickly find the problem and fix it. In this case, to restore your sysmen, you only need a file backup of unique personal data to your NAS, at best to a Cloud repository. You are a typical home user.

Suppose you are the head of an enterprise. And a crypto-virus appeared in your company's network, or a natural disaster like a fire or a tsunami physically destroyed your data center. The entire infrastructure was hit, but the backup repository survived, as it was isolated from the general network and was located in a more secure location (in a neighboring city, country, or even on an orbital datacenter). In this case, the entire infrastructure of the enterprise needs to be restored as soon as possible, since every hour of downtime brings enormous losses. Therefore, you do not have time to wait for system administrators to install the OS, install the required applications and make the necessary configuration of each server. It is necessary to return the state of the entire infrastructure to the moment "when everything was working", and in the shortest possible time. Therefore, a file backup does not suit you. You need a backup of "Entire machine". You are a typical enterprise user.

And now let's try to answer the question: "What does a enterprise user require from a backup tool?".

## Requirements for the backup tool

### Universality

Different enterprises choose different schemes for organizing their IT infrastructure. Even the same company in the course of its growth can change its strategy. As a result, we have to maintain a huge zoo of different configurations of IT infrastructures. Virtual machines on ESX or Hyper-V are popular. Virtualization based on Qemu+KVM can be used. Don't forget about XEN. Often there are just servers without virtualization, although 15 years ago some people believed that such configurations would die out like dinosaurs. Not extinct. They live for decades. Now the boom of various cloud solutions continues.
ESX and Hyper-V hypervisors offer the ability to create a snapshot of a virtual machine that can be used for backup. Everything else can be backed up only using the built-in tools of the operating system.

The spread of Linux distributions is also wide, and with them, the principles of partitioning disk space may vary.
Therefore, servers without LVM markup on disks, or without BTRFS exist. Moreover, to use dm-snap snapshots, we need to reserve free disk space on the server without a file system. Judging from my experience, such a configuration is rare. BTRFS has supporters and opponents. I won't go into details, this is a topic for a "holy war".

Thus, a significant part of the servers remain without snapshots. Of course, you can offer customers "Here, let's update the configurations of your entire couple of hundred servers so that it meets the requirements of our backup tool." I doubt the success of this approach.

### Reliability

The corporate user's server should work 24/7. Stopping the server during the backup is unacceptable. And even if there is a malfunction in the operation of the backup tool, this should not noticeably affect the operation of the server.

### Minimal consumption of system resources

Modern servers can boast of tens of terabytes of disk space. At the same time, it is very desirable that the backup has time to be performed overnight, during the minimum load on the infrastructure. This problem is solved by the presence of a change tracker. It allows to create an incremental backup, when only changed blocks are read.

### Minimum recovery or replication time

A modern backup tool should allow to restore the entire IT infrastructure of the enterprise in the shortest possible time. This can be achieved only by eliminating a person from the recovery process. A good recovery plan is when you have a "restore everything" button.

## Compliance with the requirements

And now let's compare which tools meet the listed requirements.

### BTRFS

Obviously, BTRFS does not have a means of universality. But alas, this is not its only drawback.

There are reliability issue. If a snapshot is created on the file system, or maybe even two, then a situation of lack of disk space may arise. In this case, the user's applications will be refused when trying to write data, and the server will no longer be able to work. This situation is unacceptable for a corporate user.

The consumption of system resources is excessive. Despite the fact that BTRFS supports [incremental backups](https://btrfs.wiki.kernel.org/index.php/Incremental_Backup), in order to use it, we will have to store a snapshot from the previous backup on the system. This increases the need for disk space. Taking into account the reliability issue, the additional consumption of disk space can lead to sad consequences.

When restoring, we have to create a file system again and restore files in it. This reduces the recovery rate.

### DM snapshot

A DM snapshot can only be created for DM devices, which means that there must be LVM markup on the disk. Therefore, such a solution does not have the property of universality. The organization of the difference storage has disadvantages, since it requires allocating free disk space in advance and distributing it among all block devices for which a snapshot is created.

Minimizing the consumption of system resources during backup is not ensured, since during backup we have to reread the entire block device.

The remaining requirements are provided.

### Blksnap

The blksnap module allows  to ensure requirement of universality almost completely, as it allows to create snapshots for any block devices, including for DM devices, regardless of the file system. However, there are difficulties with restoring BTRFS from such a backup. Cluster file systems are not supported.

The blksnap allows to organize a difference storage on any free disk space and even from blocks of files on an existing file system. The difference storage is shared by all block devices for which the image is being created. As a result, the module can be used for almost any servers.

The high reliability of the blksnap module is provided by the COW algorithm. When I/O unit are handled, like in DM snapshot, the data for displaying the snapshot is copied to the difference storage, and the actual data is located in its place. This algorithm ensures that the user's data will remain intact, even if a kernel crash occurs while holding the snapshot.

It is possible to expand the difference storage dynamically already while holding the snapshot. However, if the free disk space remains less than the allowable limit, then the difference storage cannot be expanded. As a result, a snapshot overflow situation occurs, which leads to the release of the difference storage. Of course, the backup process is interrupted with an error, but the server continues to work.

Minimal consumption of system resources is provided by the change tracker. Thanks to it, the backup time is reduced, which means that electricity consumption is also reduced.

Minimal recovery time is possible due to copying block devices entirely. The entire system gets into the backup completely, with the exception of the SWAP section and the contents of NVRAM. When restoring, it remains only to recreate the partition table and restore the contents of the partitions.

## Most popular questions

If I could convince you that there is something wrong with the existing snapshots for Linux, then let's try to answer the most popular questions.

### Why not DM?

Of course you will say: "Why write a new bike! You just need to modify the existing one! Code duplication.". To be honest, I also thought so...
I tried to go through [this path](https://lore.kernel.org/linux-block/1611853955-32167-1-git-send-email-sergei.shtepa@veeam.com/).

To move in this direction, you need the interest of the DM maintainer (and his employer, I think). A significant change in well-debugged code is required. The costs of reviewing, testing, and correcting documentation are required. Unexpected errors may appear, for sure it will add new cases from users, and the team dm-devel@redhat.com we will have to support the new functionality.

I couldn't get far enough in this direction, although my prototype blk_interposer worked quite well on my machine.
I'm not the only one who has tried to interact with the DM folks on this issue. I really liked [this comment](https://lwn.net/Articles/914908/). I agree with him.
For myself, I decided that the DM is good enough to try to make it even better.

### And are the interests of other backup vendors taken into account?

The answer is both "yes" and "no" at once.
On the one hand, there are no formal agreements. The blksnap module is an initiative of Veeam Software developer.
On the other hand, I am very glad that Fabio Fantoni has joined the project. His advice allows me to look at some issues from a different point of view. Therefore, we can assume that the interests of [M2Rbiz](https://github.com/M2Rbiz) are taken into account.
There is also an open [backup tools](https://github.com/cloudbase/coriolis-snapshot-agent) on github that already use the blksnap module. The project is still under development, but I hope that the folks will succeed.

The blksnap module is documented in some detail, there are libraries, tools and tests for it. Take a look at the [blksnap](https://github.com/veeam/blksnap/) project. I am sure that it will not be difficult for backup vendors to support the operation of their products with the blksnap module. Therefore, I think that their interests are taken into account.

### Do you drag your hook into the block layer, and the module in the kernel will be abandoned?

Using a module from the kernel will allow to raise the quality of service for users of backup tools to a new level.
1. There are organizations whose security policies prohibit the installation of out-of-tree kernel modules. Now we are able to ensure the operation of our product on such servers with limited functionality. There will be no such limitations with a module in the kernel.
2. Distributors who provide technical support to their users refuse to consider kernel crash cases if an out-of-tree module is detected on the system. With the module in the kernel, users are guaranteed to be able to receive technical support.
3. Guaranteed operation of the module when updating the kernel from upstream and "Frankensteins" from distributors. The Kernel API is constantly changing and improving. This is great, of course, but in the out-of-tree module it looks like a pile of conditional compilation directives. Each release of the upstream kernel or the kernel from the distributor turns into a lottery: whether it will be assembled or not, whether to drop the kernel or not. In the case of in-tree module maintenance, all changes can be performed in a timely manner, changes can be tested, critical issues can be fixed at the RC stage, and distributors will distribute the module assembled with the kernel, which will eliminate problems, for example, with KABI compatibility such as [this](https://access.redhat.com/solutions/6985596).

## Conclusion

I am sure that a module for creating snapshots of block devices in the Linux kernel is necessary. I hope I was able to convince you of this.
Any help, advice or criticism is welcome.
I will be happy to answer all your questions.

The publication of an article with an indication of the author and a link to the original source is welcome.
