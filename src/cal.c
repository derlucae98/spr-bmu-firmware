/*
 * cal.c
 *
 *  Created on: May 20, 2023
 *      Author: luca
 */


#include "cal.h"

#define NUMBER_OF_CONFIG_PARAMS 17

static config_t prvConfig;
static SemaphoreHandle_t prvConfigMutex = NULL;


enum {
    ID_LOAD_DEFAULT_CONFIG = 0,
    ID_GLOBAL_BALANCING_ENABLE,
    ID_BALANCING_THRESHOLD,
    ID_BALANCING_FEEDBACK,
    ID_SOC_LOOKUP,
    ID_AUTOMATIC_SOC_LOOKUP_ENABLE,
    ID_NUMBER_OF_STACKS,
    ID_LOGGER_ENABLE,
    ID_LOGGER_DELETE_OLDEST_FILE,
    ID_AUTORESET_ENABLE,
    ID_SET_GET_RTC,
    ID_CONTROL_CALIBRATION,
    ID_CALIBRATION_STATE,
    ID_CALIBRATION_VALUE,
    ID_FORMAT_SD_CARD,
    ID_SAVE_NV,
    ID_FORMAT_SD_CARD_STATUS
};

enum {
    RW, //Read-Write
    RO, //Read only
    WO  //Write only
};

typedef struct {
    uint8_t ID;
    bool NV;
    uint8_t modifier;
    uint8_t dataTypeLength;
} param_type_t;

static param_type_t prvParamTypes[NUMBER_OF_CONFIG_PARAMS] = {
        {ID_LOAD_DEFAULT_CONFIG,         false, WO, 0},
        {ID_GLOBAL_BALANCING_ENABLE,     true,  RW, sizeof(prvConfig.globalBalancingEnable)},
        {ID_BALANCING_THRESHOLD,         true,  RW, sizeof(prvConfig.balancingThreshold)},
        {ID_BALANCING_FEEDBACK,          false, RO, sizeof(uint16_t)},
        {ID_SOC_LOOKUP,                  false, WO, 0},
        {ID_AUTOMATIC_SOC_LOOKUP_ENABLE, true,  RW, sizeof(prvConfig.automaticSocLookupEnable)},
        {ID_NUMBER_OF_STACKS,            true,  RW, sizeof(prvConfig.numberOfStacks)},
        {ID_LOGGER_ENABLE,               true,  RW, sizeof(prvConfig.loggerEnable)},
        {ID_LOGGER_DELETE_OLDEST_FILE,   true,  RW, sizeof(prvConfig.loggerDeleteOldestFile)},
        {ID_AUTORESET_ENABLE,            true,  RW, sizeof(prvConfig.autoResetOnPowerCycleEnable)},
        {ID_SET_GET_RTC,                 false, RW, sizeof(uint32_t)},
        {ID_CONTROL_CALIBRATION,         false, WO, sizeof(uint8_t)},
        {ID_CALIBRATION_STATE,           false, RO, sizeof(uint8_t)},
        {ID_CALIBRATION_VALUE,           false, WO, sizeof(float)},
        {ID_FORMAT_SD_CARD,              false, WO, 0},
        {ID_SAVE_NV,                     false, WO, 0},
        {ID_FORMAT_SD_CARD_STATUS,       false, RO, 1},
};

static config_t prvDefaultConfig = {
        false,
        40000,
        false,
        MAX_NUM_OF_SLAVES,
        false,
        false,
        true
};

static bool prv_find_param_type(uint8_t ID, param_type_t *found);
static void prv_update_param(param_type_t *param, void *value);
static void prv_get_param(param_type_t *param);
static void prv_send_negative_response(uint8_t ID, uint8_t reason);
static void prv_send_positive_response(uint8_t ID);
static void prv_send_response(uint8_t *data, uint8_t len);
static BaseType_t prv_config_mutex_take(TickType_t blocktime);
static void prv_load_config(void);
static bool prv_write_config(void);
static bool prv_default_config(void);
static void prv_print_config(void);


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

