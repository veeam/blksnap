# Test corrupt

## Purpose of the test
The test is designed to detect data distortions on the snapshot image of a block device.
There are several main reasons why the snapshot image may contain incorrect data:
* tracking does not work, that is, requests are not handled;
* the COW algorithm does not work correctly, that is, already modified data gets into the change store;
* errors in addressing data blocks.

## Testing methodology
The data of a certain pattern is recorded on the block device before the snapshot is created and after.
After the snapshot is created, the data is checked on the snapshot image. It should only contain data recorded before the snapshot was created.
The pattern has a sector size and is a header and random data.
The header contains the checksum of the sector, the sequence number of the record, the offset of the sector from the beginning of the block device in the sectors, and the time of recording the sector.
The fact that the sector was recorded before the snapshot was created is checked by the sequence number of the record and the recording time.
The correct location of the sector is checked by its offset from the beginning of the block device.
The integrity of the sector is controlled by a checksum.

## Algorithm
1. The entire original block device is filled with a pattern.
2. Further actions are performed in the main test cycle.
3. Create a snapshot of a block device.
4. A full check is made that the snapshot image contains correct data.
5. The first few (3) sectors are overwritten (the file system superblock update is simulated during mounting).
6. In the loop, data is recorded on the original block device and data is checked on the snapshot.
	* A random number of sectors of the original block device is recorded.
	* Overwrite the first sector again on the original block device.
	* A complete re-check of the correctness of the data on the snapshot image is performed.
	* If data corruption has been detected on the snapshot image, then the first few dozen damages are logged.
7. A message about the success of the cycle is displayed.
8. The snapshot is released
9. If successful, the verification cycle is repeated until the time allocated for testing has passed.
