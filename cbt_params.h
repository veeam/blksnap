#pragma once

#ifdef PERSISTENT_CBT

#include "uuid_util.h"
#include "rangevector.h"

typedef struct
{
    dev_t dev_id;
    veeam_uuid_t part_uuid;
    rangevector_t rangevector;
}cbt_persistent_parameters_t;

int cbt_prst_parse_parameters(const char* params_str, cbt_persistent_parameters_t* cbt_data);

#endif//PERSISTENT_CBT