void handle_cal_request(can_msg_t *msg) {

    uint8_t ID = msg->payload[0];
    uint8_t len = msg->payload[1];
    uint8_t modify = ID & 0x80; //Read or modify value requested?
    param_type_t param;

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
        if (modify && ((len + 2) != msg->DLC)) {
            PRINTF("Error: DLC does not match the expected number of bytes!\n");
            prv_send_negative_response(ID, ERROR_DLC_DOES_NOT_MATCH_NUMBER_OF_BYTES); //DLC does not match the expected number of bytes
            return;
        }
        if (modify && param.dataTypeLength != len) {
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
        prv_update_param(&param, &msg->payload[2]);
    } else {
        //Read value
        prv_get_param(&param);
    }
}

static void prv_update_param(param_type_t *param, void *value) {
    uint8_t ID = param->ID;

    if (param->NV) {
        if (get_config(pdMS_TO_TICKS(1000)) == NULL) {
            PRINTF("Error: Could not lock mutex!\n");
            prv_send_negative_response(ID, ERROR_INTERNAL_ERROR); //Could not lock mutex
            return;
        }
    }

    //Only the RW and WO parameters are listed here
    switch (ID) {
    case ID_LOAD_DEFAULT_CONFIG:
        if (prv_default_config() != true) {
            prv_send_negative_response(ID, ERROR_LOADING_DEFAULT_CONFIG); //Error loading default config
            return;
        }
        break;
    case ID_GLOBAL_BALANCING_ENABLE:
        prvConfig.globalBalancingEnable = *((bool*)value);
        break;

    case ID_BALANCING_THRESHOLD:
        prvConfig.balancingThreshold = *((uint16_t*)value);
        break;

    case ID_SOC_LOOKUP:
        //TODO call soc_lookup();
        break;

    case ID_AUTOMATIC_SOC_LOOKUP_ENABLE:
        prvConfig.automaticSocLookupEnable = *((bool*)value);
        break;

    case ID_NUMBER_OF_STACKS:
        PRINTF("Number of slaves: %u\n", *((uint8_t*)value));
        if (*((uint8_t*)value) > MAX_NUM_OF_SLAVES || *((uint8_t*)value) == 0) {
            release_config();
            prv_send_negative_response(ID, ERROR_NUMBER_OF_STACKS_OUT_OF_RANGE); //More stacks requested as possible
            return;
        }
        prvConfig.numberOfStacks = *((uint8_t*)value);
        break;

    case ID_LOGGER_ENABLE:
        prvConfig.loggerEnable = *((bool*)value);
        break;

    case ID_LOGGER_DELETE_OLDEST_FILE:
        prvConfig.loggerDeleteOldestFile = *((bool*)value);
        break;

    case ID_AUTORESET_ENABLE:
        prvConfig.autoResetOnPowerCycleEnable = *((bool*)value);
        break;

    case ID_SET_GET_RTC:
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

    case ID_SAVE_NV:
        if (prv_write_config() != true) { //Update non-volatile memory if the requested parameter has the non-voltatile flag
            prv_send_negative_response(ID, ERROR_UPDATING_NV_DATA); //Error while updating NV data
            return;
        }
        break;

    default:
        prv_send_negative_response(ID, ERROR_INTERNAL_ERROR); //Unknown error
        return;
    }


    if (param->NV) {
        release_config();
    }

    prv_send_positive_response(ID);
}

