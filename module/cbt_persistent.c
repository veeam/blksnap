#include "stdafx.h"

#ifdef PERSISTENT_CBT

#include "cbt_persistent.h"

#include "uuid_util.h"
#include "rangevector.h"
#include "blk_direct.h"

#include "tracker.h"

#include "cbt_params.h"
#include "cbt_notify.h"
#include "cbt_storage.h"
#include "cbt_checkfs.h"

#include <linux/syscore_ops.h>

#define SECTION "cbt_prst  "
#include "log_format.h"


#define CBT_REGISTER_MAGIC "vcbtreg\0"
#define CBT_REGISTER_END   "vcbtend\0"
#define CBT_REGISTER_ERROR "vcbterr\0"

//////////////////////////////////////////////////////////////////////////
// global variables 
//////////////////////////////////////////////////////////////////////////
typedef struct cbt_prst_list_s
{
    struct list_head headlist;
    struct rw_semaphore lock;
}cbt_prst_list_t;

static cbt_prst_list_t _cbt_prst_registers_list = { { 0 } }; // list of registes for the persistend CBT. struct cbt_prst_reg_t
static cbt_persistent_parameters_t _cbt_params = {0}; //parsed persistent CBT parameters
static volatile bool _cbt_is_load = false; //

DEFINE_MUTEX(_cbt_prst_attach_locker);

//////////////////////////////////////////////////////////////////////////
// cbt_prst_reg_t
//////////////////////////////////////////////////////////////////////////
typedef struct cbt_prst_reg_s
{
    struct list_head link;
    dev_t dev_id;
    uint32_t check_parameters_sz; 
    void* check_parameters;
    cbt_map_t* cbt_map;
} cbt_prst_reg_t;

static cbt_prst_reg_t* _cbt_prst_reg_new(dev_t dev_id, uint32_t check_parameters_sz, void* check_parameters, cbt_map_t* cbt_map)
{
    cbt_prst_reg_t* reg = dbg_kzalloc(sizeof(cbt_prst_reg_t), GFP_KERNEL);
    if (reg == NULL){
        log_err("Failed to allocate memory for persistent CBT register");
        return NULL;
    }
    INIT_LIST_HEAD(&reg->link);
    reg->dev_id = dev_id;
    reg->check_parameters_sz = check_parameters_sz;
    reg->check_parameters = check_parameters;
    reg->cbt_map = cbt_map_get_resource(cbt_map);

    list_add(&reg->link, &_cbt_prst_registers_list.headlist);
    return reg;
}

static void _cbt_prst_reg_del(cbt_prst_reg_t* reg)
{
    list_del(&reg->link);
    
    if (reg->check_parameters)
        dbg_kfree(reg->check_parameters);

    cbt_map_put_resource(reg->cbt_map);

    dbg_kfree(reg);
}

static cbt_prst_reg_t* _cbt_prst_reg_find(dev_t dev_id)
{
    struct list_head* current_list_head;

    if (!list_empty(&_cbt_prst_registers_list.headlist))
    {
        list_for_each(current_list_head, &_cbt_prst_registers_list.headlist){
            cbt_prst_reg_t* current_reg = list_entry(current_list_head, cbt_prst_reg_t, link);

            if (dev_id == current_reg->dev_id)
                return current_reg;
        }
    }
    return NULL;
}

//////////////////////////////////////////////////////////////////////////

