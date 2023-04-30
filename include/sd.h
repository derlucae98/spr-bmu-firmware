
#ifndef SD_H_
#define SD_H_

#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "config.h"
#include "gpio.h"
#include "uart.h"
#include "ff.h"
#include "diskio.h"
#include "rtc.h"

typedef void (*sd_status_hook_t)(bool ready, FIL *file);

void sd_init(sd_status_hook_t sdInitHook);
bool sd_list_files(void);
bool sd_initialized(void);

#endif /* SD_H_ */
