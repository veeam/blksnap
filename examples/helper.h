#ifndef BLK_SNAP_UTILS_HELPER_H
#define BLK_SNAP_UTILS_HELPER_H

#include <blk-snap/types.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sstream>
#include <iomanip>
#include <linux/fiemap.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

struct ioctl_dev_id_s to_dev_id(dev_t dev)
{
	struct ioctl_dev_id_s result;
	result.major = major(dev);
	result.minor = minor(dev);

	return result;
}

std::string snap_id_to_str(const unsigned char* id)
{
	std::ostringstream ss;
	ss << std::hex << std::setfill('0') << std::setw(2);
	for (size_t i = 0; i < SNAP_ID_LENGTH; ++i )
		ss << std::hex << std::setfill('0') << std::setw(2) << (int)id[i];

	return ss.str();
}

void str_to_snap_id(const std::string& str, unsigned char* id)
{
	int val;
	for (size_t i = 0; i < SNAP_ID_LENGTH; ++i)
	{
		std::stringstream ss(str.substr(i*2, 2));
		ss >> std::hex >> val;
		id[i] = val;
	}
}

std::string snap_store_to_str(struct snap_store* store)
{
	return snap_id_to_str(store->id);
}

size_t GetFileSize(int file)
{
	struct stat64 file_stat;
	int  stat_res = fstat64( file, &file_stat );
	if ( 0 != stat_res )
		throw std::system_error(errno, std::generic_category(), "Failed to get file size");

	size_t result = (file_stat.st_size/file_stat.st_blksize) * file_stat.st_blksize;
	if (result == 0)
		throw std::runtime_error("No blocks in file");

	return result;
}

std::vector<struct ioctl_range_s> EnumRanges(int file)
{
	std::vector<fiemap_extent> extends;

	uint32_t ignoreMask = (FIEMAP_EXTENT_LAST | FIEMAP_EXTENT_UNWRITTEN | FIEMAP_EXTENT_MERGED);
	const uint32_t extentsPerQuery = 500;
	std::vector<uint8_t> fiemapBuffer(sizeof(fiemap) + sizeof(fiemap_extent) * extentsPerQuery);
	size_t fileSize = GetFileSize(file);
	fiemap* pFiemap = reinterpret_cast<fiemap*>(&fiemapBuffer.front());

	for (uint64_t logicalOffset = 0; logicalOffset < fileSize; )
	{
		pFiemap->fm_start = logicalOffset;
		pFiemap->fm_length = fileSize - logicalOffset;
		pFiemap->fm_extent_count = extentsPerQuery;
		pFiemap->fm_flags = 0;

		if (::ioctl(file, FS_IOC_FIEMAP, pFiemap))
			throw std::system_error(errno, std::generic_category(), "Failed to call FIEMAP");

		for (uint32_t i = 0; i != pFiemap->fm_mapped_extents; ++i)
		{
			const fiemap_extent& fiemapExtent = pFiemap->fm_extents[i];
			if (fiemapExtent.fe_flags & ~ignoreMask)
				throw std::runtime_error("Incompatible extent flags"); //@todo add more information

			extends.push_back(fiemapExtent);
			logicalOffset = fiemapExtent.fe_logical + fiemapExtent.fe_length;
		}

		if (pFiemap->fm_mapped_extents != extentsPerQuery)
			break;
	}

	std::vector<struct ioctl_range_s> result;
	result.reserve(extends.size());
	for (auto& ext : extends)
	{
		struct ioctl_range_s st;
		st.left = ext.fe_physical;
		st.right = ext.fe_physical + ext.fe_length;
		result.push_back(st);
	}

	return result;
}

int generate_random(void* buf, unsigned int length)
{
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd == -1)
		return -1;

	int result = read(fd, buf, length);
	close(fd);
	return result;
}


#endif //BLK_SNAP_UTILS_HELPER_H
