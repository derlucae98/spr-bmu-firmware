/*
 * bmu.h
 *
 *  Created on: Feb 10, 2022
 *      Author: Luca Engelmann
 */

#ifndef COMMUNICATION_H_
#define COMMUNICATION_H_

#include "adc.h"
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


void init_bmu(void);




#endif /* COMMUNICATION_H_ */
