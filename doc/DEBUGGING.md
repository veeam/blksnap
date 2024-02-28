## Decode kernel call trace

The kernel call trace need to be decoded with debug symbols and source code of
the exact build of the kernel used and that have generated the call trace.
With decoded call trace and the corresponding source code, a developer can found
the issue and solve it. In many cases there are multiple call trace and often
the first one that is the cause (or connected to it) is important, while the
others are consequences, if in doubt, it is better to decode and report all
them. In some cases the decoded call traces are not enough and further
logs and/or details are still needed.

For simply decode kernel call trace there is a specific script in the
kernel source: scripts/decode_stacktrace.sh.
Must be passed as parameters:
- the kernel with debug symbols must be passed as parameters (usually those used in production must therefore use the debug one of the same kernel build)
- the kernel source tree (the identical source from which the kernel build that generated the call trace was made)
- the full text of call trace to debug, usually starts with:
  
  "------------[ cut here ]------------"
  
  and end something like:
  
  "---[ end trace c1947abfeca4e04a ]---"
  
  decode works also with additional logs and multiple call trace
  
  to make simply put it in file and pass the file
The decoded call trace will be given as output, to make simply is possible to save on file.

``` bash
cd <kernel-source-tree>/scripts/
./decode_stacktrace.sh <kernel-image-with-debug-symbols> <kernel-source-tree> < stacktrace.log > stacktrace-decoded.log
```

# Example of debug blksnap module call trace on ubuntu using official kernel build

