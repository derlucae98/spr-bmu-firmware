#include "can.h"

QueueHandle_t can0RxQueueHandle = NULL;
QueueHandle_t can1RxQueueHandle = NULL;
static QueueHandle_t prvCan0TxQueueHandle = NULL;
static QueueHandle_t prvCan1TxQueueHandle = NULL;

static SemaphoreHandle_t prvCan0Mutex = NULL;
static SemaphoreHandle_t prvCan1Mutex = NULL;

static void prv_can0_send_task(void *p);
static void prv_can1_send_task(void *p);
static bool prv_can_send(CAN_Type *can, can_msg_t *msg);

#define CAN_MB_REC_START               0
#define CAN_MB_REC                     8
#define CAN_MB_SEND_START              CAN_MB_REC
#define CAN_MB_SEND                    24
#define CAN_MB_SIZE                    4

#define CAN_MB_CODE_SHIFT              24
#define CAN_MB_CODE_MASK               (0xF << CAN_MB_CODE_SHIFT)
#define CAN_MB_CODE_REC_INACTIVE_MASK  (0  << CAN_MB_CODE_SHIFT)
#define CAN_MB_CODE_REC_EMPTY_MASK     (4  << CAN_MB_CODE_SHIFT)
#define CAN_MB_CODE_REC_FULL_MASK      (2  << CAN_MB_CODE_SHIFT)
#define CAN_MB_CODE_REC_OVERRUN_MASK   (6  << CAN_MB_CODE_SHIFT)
#define CAN_MB_CODE_REC_RANSWER_MASK   (10 << CAN_MB_CODE_SHIFT)
#define CAN_MB_CODE_REC_BUSY_MASK      (1  << CAN_MB_CODE_SHIFT)
#define CAN_MB_CODE_SEND_INACTIVE_MASK (8  << CAN_MB_CODE_SHIFT)
#define CAN_MB_CODE_SEND_ABORT_MASK    (9  << CAN_MB_CODE_SHIFT)
#define CAN_MB_CODE_SEND_DATA_MASK     (12 << CAN_MB_CODE_SHIFT)
#define CAN_MB_CODE_SEND_REMOTE_MASK   (12 << CAN_MB_CODE_SHIFT)
#define CAN_MB_CODE_SEND_TANSWER_MASK  (14 << CAN_MB_CODE_SHIFT)


static void handle_irq(CAN_Type *can, BaseType_t *higherPrioTaskWoken);

