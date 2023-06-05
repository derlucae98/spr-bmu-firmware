/*
 * isotp_handler.c
 *
 *  Created on: 02.06.2023
 *      Author: luca
 */

#include "isotp_handler.h"


static IsoTpLink prvIsotpLink;

static uint8_t prvIsotpRecvBuffer[ISOTP_BUFFFERSIZE];
static uint8_t prvIsotpSendBuffer[ISOTP_BUFFFERSIZE];
static uint8_t prvPayload[ISOTP_BUFFFERSIZE];

static void isotp_task(void* p);

void init_isotp(void) {
    isotp_init_link(&prvIsotpLink, CAN_ID_ISOTP_UP,
            prvIsotpSendBuffer, sizeof(prvIsotpSendBuffer),
            prvIsotpRecvBuffer, sizeof(prvIsotpRecvBuffer));
    xTaskCreate(isotp_task, "isotp", ISOTP_TASK_STACK, NULL, ISOTP_TASK_PRIO, NULL);
}

void isotp_on_recv(can_msg_t *msg) {
    isotp_on_can_message(&prvIsotpLink, msg->payload, msg->DLC);
}

void isotp_send_test(void) {
    memset(&prvPayload, 0x5A, sizeof(prvPayload));
    int ret = isotp_send(&prvIsotpLink, prvPayload, ISOTP_BUFFFERSIZE);
    if (ISOTP_RET_OK == ret) {
        PRINTF("ISOTP: Send ok.\n");
    } else {
        PRINTF("ISOTP: Send failed!\n");
    }
}

static void isotp_task(void* p) {
    (void) p;
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(5);

    while (1) {

        isotp_poll(&prvIsotpLink);
        uint16_t recSize;
        int ret = isotp_receive(&prvIsotpLink, prvPayload, ISOTP_BUFFFERSIZE, &recSize);
        if (ISOTP_RET_OK == ret) {
            PRINTF("ISOTP: New data available!\n");
            for (size_t i = 0; i < recSize; i++) {
                PRINTF("[%02X]", prvPayload[i]);
            }
            PRINTF("\n");
        }

        vTaskDelayUntil(&lastWakeTime, period);
    }
}

int  isotp_user_send_can(const uint32_t arbitration_id,
                         const uint8_t* data, const uint8_t size) {
    can_msg_t msg;
    msg.ID = arbitration_id;
    memcpy(&msg.payload, data, size);
    msg.DLC = size;
    can_send(CAN_ISOTP, &msg);
    return ISOTP_RET_OK;
}

uint32_t isotp_user_get_ms(void) {
    return xTaskGetTickCountFromISR();
}

void isotp_user_debug(const char* message, ...) {
    PRINTF(message);
}