First is needed add [debug repository](https://wiki.ubuntu.com/Debug%20Symbol%20Packages) (not included by default) and kernel debug packages of the same build:


``` bash
echo "deb http://ddebs.ubuntu.com $(lsb_release -cs) main restricted universe multiverse
deb http://ddebs.ubuntu.com $(lsb_release -cs)-updates main restricted universe multiverse
deb http://ddebs.ubuntu.com $(lsb_release -cs)-proposed main restricted universe multiverse" | \
sudo tee -a /etc/apt/sources.list.d/ddebs.list

sudo apt install ubuntu-dbgsym-keyring
sudo apt update
sudo apt install linux-image-`uname -r`-dbgsym # !! will require big amount of disk space, in this case near 7 gb
```

After the source code of same build is needed, if latest version of default kernel is used is possible to have the source with applied patches easy and fast:

``` bash
# Note: before execute this is needed enable source code repository if not already done
apt source linux
```

If other version of kernel was used is needed to download the specific source and make sure to apply all the patches used in the build

With debug kernel package installed and with source code is now possible to decode call trace.
The script to decode is also in linux-headers package.
Here one example of decode:

``` bash
cd /usr/src/linux-headers-5.15.0-60-generic/scripts/
./decode_stacktrace.sh /usr/lib/debug/boot/vmlinux-5.15.0-60-generic /tmp/linux-5.15.0/ < /tmp/stacktrace.log >/tmp/stacktrace-decoded.log
```
<details>

<summary>Example of call trace</summary>

```
[  951.558734] WARNING: CPU: 6 PID: 36678 at block/blk-mq.c:2335 blk_mq_free_rqs+0x1af/0x1c0
[  951.558747] Modules linked in: blksnap(OE) bdevfilter(OEK) nvidia_uvm(POE) nvidia(POE) rpcsec_gss_krb5 mpt3sas raid_class scsi_transport_sas mptctl mptbase dell_rbu nft_chain_nat xt_REDIRECT xt_MASQUERADE xt_owner xt_nat nf_nat nft_counter xt_LOG nf_log_syslog xt_state xt_conntrack nf_conntrack nf_defrag_ipv6 nf_defrag_ipv4 xt_tcpudp nft_compat nf_tables nfnetlink binfmt_misc intel_rapl_msr rc_tt_1500 snd_hda_codec_hdmi ts2020 intel_rapl_common snd_hda_intel m88ds3103 sb_edac snd_intel_dspcfg snd_intel_sdw_acpi snd_hda_codec i2c_mux x86_pkg_temp_thermal intel_powerclamp coretemp snd_hda_core kvm_intel dvb_usb_dw2102 dvb_usb dvb_core snd_hwdep snd_pcm joydev mc input_leds snd_timer kvm snd soundcore ipmi_ssif rapl dcdbas intel_cstate mei_me mei mac_hid acpi_power_meter ipmi_si sch_fq_codel ipmi_watchdog ipmi_devintf ipmi_msghandler 8021q garp mrp stp llc nfsd parport_pc ppdev auth_rpcgss nfs_acl lockd lp grace parport sunrpc ramoops efi_pstore reed_solomon pstore_blk pstore_zone
[  951.558832]  ip_tables x_tables autofs4 btrfs blake2b_generic zstd_compress dm_crypt raid10 raid456 async_raid6_recov async_memcpy async_pq async_xor async_tx xor raid6_pq libcrc32c raid1 raid0 multipath linear bonding tls mgag200 drm_kms_helper syscopyarea sysfillrect sysimgblt fb_sys_fops cec crct10dif_pclmul hid_generic crc32_pclmul ghash_clmulni_intel rc_core usbhid cdc_ether aesni_intel usbnet igb crypto_simd mii ahci hid dca cryptd drm lpc_ich libahci megaraid_sas i2c_algo_bit wmi
[  951.558878] CPU: 6 PID: 36678 Comm: peer local sock Tainted: P        W  OE K   5.15.0-60-generic #66-Ubuntu
[  951.558882] Hardware name: Dell Inc. PowerEdge R720/0XH7F2, BIOS 2.9.0 12/06/2019
[  951.558884] RIP: 0010:blk_mq_free_rqs+0x1af/0x1c0
[  951.558890] Code: 40 08 e8 44 6b d1 ff 4c 8b 45 d0 49 8b 80 a0 00 00 00 4c 39 e8 75 c4 48 83 c4 18 5b 41 5c 41 5d 41 5e 41 5f 5d c3 cc cc cc cc <0f> 0b e9 5c ff ff ff 66 2e 0f 1f 84 00 00 00 00 00 0f 1f 44 00 00
[  951.558893] RSP: 0018:ffffbc372058fd10 EFLAGS: 00010286
[  951.558895] RAX: ffff980237f70180 RBX: ffff980237f70000 RCX: ffff980237f80000
[  951.558898] RDX: 0000000000000021 RSI: ffff980222fd8508 RDI: 00000000c0000000
[  951.558900] RBP: ffffbc372058fd50 R08: ffff98022527e840 R09: fffff5d897dfdc00
[  951.558902] R10: 0000000000000030 R11: ffff800000000000 R12: ffff980ad94b0c58
[  951.558904] R13: ffff98022527e8e0 R14: ffff98022527ed80 R15: 0000000000000000
[  951.558906] FS:  00007f48957fa640(0000) GS:ffff98086f8c0000(0000) knlGS:0000000000000000
[  951.558909] CS:  0010 DS: 0000 ES: 0000 CR0: 0000000080050033
[  951.558911] CR2: 000055793d24a0d0 CR3: 0000000c8c1e4002 CR4: 00000000000606e0
[  951.558914] Call Trace:
[  951.558916]  <TASK>
[  951.558921]  ? mutex_lock+0x13/0x50
[  951.558927]  blk_mq_sched_free_requests+0x3f/0x60
[  951.558932]  blk_cleanup_queue+0xc0/0xf0
[  951.558937]  blk_cleanup_disk+0x16/0x40
[  951.558942]  snapimage_free+0x9e/0x170 [blksnap]
[  951.558951]  snapshot_free+0x67/0x330 [blksnap]
[  951.558957]  snapshot_destroy+0x10b/0x150 [blksnap]
[  951.558964]  ? ioctl_snapshot_take+0x80/0x80 [blksnap]
[  951.558970]  ioctl_snapshot_destroy+0x3e/0x80 [blksnap]
[  951.558976]  ctrl_unlocked_ioctl+0x78/0xd0 [blksnap]
[  951.558982]  __x64_sys_ioctl+0x95/0xd0
[  951.558989]  do_syscall_64+0x5c/0xc0
[  951.558993]  ? syscall_exit_to_user_mode+0x27/0x50
[  951.558997]  ? __x64_sys_write+0x19/0x20
[  951.559000]  ? do_syscall_64+0x69/0xc0
[  951.559003]  ? do_syscall_64+0x69/0xc0
[  951.559006]  ? exc_page_fault+0x89/0x170
[  951.559010]  entry_SYSCALL_64_after_hwframe+0x61/0xcb
[  951.559013] RIP: 0033:0x7f495e6f2aff
[  951.559018] Code: 00 48 89 44 24 18 31 c0 48 8d 44 24 60 c7 04 24 10 00 00 00 48 89 44 24 08 48 8d 44 24 20 48 89 44 24 10 b8 10 00 00 00 0f 05 <41> 89 c0 3d 00 f0 ff ff 77 1f 48 8b 44 24 18 64 48 2b 04 25 28 00
[  951.559020] RSP: 002b:00007f48957f9730 EFLAGS: 00000246 ORIG_RAX: 0000000000000010
[  951.559023] RAX: ffffffffffffffda RBX: 000000000354cbb0 RCX: 00007f495e6f2aff
[  951.559025] RDX: 00007f48957f9890 RSI: 0000000080105606 RDI: 0000000000000037
[  951.559026] RBP: 000000000354cb80 R08: 00007f48600068b0 R09: 0000000000000000
[  951.559028] R10: 00007f48957f9780 R11: 0000000000000246 R12: 000000000354cbd8
[  951.559030] R13: 00007f48957f97d0 R14: 000000000354cba8 R15: 00007f48957f97cf
[  951.559033]  </TASK>
```
</details>

<details>

<summary>Example of the same  call trace decoded</summary>

```
[  951.558734] WARNING: CPU: 6 PID: 36678 at block/blk-mq.c:2335 blk_mq_free_rqs (/build/linux-25O3Ed/linux-5.15.0/block/blk-mq.c:2335 /build/linux-25O3Ed/linux-5.15.0/block/blk-mq.c:2369) 
[  951.558747] Modules linked in: blksnap(OE) bdevfilter(OEK) nvidia_uvm(POE) nvidia(POE) rpcsec_gss_krb5 mpt3sas raid_class scsi_transport_sas mptctl mptbase dell_rbu nft_chain_nat xt_REDIRECT xt_MASQUERADE xt_owner xt_nat nf_nat nft_counter xt_LOG nf_log_syslog xt_state xt_conntrack nf_conntrack nf_defrag_ipv6 nf_defrag_ipv4 xt_tcpudp nft_compat nf_tables nfnetlink binfmt_misc intel_rapl_msr rc_tt_1500 snd_hda_codec_hdmi ts2020 intel_rapl_common snd_hda_intel m88ds3103 sb_edac snd_intel_dspcfg snd_intel_sdw_acpi snd_hda_codec i2c_mux x86_pkg_temp_thermal intel_powerclamp coretemp snd_hda_core kvm_intel dvb_usb_dw2102 dvb_usb dvb_core snd_hwdep snd_pcm joydev mc input_leds snd_timer kvm snd soundcore ipmi_ssif rapl dcdbas intel_cstate mei_me mei mac_hid acpi_power_meter ipmi_si sch_fq_codel ipmi_watchdog ipmi_devintf ipmi_msghandler 8021q garp mrp stp llc nfsd parport_pc ppdev auth_rpcgss nfs_acl lockd lp grace parport sunrpc ramoops efi_pstore reed_solomon pstore_blk pstore_zone
[  951.558832]  ip_tables x_tables autofs4 btrfs blake2b_generic zstd_compress dm_crypt raid10 raid456 async_raid6_recov async_memcpy async_pq async_xor async_tx xor raid6_pq libcrc32c raid1 raid0 multipath linear bonding tls mgag200 drm_kms_helper syscopyarea sysfillrect sysimgblt fb_sys_fops cec crct10dif_pclmul hid_generic crc32_pclmul ghash_clmulni_intel rc_core usbhid cdc_ether aesni_intel usbnet igb crypto_simd mii ahci hid dca cryptd drm lpc_ich libahci megaraid_sas i2c_algo_bit wmi
[  951.558878] CPU: 6 PID: 36678 Comm: peer local sock Tainted: P        W  OE K   5.15.0-60-generic #66-Ubuntu
[  951.558882] Hardware name: Dell Inc. PowerEdge R720/0XH7F2, BIOS 2.9.0 12/06/2019
[  951.558884] RIP: 0010:blk_mq_free_rqs (/build/linux-25O3Ed/linux-5.15.0/block/blk-mq.c:2335 /build/linux-25O3Ed/linux-5.15.0/block/blk-mq.c:2369) 
[ 951.558890] Code: 40 08 e8 44 6b d1 ff 4c 8b 45 d0 49 8b 80 a0 00 00 00 4c 39 e8 75 c4 48 83 c4 18 5b 41 5c 41 5d 41 5e 41 5f 5d c3 cc cc cc cc <0f> 0b e9 5c ff ff ff 66 2e 0f 1f 84 00 00 00 00 00 0f 1f 44 00 00
All code
========
   0:	40 08 e8             	or     %bpl,%al
   3:	44 6b d1 ff          	imul   $0xffffffff,%ecx,%r10d
   7:	4c 8b 45 d0          	mov    -0x30(%rbp),%r8
   b:	49 8b 80 a0 00 00 00 	mov    0xa0(%r8),%rax
  12:	4c 39 e8             	cmp    %r13,%rax
  15:	75 c4                	jne    0xffffffffffffffdb
  17:	48 83 c4 18          	add    $0x18,%rsp
  1b:	5b                   	pop    %rbx
  1c:	41 5c                	pop    %r12
  1e:	41 5d                	pop    %r13
  20:	41 5e                	pop    %r14
  22:	41 5f                	pop    %r15
  24:	5d                   	pop    %rbp
  25:	c3                   	ret    
  26:	cc                   	int3   
  27:	cc                   	int3   
  28:	cc                   	int3   
  29:	cc                   	int3   
  2a:*	0f 0b                	ud2    		<-- trapping instruction
  2c:	e9 5c ff ff ff       	jmp    0xffffffffffffff8d
  31:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
  38:	00 00 00 
  3b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)

Code starting with the faulting instruction
===========================================
   0:	0f 0b                	ud2    
   2:	e9 5c ff ff ff       	jmp    0xffffffffffffff63
   7:	66 2e 0f 1f 84 00 00 	cs nopw 0x0(%rax,%rax,1)
   e:	00 00 00 
  11:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
[  951.558893] RSP: 0018:ffffbc372058fd10 EFLAGS: 00010286
[  951.558895] RAX: ffff980237f70180 RBX: ffff980237f70000 RCX: ffff980237f80000
[  951.558898] RDX: 0000000000000021 RSI: ffff980222fd8508 RDI: 00000000c0000000
[  951.558900] RBP: ffffbc372058fd50 R08: ffff98022527e840 R09: fffff5d897dfdc00
[  951.558902] R10: 0000000000000030 R11: ffff800000000000 R12: ffff980ad94b0c58
[  951.558904] R13: ffff98022527e8e0 R14: ffff98022527ed80 R15: 0000000000000000
[  951.558906] FS:  00007f48957fa640(0000) GS:ffff98086f8c0000(0000) knlGS:0000000000000000
[  951.558909] CS:  0010 DS: 0000 ES: 0000 CR0: 0000000080050033
[  951.558911] CR2: 000055793d24a0d0 CR3: 0000000c8c1e4002 CR4: 00000000000606e0
[  951.558914] Call Trace:
[  951.558916]  <TASK>
[  951.558921] ? mutex_lock (/build/linux-25O3Ed/linux-5.15.0/arch/x86/include/asm/atomic64_64.h:190 /build/linux-25O3Ed/linux-5.15.0/include/linux/atomic/atomic-long.h:443 /build/linux-25O3Ed/linux-5.15.0/include/linux/atomic/atomic-instrumented.h:1669 /build/linux-25O3Ed/linux-5.15.0/kernel/locking/mutex.c:165 /build/linux-25O3Ed/linux-5.15.0/kernel/locking/mutex.c:279) 
[  951.558927] blk_mq_sched_free_requests (/build/linux-25O3Ed/linux-5.15.0/block/blk-mq-sched.c:671 (discriminator 2)) 
[  951.558932] blk_cleanup_queue (/build/linux-25O3Ed/linux-5.15.0/block/blk-core.c:406) 
[  951.558937] blk_cleanup_disk (/build/linux-25O3Ed/linux-5.15.0/block/genhd.c:1361 /build/linux-25O3Ed/linux-5.15.0/block/genhd.c:1378) 
[  951.558942] snapimage_free (/var/lib/dkms/blksnap/6.0.0.1060/build/snapimage.c:201) blksnap
[  951.558951] snapshot_free (/var/lib/dkms/blksnap/6.0.0.1060/build/snapshot.c:173 /var/lib/dkms/blksnap/6.0.0.1060/build/snapshot.c:206) blksnap
[  951.558957] snapshot_destroy (/usr/src/linux-headers-5.15.0-60-generic/./include/linux/kref.h:66 /var/lib/dkms/blksnap/6.0.0.1060/build/snapshot.c:240 /var/lib/dkms/blksnap/6.0.0.1060/build/snapshot.c:462) blksnap
[  951.558964] ? ioctl_snapshot_take (/var/lib/dkms/blksnap/6.0.0.1060/build/ctrl.c:259) blksnap
[  951.558970] ioctl_snapshot_destroy (/var/lib/dkms/blksnap/6.0.0.1060/build/ctrl.c:268) blksnap
[  951.558976] ctrl_unlocked_ioctl (/var/lib/dkms/blksnap/6.0.0.1060/build/ctrl.c:552) blksnap
[  951.558982] __x64_sys_ioctl (/build/linux-25O3Ed/linux-5.15.0/fs/ioctl.c:52 /build/linux-25O3Ed/linux-5.15.0/fs/ioctl.c:874 /build/linux-25O3Ed/linux-5.15.0/fs/ioctl.c:860 /build/linux-25O3Ed/linux-5.15.0/fs/ioctl.c:860) 
[  951.558989] do_syscall_64 (/build/linux-25O3Ed/linux-5.15.0/arch/x86/entry/common.c:50 /build/linux-25O3Ed/linux-5.15.0/arch/x86/entry/common.c:80) 
[  951.558993] ? syscall_exit_to_user_mode (/build/linux-25O3Ed/linux-5.15.0/arch/x86/include/asm/jump_label.h:55 /build/linux-25O3Ed/linux-5.15.0/arch/x86/include/asm/nospec-branch.h:383 /build/linux-25O3Ed/linux-5.15.0/arch/x86/include/asm/entry-common.h:94 /build/linux-25O3Ed/linux-5.15.0/kernel/entry/common.c:131 /build/linux-25O3Ed/linux-5.15.0/kernel/entry/common.c:302) 
[  951.558997] ? __x64_sys_write (/build/linux-25O3Ed/linux-5.15.0/fs/read_write.c:658) 
[  951.559000] ? do_syscall_64 (/build/linux-25O3Ed/linux-5.15.0/arch/x86/entry/common.c:87) 
[  951.559003] ? do_syscall_64 (/build/linux-25O3Ed/linux-5.15.0/arch/x86/entry/common.c:87) 
[  951.559006] ? exc_page_fault (/build/linux-25O3Ed/linux-5.15.0/arch/x86/mm/fault.c:1545) 
[  951.559010] entry_SYSCALL_64_after_hwframe (/build/linux-25O3Ed/linux-5.15.0/arch/x86/entry/entry_64.S:118) 
[  951.559013] RIP: 0033:0x7f495e6f2aff
[ 951.559018] Code: 00 48 89 44 24 18 31 c0 48 8d 44 24 60 c7 04 24 10 00 00 00 48 89 44 24 08 48 8d 44 24 20 48 89 44 24 10 b8 10 00 00 00 0f 05 <41> 89 c0 3d 00 f0 ff ff 77 1f 48 8b 44 24 18 64 48 2b 04 25 28 00
All code
========
   0:	00 48 89             	add    %cl,-0x77(%rax)
   3:	44 24 18             	rex.R and $0x18,%al
   6:	31 c0                	xor    %eax,%eax
   8:	48 8d 44 24 60       	lea    0x60(%rsp),%rax
   d:	c7 04 24 10 00 00 00 	movl   $0x10,(%rsp)
  14:	48 89 44 24 08       	mov    %rax,0x8(%rsp)
  19:	48 8d 44 24 20       	lea    0x20(%rsp),%rax
  1e:	48 89 44 24 10       	mov    %rax,0x10(%rsp)
  23:	b8 10 00 00 00       	mov    $0x10,%eax
  28:	0f 05                	syscall 
  2a:*	41 89 c0             	mov    %eax,%r8d		<-- trapping instruction
  2d:	3d 00 f0 ff ff       	cmp    $0xfffff000,%eax
  32:	77 1f                	ja     0x53
  34:	48 8b 44 24 18       	mov    0x18(%rsp),%rax
  39:	64                   	fs
  3a:	48                   	rex.W
  3b:	2b                   	.byte 0x2b
  3c:	04 25                	add    $0x25,%al
  3e:	28 00                	sub    %al,(%rax)

Code starting with the faulting instruction
===========================================
   0:	41 89 c0             	mov    %eax,%r8d
   3:	3d 00 f0 ff ff       	cmp    $0xfffff000,%eax
   8:	77 1f                	ja     0x29
   a:	48 8b 44 24 18       	mov    0x18(%rsp),%rax
   f:	64                   	fs
  10:	48                   	rex.W
  11:	2b                   	.byte 0x2b
  12:	04 25                	add    $0x25,%al
  14:	28 00                	sub    %al,(%rax)
[  951.559020] RSP: 002b:00007f48957f9730 EFLAGS: 00000246 ORIG_RAX: 0000000000000010
[  951.559023] RAX: ffffffffffffffda RBX: 000000000354cbb0 RCX: 00007f495e6f2aff
[  951.559025] RDX: 00007f48957f9890 RSI: 0000000080105606 RDI: 0000000000000037
[  951.559026] RBP: 000000000354cb80 R08: 00007f48600068b0 R09: 0000000000000000
[  951.559028] R10: 00007f48957f9780 R11: 0000000000000246 R12: 000000000354cbd8
[  951.559030] R13: 00007f48957f97d0 R14: 000000000354cba8 R15: 00007f48957f97cf
[  951.559033]  </TASK>
```
</details>
