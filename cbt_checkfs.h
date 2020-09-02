#pragma once

#ifdef PERSISTENT_CBT

#define CBT_CHECKFS_STATUS_TEST_LIMIT 1000

typedef struct cbt_checkfs_status_s
{
    int32_t dev_id;
    int32_t errcode;
    uint16_t message_length;
    char message_text[CBT_CHECKFS_STATUS_TEST_LIMIT];
}cbt_checkfs_status_t;

//void cbt_checkfs_status_set(cbt_checkfs_status_t* checkfs_status, dev_t dev_id, int errcode, char* message);
void cbt_checkfs_status_log(cbt_checkfs_status_t* checkfs_status);

int cbt_checkfs_start_tracker_available(dev_t dev_id, uint32_t check_parameters_sz, void* check_parameters);
int cbt_checkfs_store_available(dev_t dev_id, uint32_t* p_check_parameters_sz, void** p_check_parameters, cbt_checkfs_status_t* error_status);

#endif //PERSISTENT_CBT