void can_init(CAN_Type *can, uint32_t filterMask) {
    configASSERT(can);

    uintptr_t canModule = (uintptr_t)can;

    switch (canModule) {
    case CAN0_BASE:
        PCC->PCCn[PCC_FlexCAN0_INDEX] |= PCC_PCCn_CGC_MASK;
        break;
    case CAN1_BASE:
        PCC->PCCn[PCC_FlexCAN1_INDEX] |= PCC_PCCn_CGC_MASK;
        break;
    }

    can->MCR |= CAN_MCR_MDIS_MASK;
    can->CTRL1 &= ~CAN_CTRL1_CLKSRC_MASK;
    can->MCR &= ~CAN_MCR_MDIS_MASK;
    while (!(can->MCR & CAN_MCR_FRZACK_MASK));

    can->CTRL1 = 0x10003; //1 MBit/s
    //Initialize message buffer
    for (size_t i = 0; i < ((CAN_MB_REC + CAN_MB_SEND) * CAN_MB_SIZE); i++) {
        can->RAMn[i] = 0; //Clear buffer
    }

    for (size_t i = 0; i < CAN_MB_REC; i++) {
        can->RXIMR[i] = filterMask;
    }
    //Mask for message buffer 14 and 15 is handled separately
    can->RX14MASK = filterMask;
    can->RX15MASK = filterMask;

    can->RXMGMASK = filterMask;

    //Prepare message buffer for reception
    for (size_t i = 0; i < CAN_MB_REC; i++) {
        can->RAMn[CAN_MB_SIZE * i] = CAN_MB_CODE_REC_EMPTY_MASK;
    }
    //Prepare message buffer for sending
    for (size_t i = 0; i < CAN_MB_SEND; i++) {
        can->RAMn[(i + CAN_MB_SEND_START) * CAN_MB_SIZE] = CAN_MB_CODE_SEND_INACTIVE_MASK;
    }

    can->IMASK1 = 0xFFFF; //Enable interrupt for all rec buffers

    can->MCR = 0x4002001F;

    //Wait for module readiness
    while (can->MCR & CAN_MCR_FRZACK_MASK);
    while (can->MCR & CAN_MCR_NOTRDY_MASK);

    switch (canModule) {
    case CAN0_BASE:
        nvic_enable_irq(CAN0_ORed_0_15_MB_IRQn);
        nvic_set_priority(CAN0_ORed_0_15_MB_IRQn, 0xFF);
        prvCan0Mutex = xSemaphoreCreateMutex();
        configASSERT(prvCan0Mutex);

        can0RxQueueHandle = xQueueCreate(CAN_RX_Q_ITEMS, sizeof(can_msg_t));
        configASSERT(can0RxQueueHandle);
        prvCan0TxQueueHandle = xQueueCreate(CAN_TX_Q_ITEMS, sizeof(can_msg_t));
        configASSERT(prvCan0TxQueueHandle);

        xTaskCreate(prv_can0_send_task, "can0send", 512, NULL, 3, NULL);
        break;
    case CAN1_BASE:
        nvic_enable_irq(CAN1_ORed_0_15_MB_IRQn);
        nvic_set_priority(CAN1_ORed_0_15_MB_IRQn, 0xFF);

        prvCan1Mutex = xSemaphoreCreateMutex();
        configASSERT(prvCan1Mutex);

        can1RxQueueHandle = xQueueCreate(CAN_RX_Q_ITEMS, sizeof(can_msg_t));
        configASSERT(can1RxQueueHandle);
        prvCan1TxQueueHandle = xQueueCreate(CAN_TX_Q_ITEMS, sizeof(can_msg_t));
        configASSERT(prvCan1TxQueueHandle);

        xTaskCreate(prv_can1_send_task, "can1send", 512, NULL, 3, NULL);
        break;
    }
}

bool can_enqueue_message(CAN_Type *can, can_msg_t *message, TickType_t blocktime) {
    configASSERT(can);

    uintptr_t canModule = (uintptr_t)can; //Transform the address stored in the pointer into a number

    switch (canModule) {
    case CAN0_BASE:
        return xQueueSendToBack(prvCan0TxQueueHandle, message, blocktime);
    case CAN1_BASE:
        return xQueueSendToBack(prvCan1TxQueueHandle, message, blocktime);
    default:
        configASSERT(0);
    }
}

void CAN0_ORed_0_15_MB_IRQHandler(void) {
    BaseType_t higherPrioTaskWoken = pdFALSE;
    handle_irq(CAN0, &higherPrioTaskWoken);
    portYIELD_FROM_ISR(higherPrioTaskWoken);
}

void CAN1_ORed_0_15_MB_IRQHandler(void) {
    BaseType_t higherPrioTaskWoken = pdFALSE;
    handle_irq(CAN1, &higherPrioTaskWoken);
    portYIELD_FROM_ISR(higherPrioTaskWoken);
}

static void handle_irq(CAN_Type *can, BaseType_t *higherPrioTaskWoken) {
    configASSERT(can);

    uint32_t iflags = can->IFLAG1;
    can->IFLAG1 = can->IFLAG1; //Clear all flags
    uint8_t currPos = 0;
    uintptr_t canModule = (uintptr_t)can;
    iflags &= 0xFFFF; //iterate only over the first 16 message buffers
    while (iflags > 0) {
        if (iflags & 0x1) {
            can_msg_t msg;
            msg.ID = (can->RAMn[(currPos * CAN_MB_SIZE) + 1] >> 18);
            msg.DLC = (can->RAMn[(currPos * CAN_MB_SIZE)] >> 16) & 0xF;
            for (size_t byte = 0; byte < msg.DLC; byte++) {
                if (byte < 4) {
                    msg.payload[byte] = (can->RAMn[(currPos * CAN_MB_SIZE) + 2] >> (8 * (3 - byte)));
                } else {
                    msg.payload[byte] = (can->RAMn[(currPos * CAN_MB_SIZE) + 3] >> (8 * (7 - byte)));
                }
            }
            //Needed to unlock the message buffer
            volatile uint32_t code = (can->RAMn[(currPos * CAN_MB_SIZE)] & 0x07000000) >> 24;
            volatile uint32_t dummy = can->TIMER;
            (void)code;
            (void)dummy;

            switch (canModule) {
            case CAN0_BASE:
                xQueueSendToBackFromISR(can0RxQueueHandle, &msg, higherPrioTaskWoken);
                break;
            case CAN1_BASE:
                xQueueSendToBackFromISR(can1RxQueueHandle, &msg, higherPrioTaskWoken);
                break;
            }

        }
        iflags >>= 1;
        currPos++;
    }
}

