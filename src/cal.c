/*
 * cal.c
 *
 *  Created on: May 20, 2023
 *      Author: luca
 */


#include "cal.h"

#define NUMBER_OF_CONFIG_PARAMS 14

static config_t prvConfig;
static SemaphoreHandle_t prvConfigMutex = NULL;


enum {
    ID_LOAD_DEFAULT_CONFIG = 0,
    ID_QUERY_CONFIG,
    ID_UPDATE_CONFIG,
    ID_BALANCING_FEEDBACK,
    ID_SOC_LOOKUP,
    ID_SET_RTC,
    ID_CONTROL_CALIBRATION,
    ID_CALIBRATION_STATE,
    ID_CALIBRATION_VALUE,
    ID_FORMAT_SD_CARD,
    ID_FORMAT_SD_CARD_STATUS,
    ID_QUERY_LOGFILE_INFO,
    ID_RESTART_SYSTEM,
    ID_TRANSFER_LOGFILE
};

enum {
    RW, //Read-Write
    RO, //Read only
    WO  //Write only
};

typedef struct {
    uint8_t ID;
    uint8_t modifier;
    uint8_t dataTypeLength;
} param_type_t;

static param_type_t prvParamTypes[NUMBER_OF_CONFIG_PARAMS] = {
        {ID_LOAD_DEFAULT_CONFIG,         WO, 0},
        {ID_QUERY_CONFIG,                RO, 0},
        {ID_UPDATE_CONFIG,               WO, sizeof(config_t)},
        {ID_BALANCING_FEEDBACK,          RO, sizeof(uint16_t)},
        {ID_SOC_LOOKUP,                  WO, 0},
        {ID_SET_RTC,                 RW, sizeof(uint32_t)},
        {ID_CONTROL_CALIBRATION,         WO, sizeof(uint8_t)},
        {ID_CALIBRATION_STATE,           RO, sizeof(uint8_t)},
        {ID_CALIBRATION_VALUE,           WO, sizeof(float)},
        {ID_FORMAT_SD_CARD,              WO, 0},
        {ID_FORMAT_SD_CARD_STATUS,       RO, 1},
        {ID_QUERY_LOGFILE_INFO,          RO, 0},
        {ID_RESTART_SYSTEM,              WO, 0},
        {ID_TRANSFER_LOGFILE,            WO, 1}
};

static config_t prvDefaultConfig = {
        false,
        40000,
        false,
        MAX_NUM_OF_SLAVES,
        false,
        false,
        true,
        0
};

static bool prv_find_param_type(uint8_t ID, param_type_t *found);
static void prv_update_param(param_type_t *param, void *value);
static void prv_get_param(param_type_t *param);
static void prv_send_negative_response(uint8_t ID, uint8_t reason);
static void prv_send_positive_response(uint8_t ID);
static void prv_send_response(uint8_t *data, size_t len);
static BaseType_t prv_config_mutex_take(TickType_t blocktime);
static void prv_load_config(void);
static bool prv_write_config(void);
static bool prv_default_config(void);
static void prv_print_config(void);
static void prv_new_config(config_t *config);
static void prv_send_config(void);
static bool prv_format_file_list(FILINFO *entries, uint8_t numberOfEntries, uint8_t *buffer);
static bool prv_find_file_from_handle(uint8_t handle, char *name);


void init_cal(void) {
    prvConfigMutex = xSemaphoreCreateMutex();
    configASSERT(prvConfigMutex);
    prv_load_config();
}

config_t* get_config(TickType_t blocktime) {
    if (prv_config_mutex_take(blocktime)) {
        return &prvConfig;
    } else {
        return NULL;
    }
}

bool copy_config(config_t *dest, TickType_t blocktime) {
    config_t *src = get_config(blocktime);
    if (src != NULL) {
        memcpy(dest, src, sizeof(config_t));
        release_config();
        return true;
    } else {
        return false;
    }
}

