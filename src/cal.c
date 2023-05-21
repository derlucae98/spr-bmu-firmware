/*
 * cal.c
 *
 *  Created on: May 20, 2023
 *      Author: luca
 */


#include "cal.h"

#define NUMBER_OF_CONFIG_PARAMS 15

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
    ID_FORMAT_SD_CARD
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
        {ID_SOC_LOOKUP,                  false, RW, 0},
        {ID_AUTOMATIC_SOC_LOOKUP_ENABLE, true,  RW, sizeof(prvConfig.automaticSocLookupEnable)},
        {ID_NUMBER_OF_STACKS,            true,  RW, sizeof(prvConfig.numberOfStacks)},
        {ID_LOGGER_ENABLE,               true,  RW, sizeof(prvConfig.loggerEnable)},
        {ID_LOGGER_DELETE_OLDEST_FILE,   true,  RW, sizeof(prvConfig.loggerDeleteOldestFile)},
        {ID_AUTORESET_ENABLE,            true,  RW, sizeof(prvConfig.autoResetOnPowerCycleEnable)},
        {ID_SET_GET_RTC,                 false, RW, sizeof(uint32_t)},
        {ID_CONTROL_CALIBRATION,         false, WO, sizeof(uint8_t)},
        {ID_CALIBRATION_STATE,           false, RO, sizeof(uint8_t)},
        {ID_CALIBRATION_VALUE,           false, WO, sizeof(float)},
        {ID_FORMAT_SD_CARD,              false, RW, sizeof(bool)}
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
static void prv_update_param(uint8_t ID, void *value, size_t len, uint8_t DLC);
static void prv_send_negative_response(uint8_t ID);
static void prv_send_posititve_response(uint8_t ID);
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

    uint8_t paramID = msg->payload[0];

    if (paramID & 0x80) {
        //Read value
        PRINTF("Read param requested\n");
    } else {
        //Set value
        PRINTF("Set param requested\n");
        PRINTF("Param ID: %u\n", paramID);
        PRINTF("len: %u\n", msg->payload[1]);
        PRINTF("DLC: %u\n", msg->DLC);
        prv_update_param(paramID, (void*)&msg->payload[2], msg->payload[1], msg->DLC);
    }
}

static void prv_update_param(uint8_t ID, void *value, size_t len, uint8_t DLC) {
    param_type_t param;
    if (!prv_find_param_type(ID, &param)) {
        PRINTF("Error: Requested parameter does not exist!\n");
        prv_send_negative_response(ID); //Requested parameter does not exist
        return;
    } else {
        if (param.modifier == RO) {
            PRINTF("Error: Requested the modification of a read only value!\n");
            prv_send_negative_response(ID); //Requested the modification of a read only value
            return;
        }
        if ((len + 2) != DLC) {
            PRINTF("Error: DLC does not match the expected number of bytes!\n");
            prv_send_negative_response(ID); //DLC does not match the expected number of bytes
            return;
        }
        if (param.dataTypeLength != len) {
            PRINTF("Error: Number of transmitted bytes does not match the length of the datatype!\n");
            prv_send_negative_response(ID); //Number of transmitted bytes does not match the length of the datatype
            return;
        }
    }

    if (param.NV) {
        if (get_config(pdMS_TO_TICKS(1000)) == NULL) {
            PRINTF("Error: Could not lock mutex!\n");
            prv_send_negative_response(ID); //Could not lock mutex
            return;
        }
    }

    //Only the RW and WO parameters are listed here
    switch (ID) {
    case ID_LOAD_DEFAULT_CONFIG:
        PRINTF("Loading default config.\n");
        if (prv_default_config() != true) {
            prv_send_negative_response(ID); //Error loading default config
            return;
        }
        break;
    case ID_GLOBAL_BALANCING_ENABLE:
        PRINTF("Setting balancing enable to %u\n", *((uint8_t*)value));
        prvConfig.globalBalancingEnable = *((bool*)value);
        break;

    case ID_BALANCING_THRESHOLD:
        PRINTF("Setting balancing threshold to %u\n", *((uint16_t*)value));
        prvConfig.balancingThreshold = *((uint16_t*)value);
        break;

    case ID_SOC_LOOKUP:
        //TODO call soc_lookup();
        break;

    case ID_AUTOMATIC_SOC_LOOKUP_ENABLE:
        prvConfig.automaticSocLookupEnable = *((bool*)value);
        break;

    case ID_NUMBER_OF_STACKS:
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
        //TODO call set_rtc();
        break;

    case ID_CONTROL_CALIBRATION:
        //TODO call function to start calibration
        break;

    case ID_CALIBRATION_VALUE:
        //TODO call function to set calibration value
        break;

    case ID_FORMAT_SD_CARD:
        sd_format();
        break;

    default:
        prv_send_negative_response(ID); //Unknown error
        return;
    }


    if (param.NV) {
        release_config();
        if (prv_write_config() != true) { //Update non-volatile memory if the requested parameter has the non-voltatile flag
            prv_send_negative_response(ID); //Error while updating NV data
            return;
        }
    }

    prv_send_posititve_response(ID);
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

static void prv_send_negative_response(uint8_t ID) {
    can_msg_t msg;
    msg.ID = CAN_ID_CAL_RESPONSE;
    msg.DLC = 1;
    msg.payload[0] = ID | 0x80;
    can_send(CAN_CAL, &msg);
}

static void prv_send_posititve_response(uint8_t ID) {
    can_msg_t msg;
    msg.ID = CAN_ID_CAL_RESPONSE;
    msg.DLC = 1;
    msg.payload[0] = ID;
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
    prv_print_config();
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
