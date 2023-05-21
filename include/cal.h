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

void init_cal(void);

config_t* get_config(TickType_t blocktime);


bool copy_config(config_t *dest, TickType_t blocktime);

void release_config(void);
void handle_cal_request(can_msg_t *msg);


#endif /* CAL_H_ */
