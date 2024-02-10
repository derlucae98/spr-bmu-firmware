/*!
 * @file            sd.c
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

#include "sd.h"

volatile bool prvSdInitialized = false;
static FATFS prvFatFs;
static FIL prvFile;
static sd_status_hook_t prvSdInitHook = NULL;

static void prv_sd_init_task(void *p);
static bool prv_mount(void);
static bool prv_create_file(void);



void sd_init(sd_status_hook_t sdInitHook) {
    prvSdInitHook = sdInitHook;
    xTaskCreate(prv_sd_init_task, "sd", SD_TASK_STACK, NULL, SD_TASK_PRIO, NULL);
}

bool sd_initialized(void) {
    return prvSdInitialized;
}

static bool prv_create_file(void) {
    FRESULT status;
    char* timestamp = rtc_get_timestamp();
    char path[24];

    snprintf(path, 24, "%s.csv", timestamp);

    uint8_t timeout = 100;
    do {
        status = f_open(&prvFile, path, FA_WRITE | FA_OPEN_APPEND | FA_CREATE_NEW);
        if (--timeout == 0) {
            break;
        }
    } while (status != FR_OK);

    if (status != FR_OK) {

        prvSdInitialized = false;
        return false;
    }

    f_sync(&prvFile);

    return true;
}

static bool prv_mount(void) {
    FRESULT status;
    status = f_mount(&prvFatFs, "", 1);

    if (status != FR_OK) {
        prvSdInitialized = false;
        return false;
    }
    return true;
}

static void prv_sd_init_task(void *p) {
    (void) p;

    const TickType_t period = pdMS_TO_TICKS(100);
    TickType_t lastWake = xTaskGetTickCount();

    while (1) {
        //SD insertion or removal detection
        static bool sdAvailableLast = false;
        bool sdAvailable = !get_pin(CARD_DETECT_PORT, CARD_DETECT_PIN);

        if (sdAvailable && !sdAvailableLast) {
            char *timestamp;
            timestamp = rtc_get_timestamp();
            PRINTF("%s\n", timestamp);

            PRINTF("SD inserted!\n");
            sdAvailableLast = true;

            bool res;

            PRINTF("Mounting SD card...\n");

            res = prv_mount();

            if (res != true) {
                PRINTF("Mounting SD card failed!\n");
                continue;
            }

            PRINTF("Done!\n");
            PRINTF("Creating logfile...\n");

            res = prv_create_file();

            if (res != true) {
                PRINTF("SD file creation failed!\n");
                continue;
            }

            PRINTF("Done!\n");

            prvSdInitialized = true;
            if (prvSdInitHook != NULL) {
                (prvSdInitHook)(prvSdInitialized, &prvFile);
            }

        } else if (!sdAvailable && sdAvailableLast) {
            PRINTF("SD removed!\n");
            sdAvailableLast = false;
            if (!disk_status(0)) {
                f_unmount("");
            }

            prvSdInitialized = false;

            if (prvSdInitHook != NULL) {
                (prvSdInitHook)(prvSdInitialized, NULL);
            }
        }

        vTaskDelayUntil(&lastWake, period);
    }
}

