#ifndef LOGGER_H_
#define LOGGER_H_



#include <stdio.h>
#include <stdbool.h>
#include "S32K14x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "sd.h"
#include <stdlib.h>
#include "rtc.h"
#include "config.h"
#include "stacks.h"
#include "adc.h"
#include "contactor.h"
#include "soc.h"

typedef struct {
    uint32_t timestamp;
    uint32_t relativeTime;
    adc_data_t adcData;
    stacks_data_t stacksData;
    contactor_error_t stateMachineError;
    contactor_SM_state_t tsState;
    uint32_t resetReason;
    soc_stats_t soc;
    uint16_t isoResistance;
    bool isoResistanceValid;
} log_data_t;

void logger_init(void);

void logger_set_file(bool cardStatus, FIL *file);
void logger_control(bool active);
void logger_tick_hook(uint32_t uptime);


#endif /* LOGGER_H_ */
