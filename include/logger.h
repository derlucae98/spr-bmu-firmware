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

#include "rtc.h"
#include "config.h"
#include "stacks.h"
#include "adc.h"
#include "contactor.h"

#define LOGDATA_STRING_LENGTH 256 //Change as needed

typedef struct {
    char data[LOGDATA_STRING_LENGTH];
} log_data_t;

void logger_init(void);
void logger_control(bool ready, FIL *file);
void logger_tick_hook(uint32_t uptime);


#endif /* LOGGER_H_ */
