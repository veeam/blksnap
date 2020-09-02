#include "stdafx.h"

#ifdef PERSISTENT_CBT

#include "cbt_notify.h"

#include "rangevector.h"
#include "blk_direct.h"
#include "tracker.h"

#define SECTION "cbt_notify"
#include "log_format.h"


//////////////////////////////////////////////////////////////////////////
// global variables 
//////////////////////////////////////////////////////////////////////////
typedef struct cbt_prst_list_s
{
    struct list_head headlist;
    struct rw_semaphore lock;
}cbt_prst_list_t;

static cbt_prst_list_t _cbt_prst_notify_devices_list = { { 0 } }; // list of notified devices. struct cbt_prst_notify_dev_t


static void _cbt_notify_dev_free(cbt_notify_dev_t* dev_note)
{
    if (dev_note->dev_name)
        dbg_kfree(dev_note->dev_name);
    if (dev_note->dev_path)
        dbg_kfree(dev_note->dev_path);
    dbg_kfree(dev_note);
}

static void _cbt_notify_dev_del(cbt_notify_dev_t* dev_note)
{
    list_del(&dev_note->link);
    _cbt_notify_dev_free(dev_note);
}


void cbt_notify_init(void)
{
    INIT_LIST_HEAD(&_cbt_prst_notify_devices_list.headlist);
    init_rwsem(&_cbt_prst_notify_devices_list.lock);
}

void cbt_notify_done(void)
{
    log_tr("Cleanup devices notify list");
    cbt_notify_list_down_write();
    while (!list_empty(&_cbt_prst_notify_devices_list.headlist)){
        cbt_notify_dev_t* dev_note = list_entry(_cbt_prst_notify_devices_list.headlist.next, cbt_notify_dev_t, link);
        _cbt_notify_dev_del(dev_note);
    }
    cbt_notify_list_up_write();
}

struct list_head* cbt_notify_list_get(void)
{
    return &_cbt_prst_notify_devices_list.headlist;
}

void cbt_notify_list_foreach(cbt_notify_list_foreach_cb_t cb)
{
    if (!list_empty(&_cbt_prst_notify_devices_list.headlist))
    {
        struct list_head* _current_list_head;
        list_for_each(_current_list_head, &_cbt_prst_notify_devices_list.headlist){
            cbt_notify_dev_t* dev_note = list_entry(_current_list_head, cbt_notify_dev_t, link);
            cb(dev_note);
        }
    }
}

void cbt_notify_list_down_write(void)
{
    down_write(&_cbt_prst_notify_devices_list.lock);
}

void cbt_notify_list_up_write(void)
{
    up_write(&_cbt_prst_notify_devices_list.lock);
}

void cbt_notify_list_down_read(void)
{
    down_read(&_cbt_prst_notify_devices_list.lock);
}

void cbt_notify_list_up_read(void)
{
    up_read(&_cbt_prst_notify_devices_list.lock);
}

cbt_notify_dev_t* cbt_notify_dev_new(dev_t dev_id, char* dev_name, char* dev_path)
{
    int res = SUCCESS;
    cbt_notify_dev_t* dev_note = NULL;

    cbt_notify_list_down_write();
    do{
        dev_note = dbg_kzalloc(sizeof(cbt_notify_dev_t), GFP_KERNEL);
        if (dev_note == NULL){
            log_err("Failed to allocate memory for persistent CBT register");
            res = -ENOMEM;
            break;
        }
        INIT_LIST_HEAD(&dev_note->link);
        dev_note->dev_id = dev_id;
        dev_note->state = NOTIFY_DEV_STATE_UNDEFINED;

        {
            int sz = strlen(dev_name);
            dev_note->dev_name = dbg_kzalloc(sz + 1, GFP_KERNEL);
            if (dev_note->dev_name == NULL){
                log_err("Failed to allocate memory for persistent CBT register data");
                res = -ENOMEM;
                break;
            }
            memcpy(dev_note->dev_name, dev_name, sz);
        }
        if (dev_path != NULL){
            int sz = strlen(dev_path);
            dev_note->dev_path = dbg_kzalloc(sz + 1, GFP_KERNEL);
            if (dev_note->dev_path == NULL){
                log_err("Failed to allocate memory for persistent CBT register data");
                res = -ENOMEM;
                break;
            }
            memcpy(dev_note->dev_path, dev_path, sz);
        }
    } while (false);

    if (res == SUCCESS)
        list_add(&dev_note->link, &_cbt_prst_notify_devices_list.headlist);
    else{
        if (dev_note != NULL){
            _cbt_notify_dev_free(dev_note);
            dev_note = NULL;
        }
    }
    cbt_notify_list_up_write();

    return dev_note;
}

cbt_notify_dev_t* cbt_notify_dev_find_by_id(dev_t dev_id)
{
    if (!list_empty(&_cbt_prst_notify_devices_list.headlist))
    {
        struct list_head* _current_list_head;
        list_for_each(_current_list_head, &_cbt_prst_notify_devices_list.headlist)
        {
            cbt_notify_dev_t* current_dev_note = list_entry(_current_list_head, cbt_notify_dev_t, link);
            if (dev_id == current_dev_note->dev_id)
                return current_dev_note;
        }
    }
    return NULL;
}

cbt_notify_dev_t* cbt_notify_dev_find_by_name(char* dev_name)
{
    if (!list_empty(&_cbt_prst_notify_devices_list.headlist))
    {
        struct list_head* _current_list_head;
        list_for_each(_current_list_head, &_cbt_prst_notify_devices_list.headlist)
        {
            cbt_notify_dev_t* current_dev_note = list_entry(_current_list_head, cbt_notify_dev_t, link);
            if (dev_name == current_dev_note->dev_name)
                return current_dev_note;
        }
    }
    return NULL;
}



int cbt_notify_dev_seek_and_delete(char* dev_name, dev_t* dev_id)
{
    int res = -ENODEV;

    cbt_notify_list_down_write();
    {
        cbt_notify_dev_t* dev_note = cbt_notify_dev_find_by_name(dev_name);
        if (dev_note){
            *dev_id = dev_note->dev_id;
            res = SUCCESS;

            _cbt_notify_dev_del(dev_note);
        }
    }
    cbt_notify_list_up_write();
    return res;
}

#endif //PERSISTENT_CBT
