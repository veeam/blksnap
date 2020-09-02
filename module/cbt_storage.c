#include "stdafx.h"
#ifdef PERSISTENT_CBT

#include "rangevector.h"
#include "cbt_params.h"
#include "cbt_storage.h"
#include "sector.h"
#include "blk_direct.h"
#include <linux/crc32.h>

#define SECTION "cbt_store "
#include "log_format.h"


static int _cbt_storage_read_page(cbt_storage_accessor_t* accessor)
{
    int res;
    sector_t phys_offset = 0;
    sector_t phys_length = 0;
    uint32_t crc;

    res = rangevector_v2p(accessor->rangevector, accessor->page_number * sector_from_uint(PAGE_SIZE), sector_from_uint(PAGE_SIZE), &phys_offset, &phys_length);
    if (res != SUCCESS){
        log_err("Failed to get real disk offset for persistent CBT data");
        return res;
    }

    if (phys_length != sector_from_uint(PAGE_SIZE)){
        log_err_sect("Unordered range size: ", phys_length);
        log_err("Range is not ordered by PAGE_SIZE");
        return -EINVAL;
    }

    res = blk_direct_submit_page(accessor->device, READ_SYNC, phys_offset, accessor->pg);
    if (res != SUCCESS){
        log_err("Failed to read device");
        log_err_sect("Real device offset: ", phys_offset);
        return res;
    }

    //check magic
    if (0 != memcmp(accessor->page->magic, CBT_STORAGE_MAGIC, 8)){
        log_err("Invalid CBT data header. Magic mismatch.");
        log_err_sect("device offset=", phys_offset);
        log_err_sect("avaliable sectors=", phys_length);
        log_err("Page header:");
        log_err_bytes((unsigned char*)accessor->page, sizeof(cbt_storage_page_t));
        return -EINVAL;
    }

    //chech CRC32
    crc = crc32(~0, (void*)(accessor->page) + offsetof(cbt_storage_page_t, number), PAGE_SIZE - offsetof(cbt_storage_page_t, number));
    if (crc != accessor->page->crc){
        log_err_lld("Invalid CRC32 on page #", accessor->page_number);
        log_err_sect("Physical offset on block device: ", phys_offset);
        log_err_lx("Expected crc=", crc);
        log_err_lx("Read crc=", accessor->page->crc);
        return -EINVAL;
    }

    if (accessor->page_number != accessor->page->number){
        log_err("Invalid cbt data page number");
        log_err_lld("Expected=", accessor->page_number);
        log_err_lld("Read=", accessor->page->number);
        return -EINVAL;
    }

    return SUCCESS;
}

static int _cbt_storage_write_page(cbt_storage_accessor_t* accessor, struct timespec64* optional_time)
{
    int res;
    sector_t phys_offset = 0;
    sector_t phys_length = 0;

    //log_tr_d("DEBUG! write page# ", accessor->page_number);

    //set magic
    memcpy(accessor->page->magic, CBT_STORAGE_MAGIC, 8);
    //set number
    accessor->page->number = accessor->page_number;
    //set time marker
    if (optional_time){
        accessor->page->tv_sec = optional_time->tv_sec;
        accessor->page->tv_nsec = optional_time->tv_nsec;
    }
    else{
        accessor->page->tv_sec = accessor->time.tv_sec;
        accessor->page->tv_nsec = accessor->time.tv_nsec;
    }
    //calculate CRC32
    accessor->page->crc = crc32(~0, (void*)(accessor->page) + offsetof(cbt_storage_page_t, number), PAGE_SIZE - offsetof(cbt_storage_page_t, number));

    res = rangevector_v2p(accessor->rangevector, accessor->page_number * sector_from_uint(PAGE_SIZE), sector_from_uint(PAGE_SIZE), &phys_offset, &phys_length);
    if (res != SUCCESS){
        log_err("Failed to get real disk offset for persistent CBT data");
        return res;
    }

    if (phys_length != sector_from_uint(PAGE_SIZE)){
        log_err_sect("Unordered range size: ", phys_length);
        log_err("Range is not ordered by PAGE_SIZE");
        return -EINVAL;
    }

    //write page
    //log_tr_sect("DEBUG! offset= ", phys_offset);
    res = blk_direct_submit_page(accessor->device, WRITE_SYNC, phys_offset, accessor->pg);
    if (res != SUCCESS){
        log_err("Failed to read device");
        log_err_sect("Real device offset: ", phys_offset);
        return res;
    }

    return SUCCESS;
}