static int _cbt_prst_load_reg(cbt_storage_accessor_t* accessor, cbt_prst_reg_t** p_reg)
{
    int res = SUCCESS;
    cbt_prst_reg_t* reg = NULL;
    void* check_parameters = NULL;
    cbt_map_t* cbt_map = NULL;

    do{
        //#0x00 - start magic
        unsigned char _b[8];
        res = cbt_storage_read(accessor, _b, 8);
        if (res != SUCCESS)
            break;

        if (0 == memcmp(_b, CBT_REGISTER_END, 8)){
            log_tr("Found CBT register end");
            break;
        }
        else if (0 == memcmp(_b, CBT_REGISTER_ERROR, 8)){
            cbt_checkfs_status_t* checkfs_status = dbg_kzalloc(sizeof(cbt_checkfs_status_t), GFP_KERNEL);
            if (!checkfs_status){
                res = -ENOMEM;
                break;
            }
            do{
                {//#0x08
                    __le32 value;
                    res = cbt_storage_read(accessor, &value, sizeof(__le32));
                    if (res != SUCCESS)
                        break;

                    checkfs_status->dev_id = le32_to_cpu(value);
                }
                {//#0x0C
                    __le32 value;
                    res = cbt_storage_read(accessor, &value, sizeof(__le32));
                    if (res != SUCCESS)
                        break;
                    checkfs_status->errcode = (size_t)le32_to_cpu(value);
                }
                {//#0x10
                    __le16 value;
                    res = cbt_storage_read(accessor, &value, sizeof(__le16));
                    if (res != SUCCESS)
                        break;
                    checkfs_status->message_length = le16_to_cpu(value);
                }
                if (checkfs_status->message_length > CBT_CHECKFS_STATUS_TEST_LIMIT)
                    log_err_d("Invalid status message length ", checkfs_status->message_length);
                else if (checkfs_status->message_length > 0){
                    //#0x12
                    res = cbt_storage_read(accessor, &checkfs_status->message_text, checkfs_status->message_length);
                    if (res != SUCCESS)
                        break;
                }

                cbt_checkfs_status_log(checkfs_status);
            } while (false);
            dbg_kfree(check_parameters);
        }
        else if (0 == memcmp(_b, CBT_REGISTER_MAGIC, 8)){
            dev_t dev_id = 0;
            uint32_t check_parameters_sz = 0;
            size_t sect_in_block_degree = 0;
            sector_t device_capacity = 0ull;

            log_tr("Found CBT register start");
            {//#0x08
                __le32 value;
                res = cbt_storage_read(accessor, &value, sizeof(__le32));
                if (res != SUCCESS)
                    break;

                dev_id = le32_to_cpu(value);
            }
            {//#0x0C
                __le32 value;
                res = cbt_storage_read(accessor, &value, sizeof(__le32));
                if (res != SUCCESS)
                    break;
                sect_in_block_degree = (size_t)le32_to_cpu(value);
            }

            {//#0x10
                __le64 value;
                res = cbt_storage_read(accessor, &value, sizeof(__le64));
                if (res != SUCCESS)
                    break;
                device_capacity = (sector_t)le64_to_cpu(value);
            }
            cbt_map = cbt_map_create(sect_in_block_degree, device_capacity);
            if (cbt_map == NULL){
                res = -ENOMEM;
                break;
            }
            {//#0x18
                res = cbt_storage_read(accessor, (void*)cbt_map->generationId.b, sizeof(veeam_uuid_t));
                if (res != SUCCESS)
                    return res;
            }
            {//#0x28
                __le32 value;
                res = cbt_storage_read(accessor, (void*)&value, sizeof(__le32));
                if (res != SUCCESS)
                    break;

                cbt_map->snap_number_active = le32_to_cpu(value);
            }
            {//#0x2C - check_parameters_sz
                __le32 value;
                res = cbt_storage_read(accessor, (void*)&value, sizeof(__le32));
                if (res != SUCCESS)
                    break;

                check_parameters_sz = le32_to_cpu(value);
            }
            {//#0x30 - check_parameters
                if (check_parameters_sz != 0){
                    check_parameters = dbg_kzalloc(check_parameters_sz, GFP_KERNEL);
                    if (check_parameters == NULL){
                        res = -ENOMEM;
                        break;
                    }
                    res = cbt_storage_read(accessor, check_parameters, check_parameters_sz);
                    if (res != SUCCESS)
                        break;
                }
            }
            {//#0x30 or later
                __le64 value;
                res = cbt_storage_read(accessor, (void*)&value, sizeof(__le64));
                if (res != SUCCESS)
                    break;
                cbt_map->map_size = (size_t)le64_to_cpu(value);
            }
            {//#0x38
                page_array_t* pg_array = cbt_map->write_map;
                size_t pg_inx;
                for (pg_inx = 0; pg_inx < pg_array->pg_cnt; ++pg_inx){
                    res = cbt_storage_read(accessor, pg_array->pg[pg_inx].addr, PAGE_SIZE);
                    if (res != SUCCESS)
                        break;
                }
                if (res != SUCCESS)
                    break;
            }

            reg = _cbt_prst_reg_new(dev_id, check_parameters_sz, check_parameters, cbt_map);
            if (reg == NULL){
                res = -ENOMEM;
                break;
            }
        }
        else{
            log_warn("Cannot find CBT register magic");
            log_warn_bytes(_b, 8);
        }
    } while (false);

    if (res == SUCCESS){
        if (reg == NULL)
            res = ENODATA;
        else
            *p_reg = reg;
    }
    else{
        if (check_parameters != NULL)
            dbg_kfree(check_parameters);
        if (cbt_map != NULL)
            cbt_map_destroy(cbt_map);
        if (reg != NULL)
            _cbt_prst_reg_del(reg);
    }
    return res;
}

