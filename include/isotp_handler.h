/*
 * isotp_handler.h
 *
 *  Created on: 02.06.2023
 *      Author: luca
 */

#ifndef ISOTP_H_
#define ISOTP_H_

#define CAN_ISOTP CAN_VEHIC


#define ISOTP_BUFFFERSIZE 4095

#include "FreeRTOS.h"
#include "task.h"
#include "config.h"
#include "can.h"
#include "uart.h"
#include "isotp_defines.h"
#include "isotp.h"
#include "communication.h"
#include "cal.h"

typedef enum {
    ISOTP_CAL_REQUEST,
    ISOTP_CAL_RESPONSE,
    ISOTP_LOGFILE, //Transmitted data is a logfile
} isotp_transmission_type_t;

void init_isotp(void);
void isotp_on_recv(can_msg_t *msg);
void isotp_send_static(void *data, size_t len); //Use the default IsoTP link
void isotp_send_test(void);


#endif /* ISOTP_H_ */
