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
#include "S32K146.h"
#include "interrupts.h"

typedef struct {
    uint16_t ID;
    uint8_t DLC;
    uint8_t payload[8];
} can_msg_t;

extern QueueHandle_t can0RxQueueHandle;
extern QueueHandle_t can1RxQueueHandle;

void can_init(CAN_Type *can);
void enqueue_message(can_msg_t *message);
bool can_send(CAN_Type *can, can_msg_t *msg);


#endif /* CAN_H_ */
