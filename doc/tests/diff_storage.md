# Test diff_duration

## Purpose of the test
The test is designed to detect data distortions on the original block device on which the change storage (diff storage) is located.
Changes are written to the repository directly to disk, and areas are allocated as files on the file system.
Thus, if the algorithm for writing changes to the repository fails, the metadata of the file system can be damaged or the data of neighboring files can be damaged.

## Testing methodology
The blocks are checked using the same pattern as for the corrupt test.
If there is an error writing to the DiffSt area, blocks with the incorrect field "sector offset from the beginning of the block device in sectors" should appear in the WR areas.
The space of a block device is randomly divided into two approximately identical sets of ranges.
We get the writable areas (WR) and the areas given over to the change repository (DiffSt), replacing one another.

	+----+--------+----+--------+-- ... --+--------+
	| WR | DiffSt | WR | DiffSt |         | DiffSt |
	+----+--------+----+--------+-- ... --+--------+

When creating a snapshot, DiffSt areas are passed to the module to store snapshot changes in them.
An entry is made to the original device, which causes the COW algorithm to work, which copies the overwritten data from the WR area to the diffstat area.
Verification is performed by checking the correctness of the data in the snapshot image.
If, when writing in the DiffSt area, a record occurs in the WR area, then when reading the snapshot image when reading from the WR zone, the read blocks will not pass the check by the value of the sector offset.

## Algorithm
1. The entire original block device is filled with a pattern.
2. The entire contents of the block device are checked.
3. Further actions are performed in the main test cycle.
4. A set of random numbers is generated, WR and DiffSt regions are formed from them.
5. A snapshot is created, the DiffSt areas are passed to the module to store the snapshot changes.
6. Random ranges bounded by WR regions are generated and overwritten on the original block device.
7. The snapshot image is checked, limited by the WR area.
8. An error message is displayed if a block with an incorrect pattern is detected, and the test is completed.
8. Upon successful completion of the test, the snapshot is released, and the DiffSt area is overwritten with correct data.
9. If successful, the verification cycle is repeated until the time allocated for testing has passed.

Nuance!
A feature has been implemented in the veeamsnap module that allows you to always read zeros when reading areas allocated for storing snapshot changes.
In the case of blksnap, this feature has not been implemented (at least not yet), so when the snapshot image is fully read, the data copied by the COW algorithm will be read from the DiffSt regions, that is, some garbage. It's not a big deal if bitlooker works correctly. And in the absence of bitlooker, the backup size may grow. With the stretch algorithm, it is insignificant. But with common it can be significant.