static int _cbt_prst_store_reg(cbt_storage_accessor_t* accessor, cbt_prst_reg_t* reg)
{
    int res = SUCCESS;
    cbt_map_t* cbt_map = reg->cbt_map;

    cbt_map_write_lock(cbt_map);
    do{
        if (!cbt_map->active){
            log_warn_dev_t("Do not store persistent CBT register for device ", reg->dev_id);
            log_warn("CBT is corrupted");
            break;
        }

        log_tr_dev_t("Store persistent CBT register for device ", reg->dev_id);
        log_tr_uuid("CBT generation id ", &cbt_map->generationId);
        log_tr_ld("Snapshot number ", cbt_map->snap_number_active);

        {//#0x00
            res = cbt_storage_write(accessor, CBT_REGISTER_MAGIC, 8);
            if (res != SUCCESS)
                break;
        }
        {//#0x08
            __le32 value = cpu_to_le32(reg->dev_id);
            res = cbt_storage_write(accessor, &value, sizeof(__le32));
            if (res != SUCCESS)
                break;
        }
        {//#0x0C
            __le32 value = cpu_to_le32(cbt_map->sect_in_block_degree);
            res = cbt_storage_write(accessor, &value, sizeof(__le32));
            if (res != SUCCESS)
                break;
        }
        {//#0x10
            __le64 value = cpu_to_le64(cbt_map->device_capacity);
            res = cbt_storage_write(accessor, &value, sizeof(__le64));
            if (res != SUCCESS)
                break;
        }
        {//#0x18
            res = cbt_storage_write(accessor, (void*)cbt_map->generationId.b, sizeof(veeam_uuid_t));
            if (res != SUCCESS)
                break;
        }
        {//#0x28
            __le32 value = cpu_to_le32(cbt_map->snap_number_active);
            res = cbt_storage_write(accessor, (void*)&value, sizeof(__le32));
            if (res != SUCCESS)
                break;
        }
        {//#0x2C - check parameters size
            __le32 value = cpu_to_le32(reg->check_parameters_sz);
            res = cbt_storage_write(accessor, (void*)&value, sizeof(__le32));
            if (res != SUCCESS)
                break;
        }
        if (reg->check_parameters_sz){//0x30
            res = cbt_storage_write(accessor, reg->check_parameters, reg->check_parameters_sz);
            if (res != SUCCESS)
                break;
        }

        {//#0x30 or less
            __le64 value = cpu_to_le64(cbt_map->map_size);
            res = cbt_storage_write(accessor, (void*)&value, sizeof(__le64));
            if (res != SUCCESS)
                break;
        }

        {//#0x38
            page_array_t* pg_array = cbt_map->write_map;
            size_t pg_inx;
            for (pg_inx = 0; pg_inx < pg_array->pg_cnt; ++pg_inx){
                res = cbt_storage_write(accessor, pg_array->pg[pg_inx].addr, PAGE_SIZE);
                if (res != SUCCESS)
                    break;

            }
            if (res != SUCCESS)
                break;
        }
    } while (false);
    cbt_map_write_unlock(cbt_map);

    return res;
}