void release_config(void) {
    xSemaphoreGive(prvConfigMutex);
}

void handle_cal_request(uint8_t *data, size_t len) {
    PRINTF("len: %u\n", len);
    uint8_t ID = data[1];
    uint8_t numberOfBytes = data[2];
    uint8_t modify = ID & 0x80; //Read or modify value requested?
    uint16_t DLC = len - 1; //First byte is message type and needs to be ignored in the cal protocol
    param_type_t param;
    uint8_t localBuffer[len];
    memcpy(localBuffer, data, len);

    if (!prv_find_param_type(ID & 0x7F, &param)) {
        PRINTF("Error: Requested parameter does not exist!\n");
        prv_send_negative_response(ID, ERROR_PARAM_DOES_NOT_EXIST); //Requested parameter does not exist
        return;
    } else {
        if (modify && param.modifier == RO) {
            PRINTF("Error: Requested the modification of a read only value!\n");
            prv_send_negative_response(ID, ERROR_CANNOT_MODIFY_RO_PARAMETER); //Requested the modification of a read only value
            return;
        }
        if (modify && ((numberOfBytes + 2) != DLC)) {
            PRINTF("Error: DLC does not match the expected number of bytes!\n");
            prv_send_negative_response(ID, ERROR_DLC_DOES_NOT_MATCH_NUMBER_OF_BYTES); //DLC does not match the expected number of bytes
            return;
        }
        if (modify && param.dataTypeLength != numberOfBytes) {
            PRINTF("Error: Number of transmitted bytes does not match the length of the datatype!\n");
            prv_send_negative_response(ID, ERROR_NUMBER_OF_BYTES_DOES_NOT_MATCH_DATATYPE); //Number of transmitted bytes does not match the length of the datatype
            return;
        }
        if (!modify && param.modifier == WO) {
            PRINTF("Cannot read a write-only parameter!\n");
            prv_send_negative_response(ID, ERROR_CANNOT_READ_WO_PARAMETER);
            return;
        }
    }

    if (modify) {
        //Set value
        prv_update_param(&param, &data[3]);
    } else {
        //Read value
        prv_get_param(&param);
    }
}

