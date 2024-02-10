/*!
 * @file            sd.h
 * @brief           Module which handles the interaction with the SD card.
 */

/*
Copyright 2023 Luca Engelmann

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

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

enum {
    SD_FORMAT_DONE,
    SD_FORMAT_BUSY,
    SD_FORMAT_ERROR,
    SD_FORMAT_NO_CARD
};

/*!
 * @brief Function pointer definition for the SD initialization hook.
 * After the SD card has been initialized and the new logfile has been created,
 * this hook function is called. It transfers the status of the SD card and a pointer to the logfile.
 * This hook is also called, if the SD card is removed.
 * This is used to control the logger task.
 */
typedef void (*sd_status_hook_t)(bool ready, FIL *file);

/*!
 * @brief Initialize the SD task.
 * @param sdInitHook Hook function to signalize the SD initialization status. See @ref sd_status_hook_t
 * @note This function does not initialize the SD card! The initialization is performed by the
 * internal task if an SD card has been detected.
 * This function must be called before any other function in this module can be used!
 */
void sd_init(sd_status_hook_t sdInitHook);

/*!
 * @brief Return the initialization status of the SD card.
 * @return true if card is initialized and ready to use, otherwise false
 */
bool sd_initialized(void);

#endif /* SD_H_ */
