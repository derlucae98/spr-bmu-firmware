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
#include "base64.h"
#include "contactor.h"


extern volatile bool sdInitPending;
void logger_init(void);
void logger_control(bool ready, FIL *file);
bool logger_is_active(void);
void logger_request_termination(void);
bool logger_terminated(void);
void logger_tick_hook(uint32_t uptime);

#endif /* LOGGER_H_ */