static bool prv_can_send(CAN_Type *can, can_msg_t *msg) {
    configASSERT(can);

    uintptr_t canModule = (uintptr_t)can; //Transform the address stored in the pointer into a number
    BaseType_t gotMutex = pdFALSE;
    bool freeBufferFound = false;

    switch (canModule) {
    case CAN0_BASE:
        configASSERT(prvCan0Mutex);
        gotMutex = xSemaphoreTake(prvCan0Mutex, pdMS_TO_TICKS(10));
        break;
    case CAN1_BASE:
        configASSERT(prvCan1Mutex);
        gotMutex = xSemaphoreTake(prvCan1Mutex, pdMS_TO_TICKS(10));
        break;
    default:
        configASSERT(0);
    }

    if (gotMutex) {
        //Iterate over the message buffers to find the first free one
        //The send buffers start at number CAN_MB_SEND_START
        for (size_t buffer = 0; buffer < CAN_MB_SEND; buffer++) {
            if ((can->RAMn[((buffer + CAN_MB_SEND_START) * CAN_MB_SIZE) + 0] & CAN_MB_CODE_MASK) != CAN_MB_CODE_SEND_DATA_MASK) {
                //Current buffer is not busy with sending
                can->RAMn[((buffer + CAN_MB_SEND_START) * CAN_MB_SIZE) + 1] = (msg->ID & 0x7FF) << 18;
                can->RAMn[((buffer + CAN_MB_SEND_START) * CAN_MB_SIZE) + 2] = 0;
                can->RAMn[((buffer + CAN_MB_SEND_START) * CAN_MB_SIZE) + 3] = 0;

                //Fill the payload buffer
                for (size_t byte = 0; byte < msg->DLC; byte++) {
                    if (byte < 4) {
                        can->RAMn[((buffer + CAN_MB_SEND_START) * CAN_MB_SIZE) + 2] |=
                                 ((uint32_t)msg->payload[byte] << (8 * (3 - byte)));
                    } else {
                        can->RAMn[((buffer + CAN_MB_SEND_START) * CAN_MB_SIZE) + 3] |=
                                 ((uint32_t)msg->payload[byte] << (8 * (7 - byte)));
                    }
                }

                //Start arbitration process
                can->RAMn[((buffer + CAN_MB_SEND_START) * CAN_MB_SIZE) + 0] = CAN_MB_CODE_SEND_DATA_MASK |
                                                 CAN_WMBn_CS_SRR_MASK | (msg->DLC << CAN_WMBn_CS_DLC_SHIFT);
                freeBufferFound = true;
                break;
            }
        }

        //No free message buffer found
        //TODO: Request abortion to free a message buffer

        //Give the mutex back
        switch (canModule) {
        case CAN0_BASE:
            xSemaphoreGive(prvCan0Mutex);
            break;
        case CAN1_BASE:
            xSemaphoreGive(prvCan1Mutex);
            break;
        }
    }
    return freeBufferFound;
}

static void prv_can0_send_task(void *p) {
    (void)p;
    can_msg_t msg;
    while (1) {
        if (xQueueReceive(prvCan0TxQueueHandle, &msg, portMAX_DELAY)) {
            prv_can_send(CAN0, &msg);
        }
    }
}

static void prv_can1_send_task(void *p) {
    (void)p;
    can_msg_t msg;
    while (1) {
        if (xQueueReceive(prvCan1TxQueueHandle, &msg, portMAX_DELAY)) {
            prv_can_send(CAN1, &msg);
        }
    }
}

