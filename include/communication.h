/*
 * bmu.h
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#ifndef COMMUNICATION_H_
#define COMMUNICATION_H_

#include <stdlib.h>
#include "S32K14x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "timers.h"
#include "contactor.h"
#include "config.h"
#include "can.h"
#include "stacks.h"
#include "adc.h"
#include "cal.h"


void init_comm(void);
uint32_t get_reset_reason(void);
void print_reset_reason(uint32_t resetReason);




#endif /* COMMUNICATION_H_ */
