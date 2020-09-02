#pragma once

#ifdef PERSISTENT_CBT

#include "cbt_map.h"

int cbt_persistent_init(const char* cbtdata);
void cbt_persistent_done(void);

int cbt_persistent_cbtdata_new(const char* cbtdata);
void cbt_persistent_cbtdata_free(void);

int cbt_persistent_load(void);
int cbt_persistent_store(void);

void cbt_persistent_register(dev_t dev_id, cbt_map_t* cbt_map);
void cbt_persistent_unregister(dev_t dev_id);

void cbt_persistent_device_attach(char* dev_name, char* dev_path);
void cbt_persistent_device_detach(char* dev_name, char* dev_path);

#endif //PERSISTENT_CBT