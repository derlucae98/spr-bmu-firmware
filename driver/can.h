#ifndef CAN_H_
#define CAN_H_

#include <gpio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "S32K14x.h"
#include "interrupts.h"

#define CAN_TX_Q_ITEMS 32
#define CAN_RX_Q_ITEMS 16

typedef struct {
    uint16_t ID;
    uint8_t DLC;
    uint8_t payload[8];
} can_msg_t;

extern QueueHandle_t can0RxQueueHandle;
extern QueueHandle_t can1RxQueueHandle;

void can_init(CAN_Type *can);
bool can_enqueue_message(CAN_Type *can, can_msg_t *message, TickType_t blocktime);



#endif /* CAN_H_ */