static void prv_get_param(param_type_t *param) {
    //Only the RW and RO parameters are listed here
    uint8_t ID = param->ID;
    uint8_t DLC = 0; //DLC to send

    if (param->NV) {
        if (get_config(pdMS_TO_TICKS(1000)) == NULL) {
            PRINTF("Error: Could not lock mutex!\n");
            prv_send_negative_response(ID, ERROR_INTERNAL_ERROR); //Could not lock mutex
            return;
        }
    }

    DLC = param->dataTypeLength + 2;
    uint8_t resp[DLC];
    resp[0] = ID;
    resp[1] = param->dataTypeLength;


    switch (ID) {
    case ID_GLOBAL_BALANCING_ENABLE:
        memcpy(&resp[2], &prvConfig.globalBalancingEnable, param->dataTypeLength);
        break;

    case ID_BALANCING_THRESHOLD:
        memcpy(&resp[2], &prvConfig.balancingThreshold, param->dataTypeLength);
        break;

    case ID_BALANCING_FEEDBACK:
        //TODO: respond with active balancing gates
        break;

    case ID_AUTOMATIC_SOC_LOOKUP_ENABLE:
        memcpy(&resp[2], &prvConfig.automaticSocLookupEnable, param->dataTypeLength);
        break;

    case ID_NUMBER_OF_STACKS:
        memcpy(&resp[2], &prvConfig.numberOfStacks, param->dataTypeLength);
        break;

    case ID_LOGGER_ENABLE:
        memcpy(&resp[2], &prvConfig.loggerEnable, param->dataTypeLength);
        break;

    case ID_LOGGER_DELETE_OLDEST_FILE:
        memcpy(&resp[2], &prvConfig.loggerDeleteOldestFile, param->dataTypeLength);
        break;

    case ID_AUTORESET_ENABLE:
        memcpy(&resp[2], &prvConfig.autoResetOnPowerCycleEnable, param->dataTypeLength);
        break;

    case ID_SET_GET_RTC:
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

    default:
        prv_send_negative_response(ID, ERROR_INTERNAL_ERROR); //Unknown error
        return;
    }

    if (param->NV) {
        release_config();
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
    can_msg_t msg;
    msg.ID = CAN_ID_CAL_RESPONSE;
    msg.DLC = 3;
    msg.payload[0] = ID | 0x80;
    msg.payload[1] = 0x01;
    msg.payload[2] = reason;
    can_send(CAN_CAL, &msg);
}

static void prv_send_positive_response(uint8_t ID) {
    can_msg_t msg;
    msg.ID = CAN_ID_CAL_RESPONSE;
    msg.DLC = 2;
    msg.payload[0] = ID;
    msg.payload[1] = 0;
    can_send(CAN_CAL, &msg);
}

static void prv_send_response(uint8_t *data, uint8_t len) {
    can_msg_t msg;
    msg.ID = CAN_ID_CAL_RESPONSE;
    msg.DLC = len;
    memcpy(&msg.payload, data, len);
    can_send(CAN_CAL, &msg);
}

static BaseType_t prv_config_mutex_take(TickType_t blocktime) {
    return xSemaphoreTake(prvConfigMutex, blocktime);
}

static void prv_load_config(void) {
    PRINTF("Loading config...\n");
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    memset(pageBuffer, 0, EEPROM_PAGESIZE);

    bool ret;
    ret = eeprom_read(pageBuffer, CONFIG_EEPROM_PAGE, EEPROM_PAGESIZE, pdMS_TO_TICKS(500));
    if (ret == false) {
        PRINTF("Unable to read from EEPROM!\n");
        configASSERT(0);
    }
    uint16_t crcShould = (pageBuffer[254] << 8) | pageBuffer[255];

    if (!eeprom_check_crc(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t), crcShould)) {
        PRINTF("CRC error while loading config! Loading default!\n");
        prv_default_config();
    }

    memcpy(&prvConfig, &pageBuffer[0], sizeof(config_t));
}

static bool prv_write_config(void) {
    PRINTF("Storing config...\n");
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    memset(pageBuffer, 0xFF, sizeof(pageBuffer));
    memcpy(&pageBuffer[0], &prvConfig, sizeof(config_t));
    uint16_t crc = eeprom_get_crc16(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t));
    pageBuffer[254] = crc >> 8;
    pageBuffer[255] = crc & 0xFF;

    bool ret;
    ret = eeprom_write(pageBuffer, CONFIG_EEPROM_PAGE, sizeof(pageBuffer), pdMS_TO_TICKS(500));

    if (ret == false) {
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
    return true;
}

static bool prv_default_config(void) {
    PRINTF("Initializing with default config.\n");
    prvConfig = prvDefaultConfig;
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