static int _cbt_prst_store_error(cbt_storage_accessor_t* accessor, dev_t dev_id, cbt_checkfs_status_t* checkfs_status)
{
    int res;

    log_tr_dev_t("Store persistent CBT error for device ", dev_id);

    {//#0x00
        res = cbt_storage_write(accessor, CBT_REGISTER_ERROR, 8);
        if (res != SUCCESS)
            return res;
    }
    {//#0x08
        __le32 value = cpu_to_le32(dev_id);
        res = cbt_storage_write(accessor, &value, sizeof(__le32));
        if (res != SUCCESS)
            return res;
    }
    {//#0x0C
        __le32 value = cpu_to_le32(checkfs_status->errcode);
        res = cbt_storage_write(accessor, &value, sizeof(__le32));
        if (res != SUCCESS)
            return res;
    }
    {//#0x10
        __le16 value = cpu_to_le16(checkfs_status->message_length);
        res = cbt_storage_write(accessor, &value, sizeof(__le16));
        if (res != SUCCESS)
            return res;
    }
    if (checkfs_status->message_length){
        res = cbt_storage_write(accessor, checkfs_status->message_text, checkfs_status->message_length);
        if (res != SUCCESS)
            return res;
    }
    return SUCCESS;
}

static int _cbt_prst_store_end(cbt_storage_accessor_t* accessor)
{
    log_tr_lld("DEBUG! Write end indication in page # ", accessor->page_number);

    return cbt_storage_write(accessor, CBT_REGISTER_END, 8);
}

static int _cbt_prst_load_all_register(cbt_storage_accessor_t* accessor)
{
    int reg_counter = 0;
    int res = SUCCESS;

    down_write(&_cbt_prst_registers_list.lock);
    do{
        cbt_prst_reg_t* reg = NULL;
        res = _cbt_prst_load_reg(accessor, &reg);
        if (res == SUCCESS){
            log_tr_dev_t("Create persistent CBT register for device ", reg->dev_id);

            log_tr_uuid("CBT generation id ", &reg->cbt_map->generationId);
            log_tr_ld("Snapshot number ", reg->cbt_map->snap_number_active);

            ++reg_counter;
        }else{
            if (res < SUCCESS)
                log_err("Failed to load register for persistent CBT data");
            if (res == ENODATA){
                log_tr_format("All of %d registers were loaded", reg_counter);
                res = SUCCESS;
            }
            break;
        }
    } while (true);
    up_write(&_cbt_prst_registers_list.lock);
    return res;
}


static int _cbt_prst_store_all_register(cbt_storage_accessor_t* accessor)
{
    int res = SUCCESS;
    down_write(&_cbt_prst_registers_list.lock);
    {
        int count = 0;
        cbt_checkfs_status_t*  checkfs_status = dbg_kzalloc(sizeof(cbt_checkfs_status_t), GFP_KERNEL);
        if (checkfs_status == NULL)
            return -ENOMEM;

        if (!list_empty(&_cbt_prst_registers_list.headlist))
        {
            struct list_head* _current_list_head;
            list_for_each(_current_list_head, &_cbt_prst_registers_list.headlist){
                cbt_prst_reg_t* current_reg = list_entry(_current_list_head, cbt_prst_reg_t, link);

                if (current_reg->check_parameters){
                    dbg_kfree(current_reg->check_parameters);
                    current_reg->check_parameters = NULL;
                    current_reg->check_parameters_sz = 0ul;
                }

                res = cbt_checkfs_store_available(current_reg->dev_id, &current_reg->check_parameters_sz, &current_reg->check_parameters, checkfs_status);
                if (res == SUCCESS){
                    res = _cbt_prst_store_reg(accessor, current_reg);
                    if (res < SUCCESS){
                        log_err("Failed to store register for persistent CBT data");
                        break;
                    }
                }
                else{
                    res = _cbt_prst_store_error(accessor, current_reg->dev_id, checkfs_status);
                    if (res < SUCCESS){
                        log_err("Failed to store register for persistent CBT data");
                        break;
                    }
                }
                ++count;
            }
        }
        dbg_kfree(checkfs_status);

        if (count)
            log_tr_format("Stored %d CBT registers", count);

        if (SUCCESS != _cbt_prst_store_end(accessor))
            log_err("Failed to write end indication for CBT data list");
        
    }
    up_write(&_cbt_prst_registers_list.lock);
    return res;
}

