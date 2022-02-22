#ifndef LOGGER_H_
#define LOGGER_H_

#include <stdio.h>
#include <stdbool.h>
#include "S32K146.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "ff.h"
#include "diskio.h"
#include "rtc.h"
#include "config.h"
#include "stacks.h"

extern volatile bool sdInitPending;
void logger_init(void);
void logger_start(void);
void logger_stop(void);
void logger_set_file(FIL *file);
bool logger_is_active(void);
void logger_request_termination(void);
bool logger_terminated(void);
void logger_tick_hook(void);

#endif /* LOGGER_H_ */
