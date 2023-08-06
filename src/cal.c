/*
 * cal.c
 *
 *  Created on: May 20, 2023
 *      Author: luca
 */


#include "cal.h"

static config_t prvConfig;
static SemaphoreHandle_t prvConfigMutex = NULL;


enum {
    ID_LOAD_DEFAULT_CONFIG = 0x00,
    ID_QUERY_CONFIG        = 0x01,
    ID_UPDATE_CONFIG       = 0x02,
    ID_SOC_LOOKUP          = 0x10,
    ID_SET_RTC             = 0x20,
    ID_CONTROL_CALIBRATION = 0x30,
    ID_CALIBRATION_STATE   = 0x31,
    ID_CALIBRATION_VALUE   = 0x32
};

enum {
    RO, //Read only
    WO  //Write only
};

typedef struct {
    uint8_t ID;
    uint8_t modifier;
    uint8_t dataTypeLength;
} param_type_t;

static param_type_t prvParamTypes[] = {
        {ID_LOAD_DEFAULT_CONFIG,         WO, 0},
        {ID_QUERY_CONFIG,                RO, 0},
        {ID_UPDATE_CONFIG,               WO, 7},
        {ID_SOC_LOOKUP,                  WO, 0},
        {ID_SET_RTC,                     WO, sizeof(uint32_t)},
        {ID_CONTROL_CALIBRATION,         WO, 1},
        {ID_CALIBRATION_STATE,           RO, 1},
        {ID_CALIBRATION_VALUE,           WO, sizeof(float)}
};

static config_t prvDefaultConfig = {
        false,
        40000,
        false,
        MAX_NUM_OF_SLAVES,
        true,
        0
};

static bool prv_find_param_type(uint8_t ID, param_type_t *found);
static void prv_send_negative_response(uint8_t ID, uint8_t reason);
static void prv_send_positive_response(uint8_t ID);
static void prv_send_response(uint8_t ID, uint8_t *data, size_t len);
static BaseType_t prv_config_mutex_take(TickType_t blocktime);
static void prv_load_config(void);
static bool prv_write_config(void);
static bool prv_default_config(void);
static void prv_print_config(void);
static void prv_send_config(void);
static void prv_update_config(uint8_t *data);


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
    if (len < 1) {
        //No command sent
        return;
    }

    uint8_t ID = data[0];

    param_type_t param;
    bool ret;
    ret = prv_find_param_type(ID, &param);

    if (ret != true) {
        prv_send_negative_response(ID, CAL_ERROR_PARAM_DOES_NOT_EXIST);
        return;
    }

    if (param.modifier == WO && (len - 1) != param.dataTypeLength) {
        prv_send_negative_response(ID, CAL_ERROR_DLC_MISMATCH);
        return;
    }

    if (param.modifier == RO && (len - 1) > 0) {
        prv_send_negative_response(ID, CAL_ERROR_DLC_MISMATCH);
        return;
    }

    switch (ID) {
        case ID_QUERY_CONFIG:
            prv_send_config();
            return;

        case ID_CALIBRATION_STATE: {
            adc_cal_state_t calState = get_cal_state();
            prv_send_response(ID, &calState, param.dataTypeLength);
            return;
            }

        case ID_LOAD_DEFAULT_CONFIG:
            if (prv_default_config() != true) {
                prv_send_negative_response(ID, ERROR_LOADING_DEFAULT_CONFIG); //Error loading default config
                return;
            } else {
                prv_send_positive_response(ID);
                SystemSoftwareReset();
            }
            break;

        case ID_UPDATE_CONFIG:
            prv_update_config(&data[1]);
            SystemSoftwareReset();
            return;

        case ID_SOC_LOOKUP: {
                bool ret = true;
                PRINTF("SOC lookup\n");
                //TODO call ret = soc_lookup();
                if (ret != true) {
                    prv_send_negative_response(ID, CAL_ERROR_INTERNAL_ERROR);
                    return;
                }
            }
            break;

        case ID_SET_RTC: {
                uint32_t time;
                memcpy(&time, data + 1, sizeof(uint32_t));
                if (rtc_set_date_time_from_epoch(time) != true) {
                    prv_send_negative_response(ID, ERROR_SETTING_TIME); //Error setting time
                    return;
                }
            }
            break;

        case ID_CONTROL_CALIBRATION: {
                uint8_t channel = data[1];
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

        case ID_CALIBRATION_VALUE: {
                float value;
                memcpy(&value, data + 1, sizeof(float));
                value_applied(value);
            }
            break;

        default:
            prv_send_negative_response(ID, CAL_ERROR_PARAM_DOES_NOT_EXIST); //Requested parameter does not exist
            return;
    }

    prv_send_positive_response(ID);
}