int cbt_storage_open(cbt_persistent_parameters_t* params, cbt_storage_accessor_t* accessor)
{
    int res = SUCCESS;
    log_tr_dev_t("Open persistent CBT storage device ",params->dev_id);

    res = blk_dev_open(params->dev_id, &accessor->device);
    if (res != SUCCESS){
        log_err_dev_t("Failed to open device ", params->dev_id);
        return res;
    }
    do{
        if (0 == rangevector_cnt(&params->rangevector)){
            log_err("No range for reading");
            res = -EINVAL;
            break;
        }

        //allocate page
        accessor->pg = alloc_page(GFP_KERNEL);
        if (NULL == accessor->pg){
            log_err("Failed to allocate page");
            res = -ENOMEM;
            break;
        }
        accessor->page = (cbt_storage_page_t*)page_address(accessor->pg);

        accessor->rangevector = &params->rangevector;
        accessor->page_count = rangevector_length(accessor->rangevector) / sector_from_uint(PAGE_SIZE);
        accessor->page_number = 0;
        accessor->used_page_count = accessor->page_count;
        accessor->page_offset = offsetof(cbt_storage_page_t, data);

    } while (false);
    if (res != SUCCESS)
        cbt_storage_close(accessor);
    return res;
}

void cbt_storage_close(cbt_storage_accessor_t* accessor)
{
    if (accessor->pg != NULL){
        __free_page(accessor->pg);
        accessor->pg = NULL;
        accessor->page = NULL;
    }
    if (accessor->device != NULL){
        log_tr("Close persistent CBT storage device ");

        blk_dev_close(accessor->device);
        accessor->device = NULL;
    }
    accessor->rangevector = NULL;
}

int cbt_storage_check(cbt_storage_accessor_t* accessor)
{
    int res = SUCCESS;
    //check all pages

    //log_tr("[TBD] Checking CRC32 for all pages:");
    log_tr("DEBUG!Reading first page of CBT data");
    accessor->page_number = 0;
    accessor->used_page_count = accessor->page_count;
    res = _cbt_storage_read_page(accessor);
    if (res != SUCCESS){
        log_err("Failed to get first page ");
        return res;
    }
    //get time from first page
    accessor->time.tv_sec = accessor->page->tv_sec;
    accessor->time.tv_nsec = accessor->page->tv_nsec;

    log_tr_llx("DEBUG!padding=", accessor->padding);

    if (accessor->time.tv_sec == 0ull){
        log_tr("Persistent CBT data is empty");
        accessor->used_page_count = 0;
    }

    log_tr("DEBUG!Reading other pages of CBT data");
    for (accessor->page_number = 1; accessor->page_number < accessor->page_count; ++accessor->page_number){
        res = _cbt_storage_read_page(accessor);
        if (res != SUCCESS){
            log_err_lld("Failed to get page #", accessor->page_number);
            break;
        }
        if (accessor->time.tv_sec != 0ull){
            if (accessor->page->tv_sec == 0ull){
                //unused page found
                if (accessor->used_page_count == accessor->page_count){
                    accessor->used_page_count = accessor->page_number;
                    log_tr_lld("Found first unused page #", accessor->page_number);
                }
            }
            else{
                if ((accessor->time.tv_sec != accessor->page->tv_sec) && (accessor->time.tv_nsec != accessor->page->tv_nsec))
                {
                    log_err_lld("Invalid time marker on page #", accessor->page_number);
                    log_err_llx("Expected=", accessor->time.tv_sec);
                    log_err_llx("Read=", accessor->page->tv_sec);

                    //cbt data skipped
                    accessor->time.tv_sec = 0ull;
                    accessor->time.tv_nsec = 0ul;

                    break;
                }
            }
        }
    }

    if (res != SUCCESS){
        log_err("Check failed");
        return res;
    }

    if (accessor->time.tv_sec != 0ull){
        //show time
        struct tm modify_time;

        time64_to_tm(accessor->time.tv_sec, 0, &modify_time);

        log_tr_format("Persistent CBT data from %04ld.%02d.%02d %02ld:%02d:%02d %d",
            modify_time.tm_year + 1900,
            modify_time.tm_mon + 1,
            modify_time.tm_mday,
            modify_time.tm_hour,
            modify_time.tm_min,
            modify_time.tm_sec,
            accessor->time.tv_nsec);
    }

    return res;
}


int cbt_storage_prepare4read(cbt_storage_accessor_t* accessor)
{
    int res = SUCCESS;

    //set position to first page
    accessor->page_number = 0;
    accessor->page_offset = 0;

    res = _cbt_storage_read_page(accessor);
    if (res != SUCCESS){
        log_err("Failed to get first page ");
        return res;
    }
    //check time from fisrt page
    accessor->time.tv_sec = accessor->page->tv_sec;
    accessor->time.tv_nsec = accessor->page->tv_nsec;

    if (accessor->time.tv_sec == 0ull)
        return ENODATA;

    //set CBT data empty
    {
        struct timespec64 tm = { 0 }; //

        res = _cbt_storage_write_page(accessor, &tm);
        if (res != SUCCESS){
            log_err("Failed to write first page ");
            return res;
        }
    }

    return res;
}

int cbt_storage_prepare4write(cbt_storage_accessor_t* accessor)
{
    int res = SUCCESS;
    //set position to first page
    accessor->page_number = 0;
    accessor->page_offset = 0;

    res = _cbt_storage_read_page(accessor);
    if (res != SUCCESS){
        log_err("Failed to get first page ");
        return res;
    }
    //get time

    ktime_get_real_ts64(&accessor->time);

    return res;
}

