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
#include "system_S32K148.h"

#define CAN_CAL CAN_DIAG


typedef struct{
    bool globalBalancingEnable;
    uint16_t balancingThreshold;
    bool automaticSocLookupEnable;
    uint8_t numberOfStacks;
    bool autoResetOnPowerCycleEnable;
    uint16_t crc16;
} config_t;

enum cal_error_codes {
    CAL_ERROR_NO_ERROR,
    CAL_ERROR_PARAM_DOES_NOT_EXIST,
    CAL_ERROR_DLC_MISMATCH, //Expected more or less bytes of payload
    CAL_ERROR_INTERNAL_ERROR
};

#define ERROR_LOADING_DEFAULT_CONFIG 0x10
#define ERROR_SETTING_TIME 0x10
#define ERROR_CALIBRATION_INVALID_CHANNEL 0x10

void init_cal(void);
config_t* get_config(TickType_t blocktime);
bool copy_config(config_t *dest, TickType_t blocktime);
void release_config(void);
void handle_cal_request(uint8_t *data, size_t len);


#endif /* CAL_H_ */