static void prv_update_param(param_type_t *param, void *value) {
    uint8_t ID = param->ID;

    //Only the RW and WO parameters are listed here
    switch (ID) {

    case ID_LOAD_DEFAULT_CONFIG:
        if (prv_default_config() != true) {
            prv_send_negative_response(ID, ERROR_LOADING_DEFAULT_CONFIG); //Error loading default config
            return;
        }
        break;

    case ID_UPDATE_CONFIG: {
        config_t config;
        memcpy(&config, value, sizeof(config_t));
        prv_new_config(&config);
        SystemSoftwareReset();
        return;
        }

    case ID_SOC_LOOKUP:
        //TODO call soc_lookup();
        break;

    case ID_SET_RTC:
        if (rtc_set_date_time_from_epoch(*((uint32_t*)value)) != true) {
            prv_send_negative_response(ID, ERROR_SETTING_TIME); //Error setting time
            return;
        }
        break;

    case ID_CONTROL_CALIBRATION:
        {
            uint8_t channel = *((uint8_t*)value);
            if (channel == 0) {
                //Stop calibration
                acknowledge_calibration();
            } else {
                if (channel > 3) {
                    prv_send_negative_response(ID, ERROR_CALIBRATION_INVALID_CHANNEL); //Invalid channel
                    return;
                } else {
                    start_calibration(channel - 1);
                }
            }
        }
        break;

    case ID_CALIBRATION_VALUE:
        value_applied(*((float*)value));
        break;

    case ID_FORMAT_SD_CARD:
        if (prvConfig.loggerEnable) {
            uint8_t resp[3];
            resp[0] = ID;
            resp[1] = 0x01; //Number of following bytes
            resp[2] = ERROR_CARD_FORMATTING_FAILED;
            prv_send_response(resp, sizeof(resp));
            return;
        }

        sd_format();
        if (sd_format_status() == SD_FORMAT_BUSY) {
            //Busy right after requesting?
            //Another request is pending!
            uint8_t resp[3];
            resp[0] = ID;
            resp[1] = 0x01; //Number of following bytes
            resp[2] = CARD_FORMATTING_BUSY;
            prv_send_response(resp, sizeof(resp));
            return;
        }
        while (sd_format_status() == SD_FORMAT_DONE) {
            //Wait for the SD task to begin with the process
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (sd_format_status() == SD_FORMAT_BUSY) {
            //Busy after some time?
            //Request is now processed
            uint8_t resp[3];
            resp[0] = ID;
            resp[1] = 0x01; //Number of following bytes
            resp[2] = CARD_FORMATTING_BUSY;
            prv_send_response(resp, sizeof(resp));
            return;
        }
        if (sd_format_status() == SD_FORMAT_NO_CARD) {
            uint8_t resp[3];
            resp[0] = ID;
            resp[1] = 0x01; //Number of following bytes
            resp[2] = ERROR_NO_CARD;
            prv_send_response(resp, sizeof(resp));
            return;
        }
        break;

    case ID_RESTART_SYSTEM:
        PRINTF("Restarting...\n");
        prv_send_positive_response(ID);
        SystemSoftwareReset();
        return;

    case ID_TRANSFER_LOGFILE: {
        char name[32];
        uint8_t handle = *(uint8_t*)value;
        PRINTF("File handle: &u\n", handle);
        bool ret = prv_find_file_from_handle(handle, name);
        if (ret != true) {
            PRINTF("Requested logfile does not exist!\n");
            prv_send_negative_response(ID, ERROR_FILE_DOES_NOT_EXIST);
            return;
        }
        PRINTF("Requested file: %s\n", name);
        ret = sd_open_file_read(name);
        if (ret != true) {
            PRINTF("Cannot open logfile for read!\n");
            prv_send_negative_response(ID, ERROR_INTERNAL_ERROR);
            return;
        }

        uint8_t buffer[4095];
        size_t br;
        //do {
            sd_read_file(buffer + 1, 4094, &br);
            buffer[0] = ISOTP_LOGFILE;
            isotp_send_static(buffer, br);

        sd_close_file();

        break;
    }

    default:
        prv_send_negative_response(ID, ERROR_INTERNAL_ERROR); //Unknown error
        return;
    }

    prv_send_positive_response(ID);
}

static void prv_get_param(param_type_t *param) {
    //Only the RW and RO parameters are listed here
    uint8_t ID = param->ID;
    uint8_t DLC = 0; //DLC to send

    DLC = param->dataTypeLength + 2;
    uint8_t resp[DLC];
    resp[0] = ID;
    resp[1] = param->dataTypeLength;


    switch (ID) {

    case ID_QUERY_CONFIG:
        prv_send_config();
        break;
    case ID_BALANCING_FEEDBACK:
        //TODO: respond with active balancing gates
        break;

    case ID_SET_RTC:
        {
            uint32_t epoch = rtc_get_unix_time();
            memcpy(&resp[2], &epoch, param->dataTypeLength);
        }
        break;

    case ID_CALIBRATION_STATE:
        {
            adc_cal_state_t calState = get_cal_state();
            memcpy(&resp[2], &calState, param->dataTypeLength);
        }
        break;

    case ID_FORMAT_SD_CARD_STATUS:
        {
            uint8_t ret = sd_format_status();
            uint8_t resp[3];
            switch (ret) {
            case SD_FORMAT_DONE:
                resp[2] = CARD_FORMATTING_FINISHED;
                break;
            case SD_FORMAT_BUSY:
                resp[2] = CARD_FORMATTING_BUSY;
                break;
            case SD_FORMAT_ERROR:
                resp[2] = ERROR_CARD_FORMATTING_FAILED;
                break;
            case SD_FORMAT_NO_CARD:
                resp[2] = ERROR_NO_CARD;
                break;
            }
            resp[0] = ID;
            resp[1] = 0x01; //Number of following bytes
            prv_send_response(resp, sizeof(resp));
            return;
        }
        break;

    case ID_QUERY_LOGFILE_INFO: {
        FILINFO *entries = NULL;
        uint8_t numberOfEntries;
        sd_get_file_list(&entries, &numberOfEntries);

        uint8_t buffer[(sizeof(file_info_t) * MAX_NUMBER_OF_LOGFILES) + 2];
        bool ret = prv_format_file_list(entries, numberOfEntries, buffer + 2);
        if (ret != true) {
            prv_send_negative_response(ID, ERROR_INTERNAL_ERROR);
            return;
        }
        buffer[0] = ID;
        buffer[1] = 255; //Number of following bytes
        prv_send_response(buffer, (numberOfEntries * sizeof(file_info_t)) + 2);
        return;
    }
    break;

    default:
        prv_send_negative_response(ID, ERROR_INTERNAL_ERROR); //Unknown error
        return;
    }

    prv_send_response(resp, DLC);
}

static bool prv_find_param_type(uint8_t ID, param_type_t *found) {
    for (uint8_t i = 0; i < NUMBER_OF_CONFIG_PARAMS; i++) {
        if (prvParamTypes[i].ID == ID) {
            *found = prvParamTypes[i];
            return true;
        }
    }
    return false;
}

static void prv_send_negative_response(uint8_t ID, uint8_t reason) {
    uint8_t payload[4];
    payload[0] = ISOTP_CAL_RESPONSE;
    payload[1] = ID | 0x80;
    payload[2] = 0x01;
    payload[3] = reason;
    isotp_send_static(payload, sizeof(payload));
}

static void prv_send_positive_response(uint8_t ID) {
    uint8_t payload[3];
    payload[0] = ISOTP_CAL_RESPONSE;
    payload[1] = ID;
    payload[2] = 0x0;
    isotp_send_static(payload, sizeof(payload));
}

static void prv_send_response(uint8_t *data, size_t len) {
    uint8_t payload[len + 1];
    payload[0] = ISOTP_CAL_RESPONSE;
    memcpy(payload + 1, data, len);
    isotp_send_static(payload, sizeof(payload));
}

static void prv_new_config(config_t *config) {
    PRINTF("New config!\n");
    if (!eeprom_check_crc((uint8_t*)config, sizeof(config_t) - sizeof(uint16_t), config->crc16)) {
        uint16_t crc = eeprom_get_crc16((uint8_t*)config, sizeof(config_t) - sizeof(uint16_t));
        PRINTF("CRC should: %u\n", config->crc16);
        PRINTF("CRC is: %u\n", crc);
        PRINTF("New config: crc error!\n");
        prv_send_negative_response(ID_UPDATE_CONFIG, ERROR_CRC_ERROR);
        return;
    }

    memcpy(&prvConfig, config, sizeof(config_t));

    bool ret = prv_write_config();
    if (ret) {
        PRINTF("New config: success!\n");
        prv_send_positive_response(ID_UPDATE_CONFIG);
        return;
    } else {
        PRINTF("New config: failed!\n");
        prv_send_negative_response(ID_UPDATE_CONFIG, ERROR_INTERNAL_ERROR);
        return;
    }
}

static void prv_send_config(void) {
    uint8_t resp[sizeof(prvConfig) + 2];
    resp[0] = ID_QUERY_CONFIG;
    resp[1] = sizeof(prvConfig); //Number of following bytes
    memcpy(resp + 2, &prvConfig, sizeof(prvConfig));
    prv_send_response(resp, sizeof(resp));
}

static BaseType_t prv_config_mutex_take(TickType_t blocktime) {
    return xSemaphoreTake(prvConfigMutex, blocktime);
}

static void prv_load_config(void) {
    PRINTF("Loading config...\n");
    uint8_t pageBuffer[256];
    config_t config;


    bool ret;
    ret = eeprom_read(pageBuffer, CONFIG_EEPROM_PAGE, EEPROM_PAGESIZE, pdMS_TO_TICKS(500));
    if (ret != true) {
        PRINTF("Unable to read from EEPROM!\n");
        configASSERT(0);
    }
    memcpy(&config, pageBuffer, sizeof(config_t));

    uint16_t crcShould = config.crc16;

    if (!eeprom_check_crc((uint8_t*)&config, sizeof(config_t) - sizeof(uint16_t), crcShould)) {
        PRINTF("CRC error while loading config! Loading default!\n");
        prv_default_config();
        return;
    }

    memcpy(&prvConfig, &config, sizeof(config_t));
}

static bool prv_write_config(void) {
    PRINTF("Storing config...\n");
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    memset(pageBuffer, 0xFF, sizeof(pageBuffer));
    memcpy(&pageBuffer[0], &prvConfig, sizeof(config_t));

    bool ret;
    ret = eeprom_write(pageBuffer, CONFIG_EEPROM_PAGE, sizeof(pageBuffer), pdMS_TO_TICKS(500));

    if (ret != true) {
        PRINTF("Unable to write to EEPROM!\n");
        return false;
    }

    uint16_t timeout = 500; //5 seconds
    while (eeprom_busy(pdMS_TO_TICKS(500))) {
        if (--timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            PRINTF("Writing config to EEPROM timed out!\n");
            return false;
        }
    }
    prv_print_config();
    return true;
}

static bool prv_default_config(void) {
    PRINTF("Initializing with default config.\n");
    prvConfig = prvDefaultConfig;
    uint16_t crc16 = eeprom_get_crc16((uint8_t*)&prvConfig, sizeof(config_t) - sizeof(uint16_t));
    prvConfig.crc16 = crc16;
    return prv_write_config();
}

static void prv_print_config(void) {
    config_t *config = get_config(pdMS_TO_TICKS(500));

    if (config != NULL){
        PRINTF("Global balancing enabled: %s\n", config->globalBalancingEnable ? "true" : "false");
        PRINTF("Balancing threshold: %.4f\n", (float)config->balancingThreshold / 10000.0);
        PRINTF("Automatic SOC lookup enabled: %s\n", config->automaticSocLookupEnable ? "true" : "false");
        PRINTF("Number of stacks: %u\n", config->numberOfStacks);
        PRINTF("Logger enabled: %s\n", config->loggerEnable ? "true" : "false");
        PRINTF("Logger delete oldest file: %s\n", config->loggerDeleteOldestFile ? "true" : "false");
        PRINTF("Auto reset enabled: %s\n", config->autoResetOnPowerCycleEnable ? "true" : "false");
        release_config();
    }
}

static bool prv_format_file_list(FILINFO *entries, uint8_t numberOfEntries, uint8_t *buffer) {
    if (entries == NULL) {
        return false;
    }

    if (buffer == NULL) {
        return false;
    }

    for (size_t i = 0; i < numberOfEntries; i++) {
        file_info_t file;
        file.handle = i;
        strncpy(file.name, entries[i].fname, 19);
        file.name[19] = '\0';
        file.size = entries[i].fsize;
        PRINTF("Name: %s, Handle: %u, size: %lu\n", file.name, file.handle, file.size);
        memcpy(&buffer[i * sizeof(file_info_t)], &file, sizeof(file_info_t));
    }
    return true;
}

static bool prv_find_file_from_handle(uint8_t handle, char *name) {
    uint8_t numberOfEntries;
    FILINFO *entries = NULL;
    sd_get_file_list(&entries, &numberOfEntries);

    if (handle >= numberOfEntries) {
        return false;
    }

    if (entries != NULL) {
        strcpy(name, entries[handle].fname);
    } else {
        return false;
    }

    return true;
}
