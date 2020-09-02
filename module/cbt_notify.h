#pragma once

#ifdef PERSISTENT_CBT

enum cbt_prst_notify_dev_state
{
    NOTIFY_DEV_STATE_UNDEFINED = 0,
    NOTIFY_DEV_STATE_IN_SYSTEM,
    NOTIFY_DEV_STATE_UNDER_TRACKING
};
typedef struct cbt_prst_notify_dev_s
{
    struct list_head link;
    dev_t dev_id;
    char* dev_name;
    char* dev_path;
    enum cbt_prst_notify_dev_state state;
}cbt_notify_dev_t;

void cbt_notify_init(void);
void cbt_notify_done(void);

typedef void(*cbt_notify_list_foreach_cb_t)(cbt_notify_dev_t*);
void cbt_notify_list_foreach(cbt_notify_list_foreach_cb_t cb);

void cbt_notify_list_down_write(void);
void cbt_notify_list_up_write(void);
void cbt_notify_list_down_read(void);
void cbt_notify_list_up_read(void);

cbt_notify_dev_t* cbt_notify_dev_new(dev_t dev_id, char* dev_name, char* dev_path);
int cbt_notify_dev_seek_and_delete(char* dev_name, dev_t* dev_id);

cbt_notify_dev_t* cbt_notify_dev_find_by_id(dev_t dev_id);
cbt_notify_dev_t* cbt_notify_dev_find_by_name(char* dev_name);

#endif //PERSISTENT_CBT