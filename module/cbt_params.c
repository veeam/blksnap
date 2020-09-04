#include "stdafx.h"
#ifdef PERSISTENT_CBT

#include "sector.h"
#include "cbt_params.h"
#include "cbt_storage.h"

#define SECTION "cbt_params"
#include "log_format.h"


static int _cbt_prst_parse_device(char* str, dev_t* p_device)
{
    int res = SUCCESS;
    int major = 0;
    int minor = 0;
    char* separator = NULL;

    log_tr_s("DEBUG! parsing device: ", str);

    separator = strchr(str, ':');
    if (separator == NULL){
        log_err_s("Cannot find delimeter: ", str);
        return -EINVAL;
    }

    separator[0] = '\0';
    ++separator;

    res = kstrtouint(str, 10, &major);
    if (SUCCESS != res){
        log_err_s("Failed to parse: ", str);
        return res;
    }

    str = separator;

    res = kstrtouint(str, 10, &minor);
    if (SUCCESS != res){
        log_err_s("Failed to parse: ", str);
        return res;
    }

    *p_device = MKDEV(major, minor);
    return res;
}

static int _cbt_prst_parse_part_uuid(char* str, veeam_uuid_t* p_part_uuid)
{
    char tmp[3] = {'\0', '\0', '\0'};
    __kernel_size_t ch_cnt = 0;
    __kernel_size_t pos = 0;
    int inx = 0;

    log_tr_s("DEBUG! parsing uuid: ", str);

    while ((str[pos] != '\0') && (inx < sizeof(veeam_uuid_t))){
        char ch = str[pos];
        if (ch == '-'){
            ++pos;
            continue;
        }

        if (ch_cnt == 0){
            tmp[ch_cnt] = ch;
            ++ch_cnt;
        }
        else if (ch_cnt == 1) {
            int res = SUCCESS;
            tmp[ch_cnt] = ch;
            ch_cnt = 0;
    
            res = kstrtou8(tmp, 16, &p_part_uuid->b[inx]);
            if (SUCCESS != res){
                log_err_s("Failed to parse: ", tmp);
                return res;
            }
            

            ++inx;
        }

        ++pos;
    }
    return SUCCESS;
}

static int _cbt_prst_parse_range(char* str, u64* p_ofs, u64* p_len)
{
    char* separator = NULL;

    log_tr_s("DEBUG! parsing range: ", str);

    separator = strchr(str, ':');
    if (separator == NULL){
        log_err_s("Cannot find delimeter: ", str);
        return -EINVAL;
    }

    separator[0] = '\0';
    ++separator;

    {
        int res = kstrtou64(str, 10, p_ofs);
        if (SUCCESS != res){
            log_err_s("Failed to parse offset: ", str);
            return res;
        }
    }

    str = separator;

    {
        int res = kstrtou64(str, 10, p_len);
        if (SUCCESS != res){
            log_err_s("Failed to parse length: ", str);
            return res;
        }
    }

    return SUCCESS;
}

static int _cbt_prst_parse_rangevector(char* str, rangevector_t* rangevector)
{
    int res = SUCCESS;
    u64 ofs = 0;
    u64 len = 0;
    range_t rg = {0};
    char* separator = NULL;

    log_tr_s("DEBUG! parsing rangevector: ", str);

    while (NULL != (separator = strchr(str, ','))){
        separator[0] = '\0';
        ++separator;

        res = _cbt_prst_parse_range(str, &ofs, &len);
        if (res != SUCCESS){
            log_err_s("Failed to parse range: ", str);
            return res;
        }
        rg.ofs = sector_from_streamsize(ofs);
        rg.cnt = sector_from_streamsize(len);
        res = rangevector_add(rangevector, &rg);
        if (res != SUCCESS){
            log_err_d("Cannot add range to rangevector. errcode= ", res);
            return res;
        }
        str = separator;
    }
    if (str[0] != '\0'){
        res = _cbt_prst_parse_range(str, &ofs, &len);
        if (res != SUCCESS){
            log_err_s("Failed to parse range: ", str);
            return res;
        }

        rg.ofs = sector_from_streamsize(ofs);
        rg.cnt = sector_from_streamsize(len);
        res = rangevector_add(rangevector, &rg);
        if (res != SUCCESS){
            log_err_d("Cannot add range to rangevector. errcode= ", res);
            return res;
        }
    }

    return res;
}

int cbt_prst_parse_parameters(const char* params_str, cbt_persistent_parameters_t* cbt_data)
{
    int res = SUCCESS;
    __kernel_size_t pos = 0;
    __kernel_size_t substr_begin = 0;
    __kernel_size_t param_index = 0;
    char* cbtdata_str = NULL;
    __kernel_size_t length = 0;

    if (params_str == NULL){
        log_err("Parameter is null");
        return -EINVAL;
    }
    length = strnlen(params_str, PAGE_SIZE); //string limited

    cbtdata_str = kzalloc(length + 1, GFP_KERNEL);
    if (cbtdata_str == NULL){
        log_err("Cannot allocate memory for parameter string");
        return -ENOMEM;
    }
    memcpy(cbtdata_str, params_str, length);

    rangevector_init(&cbt_data->rangevector, false);

    while (cbtdata_str[pos] != '\0'){
        char ch = cbtdata_str[pos];

        if ((ch == ';') || (ch == '.')){
            cbtdata_str[pos] = '\0';

            switch (param_index){
            case 0://dev_t device
                res = _cbt_prst_parse_device(&cbtdata_str[substr_begin], &cbt_data->dev_id);
                if (res != SUCCESS)
                    log_err_d("Failed to parse device id. errcode=", res);
                break;
            case 1://uuid_t part_uuid
                res = _cbt_prst_parse_part_uuid(&cbtdata_str[substr_begin], &cbt_data->part_uuid);
                if (res != SUCCESS)
                    log_err_d("Failed to parse partition UUID. errcode=", res);
                break;
            case 2://rangevector_t rangevector;
                res = _cbt_prst_parse_rangevector(&cbtdata_str[substr_begin], &cbt_data->rangevector);
                if (res != SUCCESS)
                    log_err_d("Failed to parse rangevector. errcode=", res);
                break;
            }
            ++param_index;
            ++pos;
            substr_begin = pos;
        }
        else
            ++pos;

        if (ch == '.')
            break;

        if (res != SUCCESS)
            break;
    };
    kfree(cbtdata_str);

    if (res != SUCCESS){
        rangevector_done(&cbt_data->rangevector);
        return res;
    }

    return SUCCESS;
}

#endif//PERSISTENT_CBT