static int _cbt_prst_treacker_create(dev_t dev_id, cbt_map_t* cbt_map)
{
    tracker_t* tracker = NULL;

    int res = tracker_find_by_dev_id(dev_id, &tracker);
    if (SUCCESS == res){
        log_tr_format("Device %d:%d is already under tracking", MAJOR(dev_id), MINOR(dev_id));
        res = EALREADY;
    }
    else if (-ENODATA == res){
        res = tracker_create(0ull, dev_id, 0, cbt_map, &tracker);
        if (SUCCESS != res)
            log_err_d("Failed to create tracker. errno=", res);
    }
    else
        log_err_d("Failed to find device under tracking. errno=", res);

    return res;
}
/*

static int _cbt_prst_start_tracker(dev_t dev_id)
{
    int res = ENODEV;
    log_tr_dev_t("[TBD] Start tracking for device ", dev_id);
    down_read(&_cbt_prst_registers_list.lock);
    if (!list_empty(&_cbt_prst_registers_list.headlist)){
        struct list_head* _current_list_head;
        list_for_each(_current_list_head, &_cbt_prst_registers_list.headlist){
            cbt_prst_reg_t* current_reg = list_entry(_current_list_head, cbt_prst_reg_t, link);

            if (dev_id == current_reg->dev_id){
                res = _cbt_prst_treacker_create(current_reg->dev_id, current_reg->cbt_map);
                break;
            }
        }
    }
    up_read(&_cbt_prst_registers_list.lock);

    return res;
}*/

static void _cbt_prst_try_start_tracker_for_notified_device(cbt_notify_dev_t* dev_note)
{
    if (dev_note->state == NOTIFY_DEV_STATE_IN_SYSTEM){

        down_read(&_cbt_prst_registers_list.lock);
        if (!list_empty(&_cbt_prst_registers_list.headlist)){
            struct list_head* _current_list_head;
            list_for_each(_current_list_head, &_cbt_prst_registers_list.headlist){
                cbt_prst_reg_t* current_reg = list_entry(_current_list_head, cbt_prst_reg_t, link);

                if (dev_note->dev_id == current_reg->dev_id){
                    dev_t dev_id = current_reg->dev_id;

                    int res = cbt_checkfs_start_tracker_available(dev_id, current_reg->check_parameters_sz, current_reg->check_parameters);
                    if (res == SUCCESS){
                        log_tr_dev_t("Tracking is available for the device ", dev_id);

                        res = _cbt_prst_treacker_create(dev_id, current_reg->cbt_map);
                        if (res == SUCCESS)
                            dev_note->state = NOTIFY_DEV_STATE_UNDER_TRACKING;
                        else if (res == ENODEV)
                            log_tr_dev_t("Tracking is not configured for the device ", dev_id);
                        else if (res == EALREADY)
                            log_tr_dev_t("Tracking is already configured for the device ", dev_id);
                        else
                            log_err_dev_t("Failed to start tracking for the device ", dev_id);
                    }
                    else if (res == EPERM)
                        log_tr_dev_t("Loading tracking is not permitted for the device ", dev_id);
                    else
                        log_err_dev_t("Failed to check tracking availability for the device ", dev_id);
                }
            }
        }
        up_read(&_cbt_prst_registers_list.lock);
    }
}

