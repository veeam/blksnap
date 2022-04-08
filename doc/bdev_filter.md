# Block device filters

## Introduction
A block device filter is a kernel module that handles requests to a block device and performs preprocessing of this request. Block device filters allow to expand the capabilities of the block layer. The blksnap module is the first filter module that is offered for the upstream Linux kernel.

The idea of intercepting requests to block devices is not new. Even in the 2.6 kernel, there was the possibility of handling requests by substituting the make_request_fn() function, which belonged to the request_queue structure. There are several modules that have used this feature. But none of them were in the kernel tree. Therefore, in the process of refactoring, this possibility was eliminated.

The kernel's support for block device filters will allow to return the ability to handle requests, and also allow to do it more securely. Moreover, the possibility of simultaneous operation of several filters by the number of available altitudes is provided. But the number of available altitudes is limited by the number of filters in the kernel tree. This restriction should encourage the provision of new block device filters to the kernel.

## How it works
The block device filter is added at the top of the block layer.

 +-------------+ +-------------+
 | File System | |  Direct I/O |
 +-------------+ +-------------+
        ||             ||
        \/             \/
 +-----------------------------+
 |   | Block Device Filter|    |
 |   +--------------------+    |
 |         Block layer         |
 +-----------------------------+
	||  ||  ...  ||
	\/  \/       \/
       +------+   +------+
       | Disk |   | Disk |
       +------+   +------+

Requests sent to the block layer are handled by filters and processed.
The filter can pass the request for further execution, skip processing the request, or redirect the request to another device.
In some cases, the filter cannot immediately process the request, in which case it requires repeated processing.
For more information about the filter processing cycle, see the section "Filtering algorithm".

Theoretically, there can be several filters at the same time. Filters can be compatible with each other. In this case, they can be placed on their altitudes and process requests in turn. But filters may be incompatible because of their purpose. In this case, they should use one altitude. This will protect the system from an inoperable combination of filters. Currently, only one altitude is reserved for the blksnap module.

## Filtering algorithm
In the system, the filter is a structure with a reference counter and a set of callback functions. An array of pointers to filters by the number of reserved altitudes is added to the block_device structure. The array is protected from simultaneous access by a spin lock.

The function submit_bio_noaсct() has added a call to the filter processing function filter_bio(), which implements the filtering algorithm.
For a block device, all altitudes are checked. If there was a filter on the altitude (not NULL in the cell), then the corresponding callback function is called. Depending on the result of the callback function, the following is performed:
 - go to the next altitude (pass)
 - completion of request processing (skip)
 - re-processing filters from the first altitude (redirect)
 - re-processing the request (repeat)

In order to exclude a recursive call to the submit_bio() function, a pointer to the list of I/O blocks current->bio_list is initialized before calling the callback function. If new I/O requests are creaded when processing a request, they are added to this list. This mechanism allows to protect the stack from overflow.
After the I/O request is processed by the filter, new requests are extracted from current->bio_list and executed. Therefore, synchronous execution of I/O requests in the filter is not possible.
However, if need to wait for the completion of new requests from the filter, the callback return the "repeat" code. In this case, after processing requests from current->bio_list, the filter handler will call the filter callback function again so that the filter can take a "quiet nap" while waiting for I/O.

If the filter redirect the processing of the I/O request to another block device by changing the pointer to the block device in bio, then the filter processing must be repeated from the beginning, but for another block device. In this case, the filter callback function should return the "redirect" code.

If the filter decides that the original I/O request does not need to be executed, then the return code of the callback function should be "skip".

To prevent a new I/O request from the filter from being processed by filters, the flag BIO_FILTERED can be set. Such bios are immediately skipped by the filter handler.

## Algorithm for freeing filter resources
The block device can be extracted. In this case, the block_device structure is released. The filter in this case should also be released. To properly release the filter, it has a reference counter and a callback to release its resources. If, at the time of deleting the block device and disabling the filter, it processes the request, then thanks to the reference counter, the release will be performed only when the counter is reduced to zero.

## How to use it
Detaching the filter can be initiated by calling the bdev_filter_detach() function or automatically when deleting a block device. In this case, the filter will be removed from the block_device structure and the detach_cb() callback function will be called. When executing detach_cb(), the process cannot be put into a waiting state. If the filter needs to wait for the completion of any processes, it is recommended to schedule the execution in worker. It is important to remember that after completing the execution of the bdev_filter_detach() function, the filter will no longer receive I/O requests for processing.

The kernel module does not need to store a pointer to the bdev_filter structure. It is already stored in the block_device structure. To get a filter, just call bdev_filter_get_by_altitude(). At the same time, the reference count will be increased in order to use the structure safely. After use, the reference count must be reduced using bdev_filter_put().

## What's next
In the current implementation, only the calls submit_bio_noacct() and dev_free_inode() are handled. In the future, I would like to add a handle for the functions bdev_read_page() and dev_write_page(). This will allow to work correctly with disks that have the rw_page() callback function in the block_device_operations structure.
In the future, the number of altitudes will increase. When this number reaches 8, a simple array of pointers to filters can be replaced with a more complex structure, such as xarray.
