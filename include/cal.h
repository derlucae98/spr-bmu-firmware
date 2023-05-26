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

#define CAN_CAL CAN_VEHIC


typedef struct {
    bool globalBalancingEnable;
    uint16_t balancingThreshold;
    bool automaticSocLookupEnable;
    uint8_t numberOfStacks;
    bool loggerEnable;
    bool loggerDeleteOldestFile;
    bool autoResetOnPowerCycleEnable;
} config_t;

enum error_codes {
    ERROR_PARAM_DOES_NOT_EXIST = 0x00,
    ERROR_CANNOT_MODIFY_RO_PARAMETER,
    ERROR_DLC_DOES_NOT_MATCH_NUMBER_OF_BYTES,
    ERROR_NUMBER_OF_BYTES_DOES_NOT_MATCH_DATATYPE,
    ERROR_INTERNAL_ERROR,
};

#define ERROR_LOADING_DEFAULT_CONFIG 0x10
#define ERROR_NUMBER_OF_STACKS_OUT_OF_RANGE 0x10
#define ERROR_SETTING_TIME 0x10
#define ERROR_CALIBRATION_INVALID_CHANNEL 0x10
#define ERROR_UPDATING_NV_DATA 0x10

void init_cal(void);

config_t* get_config(TickType_t blocktime);


bool copy_config(config_t *dest, TickType_t blocktime);

void release_config(void);
void handle_cal_request(can_msg_t *msg);


#endif /* CAL_H_ */
