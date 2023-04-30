
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

#define MAX_NUMBER_OF_LOGFILES 128 //Each logfile takes up 56 bytes in the list of files

typedef void (*sd_status_hook_t)(bool ready, FIL *file);

void sd_init(sd_status_hook_t sdInitHook);
bool sd_get_file_list(FILINFO *entries, uint8_t *numberOfEntries);
bool sd_initialized(void);

#endif /* SD_H_ */