static bool prv_find_param_type(uint8_t ID, param_type_t *found) {
    for (uint8_t i = 0; i < (sizeof(prvParamTypes) / sizeof(prvParamTypes[0])); i++) {
        if (prvParamTypes[i].ID == ID) {
            *found = prvParamTypes[i];
            return true;
        }
    }
    return false;
}

static void prv_send_negative_response(uint8_t ID, uint8_t reason) {
    can_msg_t msg;
    msg.ID = CAN_ID_DIAG_RESPONSE;
    msg.DLC = 2;
    msg.payload[0] = ID | 0x08;
    msg.payload[1] = reason;
    can_enqueue_message(CAN_CAL, &msg, pdMS_TO_TICKS(100));
}

static void prv_send_positive_response(uint8_t ID) {
    can_msg_t msg;
    msg.ID = CAN_ID_DIAG_RESPONSE;
    msg.DLC = 1;
    msg.payload[0] = ID;
    can_enqueue_message(CAN_CAL, &msg, pdMS_TO_TICKS(100));
}

static void prv_send_response(uint8_t ID, uint8_t *data, size_t len) {
    if (len > 7) {
        return;
    }

    can_msg_t msg;
    msg.ID = CAN_ID_DIAG_RESPONSE;
    msg.DLC = len + 1;
    msg.payload[0] = ID;
    memcpy(msg.payload + 1, data, len);
    can_enqueue_message(CAN_CAL, &msg, pdMS_TO_TICKS(100));
}

static void prv_send_config(void) {
    uint8_t resp[7];
    memset(resp, 0, sizeof(resp));
    memcpy(resp + 1, &prvConfig.balancingThreshold, sizeof(uint16_t));
    resp[3] = ((prvConfig.globalBalancingEnable & 0x01) << 6)
            | ((prvConfig.automaticSocLookupEnable & 0x01) << 5)
            | ((prvConfig.autoResetOnPowerCycleEnable & 0x01) << 4)
            | (prvConfig.numberOfStacks & 0x0F);

    prv_send_response(ID_QUERY_CONFIG, resp, sizeof(resp));
}

static void prv_update_config(uint8_t *data) {
    memcpy(&prvConfig.balancingThreshold, data + 1, sizeof(uint16_t));
    prvConfig.numberOfStacks = data[3] & 0x0F;
    prvConfig.autoResetOnPowerCycleEnable = (data[3] >> 4) & 0x01;
    prvConfig.automaticSocLookupEnable = (data[3] >> 5) & 0x01;
    prvConfig.globalBalancingEnable = (data[3] >> 6) & 0x01;

    uint16_t crc16 = eeprom_get_crc16((uint8_t*)&prvConfig, sizeof(config_t) - sizeof(uint16_t));
    prvConfig.crc16 = crc16;

    bool ret = prv_write_config();
    if (ret) {
        PRINTF("New config: success!\n");
        prv_print_config();
        prv_send_positive_response(ID_UPDATE_CONFIG);
        return;
    } else {
        PRINTF("New config: failed!\n");
        prv_send_negative_response(ID_UPDATE_CONFIG, CAL_ERROR_INTERNAL_ERROR);
        return;
    }
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
        PRINTF("Auto reset enabled: %s\n", config->autoResetOnPowerCycleEnable ? "true" : "false");
        release_config();
    }
}