static int _cbt_prst_load(void)
{
    int res = SUCCESS;
    cbt_storage_accessor_t accessor_data = { 0 };
    cbt_storage_accessor_t* accessor = &accessor_data;

    if (_cbt_is_load)
        return EALREADY;

    log_tr("Loading persistent CBT data");

    res = cbt_storage_open(&_cbt_params, accessor);
    if (res != SUCCESS) {
        log_err("Cannot open persistent CBT data");
        return res;
    }
    
    BUG_ON(NULL == accessor->device);
    BUG_ON(NULL == accessor->pg);
    BUG_ON(NULL == accessor->page);
    do {
        res = cbt_storage_check(accessor);
        if (res != SUCCESS) {
            log_err("Corrupted persistent CBT data");
            break;
        }

        res = cbt_storage_prepare4read(accessor);
        if (res < SUCCESS) {
            log_err("Corrupted persistent CBT data");
            break;
        }
        else if (res == ENODATA) {
            log_tr("Empty persistent CBT data");
            res = SUCCESS;
            break;
        }

        res = _cbt_prst_load_all_register(accessor);
        if (res != SUCCESS) {
            log_err("Failed to load persistent CBT data registers");
            break;
        }
        _cbt_is_load = true;
    } while (false);
    cbt_storage_close(accessor);

    if (res != SUCCESS)
        log_err("Failed to load persistent CBT data");
    return res;
}

int cbt_persistent_load(void)
{
    int res = _cbt_prst_load();
    if (res != SUCCESS)
        return res;

    down_read(&_cbt_prst_registers_list.lock);
    if (!list_empty(&_cbt_prst_registers_list.headlist))
    {
        struct list_head* _current_list_head;
        list_for_each(_current_list_head, &_cbt_prst_registers_list.headlist)
        {
            cbt_prst_reg_t* current_reg = list_entry(_current_list_head, cbt_prst_reg_t, link);
            struct block_device* blk_dev;

            if (SUCCESS == blk_dev_open(current_reg->dev_id, &blk_dev)){
                cbt_notify_list_down_read();
                {
                    cbt_notify_dev_t* dev_note = cbt_notify_dev_find_by_id(current_reg->dev_id);
                    if (dev_note == NULL){
#if 0
                        char dev_name[BDEVNAME_SIZE + 1];
                        memset(dev_name, 0, BDEVNAME_SIZE + 1);
                        if (bdevname(current_reg->dev_id, dev_name)){
                            dev_note = __cbt_prst_notify_dev_new(current_reg->dev_id, dev_name, NULL);

                            dev_note->state = NOTIFY_DEV_STATE_IN_SYSTEM;

                            log_tr_format("Device [%s] detected in system", dev_note->dev_name);
                        }
#else
                        if (current_reg->cbt_map){
                            int res = _cbt_prst_treacker_create(current_reg->dev_id, current_reg->cbt_map);
                            if (res == SUCCESS)
                                log_tr_dev_t("Tracking started for the device ", current_reg->dev_id);
                            else if (res == EALREADY)
                                log_tr_dev_t("Tracking already started for the device ", current_reg->dev_id);
                            else
                                log_err_dev_t("Failed to start tracking for the device ", current_reg->dev_id);
                        }else{
                            //defense from deadlock on _cbt_prst_registers_list.lock
                        }
#endif
                    }
                    else
                        log_tr_format("Device [%s] already notified", dev_note->dev_name);
                }
                cbt_notify_list_up_read();

                blk_dev_close(blk_dev);
            }
        }
    }
    up_read(&_cbt_prst_registers_list.lock);

    cbt_notify_list_down_read();
    cbt_notify_list_foreach(&_cbt_prst_try_start_tracker_for_notified_device);
    cbt_notify_list_up_read();

    return SUCCESS;
}

int cbt_persistent_store(void)
{
    int res = SUCCESS;

    cbt_storage_accessor_t accessor_data = { 0 };
    cbt_storage_accessor_t* accessor = &accessor_data;

    res = cbt_storage_open(&_cbt_params, accessor);
    if (res != SUCCESS) {
        log_err("Cannot open persistent CBT data");
        return res;
    }

    do {
        res = cbt_storage_check(accessor);
        if (res != SUCCESS) {
            log_err("Corrupted persistent CBT data");
            break;
        }

        res = cbt_storage_prepare4write(accessor);
        if (res != SUCCESS) {
            log_err("Corrupted persistent CBT data");
            break;
        }

        res = _cbt_prst_store_all_register(accessor);
        if (res != SUCCESS) {
            log_err("Failed to store persistent CBT data registers");
            break;
        }

        res = cbt_storage_write_finish(accessor);
        if (res != SUCCESS) {
            log_err("Failed to store persistent CBT data registers");
            break;
        }
    } while (false);
    cbt_storage_close(accessor);

    if (res != SUCCESS)
        log_err("Failed to store persistent CBT data");
    return res;
}

