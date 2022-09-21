# Test boundary

## Purpose of the test
The test is designed to detect data distortions on the snapshot image of a block device when overwriting specific sectors located at the beginning of the block device, or at the end, or at the border of the block (chunk) that the COW algorithm operates on.

## Testing methodology
Data of a certain pattern is recorded on a block device at specific addresses.
During the test, the minimum available blocks are recorded (512 or 4096 or others, depending on the test parameter).
At the same time, it is checked that the neighboring sectors have not changed their values.
The peculiarity of the blksnap module is that it copies data to the change repository in chunks.
The test verifies that the algorithm for calculating the offset of the copied chunks is performed correctly.
As for the "corrupt" and "diff_storage" tests, the correct location of the sector is checked by its offset from the beginning of the block device, the recording time by timestamp and sequence number, and the integrity of the sector is controlled by a checksum.
When generating random numbers of chunks for verification, the history of previous tests is taken into account. This allows you not to select already checked or adjacent chunks.

## Algorithm
1. The entire original block device is filled with a pattern.
2. Further actions are performed in the main test cycle.
3. Checking the recorded data in the original device.
	* A snapshot is being created
	* In the loop
		* The chunk number is randomly selected
		* The first block of the chunk is recorded on the original device
		* The remaining blocks of the chunk are checked on the original device
		* The data is checked on the snapshot image
			* one random block of th chunk
			* block before the test chunk
			* the block following the test chunk
		* The last block of the chunk is recorded on the original device
		* The blocks of the chunk are checked on the original device except for the first and last
		* Checking on the snapshot image
			* one random block of the chunk
			* block before the test chunk
			* the block following the test chunk
		* The check is performed on the original block device
			* the first block of the test chunk
			* the last block of the test chunk
	* the cycle ends, the snapshot is released
4.The recording of the snapshot image into the device is being tested
	* A snapshot is being created
	* In the loop
		* The chunk number is randomly selected
		* The first block of the chunk is recorded on the snapshot image
		* The data is being tested on the snapshot image
			* the remaining blocks of the chunk
			* the last block of the previous chunk
			* the first block of the subsequent chunk
		* The last block of the piece is recorded on the snapshot image
		* The check is performed on the snapshot image
			* all blocks of the chunk except the first and last
			* the last block of the previous chunk
			* the first block of the subsequent chunk
			* the first block of the overwritten chunk
			* the last block of the overwritten chunk
	* the cycle ends, the snapshot is released
5. The recording of the snapshot image into the device is being tested
	* A snapshot is being created
	* In the loop
		* The chunk number is randomly selected
		* Writing is performed on the border of the chunks (two blocks: the last one and the first of the subsequent chunk)
		* Testing is performed on the snapshot image
			* blocks of the first chunk
			* blocks of the second chunk
			* previously overwritten blocks
	* the cycle ends, the snapshot is released
6. If successful, the verification cycle is repeated until the time allocated for testing has passed.
