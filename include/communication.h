/*
 * bmu.h
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#ifndef COMMUNICATION_H_
#define COMMUNICATION_H_


#include "S32K14x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "contactor.h"
#include "config.h"
#include "can.h"
#include "stacks.h"
#include "adc.h"
#include "cal.h"
#include "isotp_handler.h"



enum {
    CAN_ID_ISOTP_UP     = 0x012,
    CAN_ID_ISOTP_DOWN   = 0x013,
    CAN_ID_TEST         = 0x014
};

void init_comm(void);




#endif /* COMMUNICATION_H_ */