void cbt_persistent_cbtdata_free(void)
{
    rangevector_done(&_cbt_params.rangevector);
    _cbt_params.dev_id = 0;
}

int cbt_persistent_cbtdata_new(const char* cbtdata)
{
    int res = SUCCESS;

    if (_cbt_params.dev_id != 0)
        cbt_persistent_cbtdata_free();

    log_tr("Parsing persistent CBT parameters");
    res = cbt_prst_parse_parameters(cbtdata, &_cbt_params);
    if (res != SUCCESS)
        log_err_d("Failed to parse CBT persistent parameters. errcode=", res);
    else 
    {//DEBUG! Show CBT data location
        range_t* p_range = NULL;

        log_tr("DEBUG! Persistent CBT data location (in sectors):");
        RANGEVECTOR_READ_LOCK(&_cbt_params.rangevector);
        RANGEVECTOR_FOREACH_BEGIN(&_cbt_params.rangevector, p_range)
        {
            log_tr_range("DEBUG!    ", p_range);
        }
        RANGEVECTOR_FOREACH_END();
        RANGEVECTOR_READ_UNLOCK(&_cbt_params.rangevector);
    }

    return res;
}

int cbt_persistent_init(const char* cbtdata)
{
    INIT_LIST_HEAD(&_cbt_prst_registers_list.headlist);
    init_rwsem(&_cbt_prst_registers_list.lock);

    cbt_notify_init();

	if (cbtdata == NULL) {
		log_warn("Persistent CBT parameters are not set");
		return ENODATA;
	}

    return cbt_persistent_cbtdata_new(cbtdata);
}

void cbt_persistent_done(void )
{
    if (_cbt_params.dev_id != 0){

        log_tr("Storing persistent CBT data");
        {
            int res = cbt_persistent_store();
            if (res != SUCCESS)
                log_err_d("Failed to store persistent CBT data. Error code ", res);
        }
        cbt_persistent_cbtdata_free();
    }

    down_write(&_cbt_prst_registers_list.lock);
    while (!list_empty(&_cbt_prst_registers_list.headlist)){
        cbt_prst_reg_t* reg = list_entry(_cbt_prst_registers_list.headlist.next, cbt_prst_reg_t, link);

        _cbt_prst_reg_del(reg);
    }
    up_write(&_cbt_prst_registers_list.lock);

    cbt_notify_done();
}


static bool _cbt_persistent_have_device_cbtdata(dev_t dev_id)
{
    return (dev_id == _cbt_params.dev_id);
    //to do: checking device by FS UUID.
}

//////////////////////////////////////////////////////////////////////////

void cbt_persistent_register(dev_t dev_id, cbt_map_t* cbt_map)
{
    //check exist and add
    down_write(&_cbt_prst_registers_list.lock);
    {
        cbt_prst_reg_t* reg = _cbt_prst_reg_find(dev_id);
        if (reg == NULL)
            reg = _cbt_prst_reg_new(dev_id, 0, NULL, cbt_map);
        else
            log_err_format("Device [%d:%d] already registered", MAJOR(reg->dev_id), MINOR(reg->dev_id));
    }
    up_write(&_cbt_prst_registers_list.lock);
}

void cbt_persistent_unregister(dev_t dev_id)
{
    int res = -ENODATA;
    //try find and delete
    down_write(&_cbt_prst_registers_list.lock);
    {
        cbt_prst_reg_t* reg = _cbt_prst_reg_find(dev_id);
        if (reg != NULL)
            _cbt_prst_reg_del(reg);
    }
    up_write(&_cbt_prst_registers_list.lock);

    if (res != SUCCESS){
        if (res == -ENODATA){
            log_tr_format("Cannot unregister device [%d:%d] from persistent CBT", MAJOR(dev_id), MINOR(dev_id));
            log_tr("Device not found");
        }
        else{
            log_err_format("Failed to unregister device [%d:%d] from persistent CBT", MAJOR(dev_id), MINOR(dev_id));
            log_err_d("Device not found. Error code ", res);
        }
    }
}