int cbt_storage_read(cbt_storage_accessor_t* accessor, void* dst, size_t sz)
{
    int res = SUCCESS;
    size_t ofs = 0;
    size_t can_be_read;
    if (accessor->page_number == accessor->page_count){
        log_err_lld("EOF reached, page number ", accessor->page_number);
        return -ENODATA;
    }

    can_be_read = min(CBT_PAGE_DATA_SIZE - accessor->page_offset, sz - ofs);
    memcpy(dst, accessor->page->data + accessor->page_offset, can_be_read);
    accessor->page_offset += can_be_read;
    ofs += can_be_read;

    do{
        if (accessor->page_offset == CBT_PAGE_DATA_SIZE) {
            accessor->page_number++;
            accessor->page_offset = 0;
        }

        if (accessor->page_number == accessor->page_count){
            if (ofs < sz){
                log_err_lld("EOF reached, page number= ", accessor->page_number);
                res = -ENODATA;
            }
            else{
                log_tr("Reached EOF");
                res = SUCCESS;
            }
            break;
        }

        //read next page
        res = _cbt_storage_read_page(accessor);
        if (res != SUCCESS){
            log_err_lld("Failed to get page #", accessor->page_number);
            return res;
        }

        if (ofs == sz)
            break;//reading is complete 

        can_be_read = min(CBT_PAGE_DATA_SIZE - accessor->page_offset, sz - ofs);
        memcpy(dst, accessor->page->data + accessor->page_offset, can_be_read);
        accessor->page_offset += can_be_read;
        ofs += can_be_read;

    } while (true);
    return res;
}

int cbt_storage_write(cbt_storage_accessor_t* accessor, void* src, size_t sz)
{
    int res = SUCCESS;
    size_t ofs = 0;
    size_t can_be_write;

    if (accessor->page_number == accessor->page_count){
        log_err_lld("Reached EOF, page number= ", accessor->page_number);
        return -ENODATA;
    }

    can_be_write = min(CBT_PAGE_DATA_SIZE - accessor->page_offset, sz - ofs);
    log_tr_sz("DEBUG! can_be_write=", can_be_write);
    if (can_be_write == 0){
        log_tr_format("can_be_write is zero. sz=%lu, ofs=%lu page_offset=%lu", (unsigned long)(sz), (unsigned long)(ofs), (unsigned long)(accessor->page_offset));
        return -EFAULT;
    }
    
    memcpy(accessor->page->data + accessor->page_offset, src + ofs, can_be_write);
    accessor->page_offset += can_be_write;
    ofs += can_be_write;

    do{
        if (accessor->page_offset == CBT_PAGE_DATA_SIZE){
            //write current page
            res = _cbt_storage_write_page(accessor, NULL);
            if (res != SUCCESS){
                log_err_lld("Failed to get page #", accessor->page_number);
                return res;
            }

            accessor->page_number++;
            accessor->page_offset = 0;
        }

        if (accessor->page_number == accessor->page_count){
            if (ofs < sz){
                log_err_lld("Reached EOF, page number= ", accessor->page_number);
                res = -ENODATA;
            }
            else{
                log_tr("Reached EOF");
                res = SUCCESS;
            }
            break;
        }

        if (ofs == sz)
            break;//writing is complete 

        can_be_write = min(CBT_PAGE_DATA_SIZE - accessor->page_offset, sz - ofs);
        log_tr_sz("DEBUG! can_be_write=", can_be_write);
        if (can_be_write == 0){
            log_tr_format("can_be_write is zero. sz=%lu, ofs=%lu page_offset=%lu", (unsigned long)sz, (unsigned long)ofs, (unsigned long)accessor->page_offset);
            return -EFAULT;
        }
        memcpy(accessor->page->data + accessor->page_offset, src + ofs, can_be_write);
        accessor->page_offset += can_be_write;
        ofs += can_be_write;

    } while (true);

    return res;
}

int cbt_storage_write_finish(cbt_storage_accessor_t* accessor)
{
    int res = SUCCESS;

    if (accessor->page_number == accessor->page_count)
        return SUCCESS;

    //write last page
    log_tr_lld("DEBUG! write last page #", accessor->page_number);

    res = _cbt_storage_write_page(accessor, NULL);
    if (res != SUCCESS){
        log_err_lld("Failed to get page #", accessor->page_number);
        return res;
    }

    accessor->page_number++;
    accessor->page_offset = 0;

    {//store empty unused pages
        struct timespec64 tm = { 0 };

        memset(accessor->page->data, 0, CBT_PAGE_DATA_SIZE);

        while (accessor->page_number < accessor->page_count){

            accessor->page->number = accessor->page_number;

            //log_tr_lld("DEBUG! write empty page #", accessor->page_number);
            res = _cbt_storage_write_page(accessor, &tm);
            if (res != SUCCESS){
                log_err_lld("Failed to get page #", accessor->page_number);
                break;
            }
            accessor->page_number++;
        }
    }
    return res;
}


#endif//PERSISTENT_CBT
