/*
 * cal.h
 *
 *  Created on: May 20, 2023
 *      Author: luca
 */

#ifndef CAL_H_
#define CAL_H_

#include "FreeRTOS.h"
#include "semphr.h"
#include "communication.h"
#include "sd.h"
#include "config.h"
#include "can.h"
#include "rtc.h"
#include "adc_cal.h"
#include "isotp.h"
#include "system_S32K148.h"

#define CAN_CAL CAN_VEHIC


typedef struct __attribute__((packed)){
    bool globalBalancingEnable;
    uint16_t balancingThreshold;
    bool automaticSocLookupEnable;
    uint8_t numberOfStacks;
    bool loggerEnable;
    bool loggerDeleteOldestFile;
    bool autoResetOnPowerCycleEnable;
    uint16_t crc16;
} config_t;

typedef struct __attribute__((packed)){
    char name[20];
    uint8_t handle;
    size_t size;
} file_info_t;

enum error_codes {
    ERROR_PARAM_DOES_NOT_EXIST = 0x00,
    ERROR_CANNOT_MODIFY_RO_PARAMETER,
    ERROR_DLC_DOES_NOT_MATCH_NUMBER_OF_BYTES,
    ERROR_NUMBER_OF_BYTES_DOES_NOT_MATCH_DATATYPE,
    ERROR_INTERNAL_ERROR,
    ERROR_CANNOT_READ_WO_PARAMETER,
    ERROR_ISOTP_ERROR,
    ERROR_CRC_ERROR,
    ERROR_FILE_DOES_NOT_EXIST
};

#define ERROR_LOADING_DEFAULT_CONFIG 0x10
#define ERROR_SETTING_TIME 0x10
#define ERROR_CALIBRATION_INVALID_CHANNEL 0x10

#define CARD_FORMATTING_FINISHED 0x10
#define CARD_FORMATTING_BUSY 0x11
#define ERROR_CARD_FORMATTING_FAILED 0x12
#define ERROR_NO_CARD 0x13



void init_cal(void);

config_t* get_config(TickType_t blocktime);



bool copy_config(config_t *dest, TickType_t blocktime);

void release_config(void);
void handle_cal_request(uint8_t *data, size_t len);


#endif /* CAL_H_ */