bool cbt_persistent_device_filter(dev_t dev_id)
{
    //processing filter
    switch (MAJOR(dev_id)) //https://www.kernel.org/doc/Documentation/admin-guide/devices.txt
    {
    case 2: //floppy
        return false;
    case 7: //loopback devices
        return false;
    case 11://CD-ROM
        return false;

    default:
        return true;
    }
}

void cbt_persistent_device_attach(char* dev_name, char* dev_path)
{

    dev_t dev_id;
    {
#ifdef LOOKUP_BDEV_MASK
        struct block_device* bdev = lookup_bdev(dev_name, 0);
#else
        struct block_device* bdev = lookup_bdev(dev_name);
#endif
        if (IS_ERR(bdev)){
            log_err_s("Failed to find device by name ", dev_name);
            return;
        }

        //log_tr("DEBUG! lookup_bdev complete success");
        if (bdev == NULL){
            log_err_s("Cannot find device by name ", dev_name);
            return;
        }

        dev_id = bdev->bd_dev;
        if (!cbt_persistent_device_filter(dev_id))
            return;

        log_tr_dev_t("Found device ", dev_id);

        //disk information
        if (bdev->bd_disk)
            log_tr_s("Found disk ", bdev->bd_disk->disk_name);

        //partition information
        if (bdev->bd_part != NULL){
            struct hd_struct *	bd_part = bdev->bd_part;

            //log_tr_d("Partition # ", bd_part->partno);
            //log_tr_sect("Partition sector start at ", bd_part->start_sect);
            //log_tr_sect("Partition sector size ", bd_part->nr_sects);

            if (bd_part->info) {
                struct partition_meta_info *info = bd_part->info;

                log_tr("Partition information found");
                log_tr_s("volume uuid ", info->uuid);
                log_tr_s("volume name ", info->volname);
            }
        }
    }

    // Lock allows only one block device to be processed at a time
    mutex_lock(&_cbt_prst_attach_locker);
    do{
        cbt_notify_dev_t* dev_note = NULL;


        dev_note = cbt_notify_dev_new(dev_id, dev_name, dev_path);
        if (dev_note == NULL){
            log_err_dev_t("Failed to allocate memory for new device notification", dev_id);
            break;
        }

        dev_note->state = NOTIFY_DEV_STATE_IN_SYSTEM;

        if (_cbt_is_load)
            _cbt_prst_try_start_tracker_for_notified_device(dev_note);

        if (_cbt_persistent_have_device_cbtdata(dev_id)){
            int res = _cbt_prst_load();
            if (res == SUCCESS){
                cbt_notify_list_down_read();
                cbt_notify_list_foreach(&_cbt_prst_try_start_tracker_for_notified_device);
                cbt_notify_list_up_read();
            }
            else if (res == EALREADY)
                log_tr("CBT persistent data is already loaded.");
            else
                log_err_d("Failed to read CBT persistent data. errcode=", res);
        }
    } while (false);
    mutex_unlock(&_cbt_prst_attach_locker);

}

void cbt_persistent_device_detach(char* dev_name, char* dev_path)
{
    dev_t dev_id = 0;
    tracker_t* tracker = NULL;

    int res = cbt_notify_dev_seek_and_delete(dev_name, &dev_id);
    if (SUCCESS != res)
        return;

    res = tracker_find_by_dev_id(dev_id, &tracker);
    if (res == SUCCESS){
        log_warn_dev_t("Detach device from tracking", dev_id);

        res = tracker_remove(tracker);
        if (res != SUCCESS){
            log_err_dev_t("Failed to remove device from tracking", dev_id);
        }
    }

}
#endif //PERSISTENT_CBT