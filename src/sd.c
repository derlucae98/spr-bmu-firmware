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
static bool prvDeleteOldest = false;
static bool prvLoggerEnabled = false;
static uint8_t prvFormatStatus = 0;
static bool prvFormatCard = false;
static FILINFO prvFiles[MAX_NUMBER_OF_LOGFILES];
static uint8_t prvNumberOfFiles;

static void prv_sd_init_task(void *p);
static bool prv_mount(void);
static bool prv_create_file(void);
static bool prv_get_oldest_file(FILINFO *oldest);
static void prv_sd_format();
static bool prv_get_file_list(void);


void sd_init(sd_status_hook_t sdInitHook) {
    prvSdInitHook = sdInitHook;
    config_t* config  = get_config(pdMS_TO_TICKS(500));
    if (config != NULL) {
        prvDeleteOldest = config->loggerDeleteOldestFile;
        prvLoggerEnabled = config->loggerEnable; //Do not create a logfile if logger is disabled
        release_config();
    }
    xTaskCreate(prv_sd_init_task, "sd", SD_TASK_STACK, NULL, SD_TASK_PRIO, NULL);
}

bool sd_initialized(void) {
    return prvSdInitialized;
}

void sd_get_file_list(FILINFO **entries, uint8_t *numberOfEntries) {
    if (entries != NULL) {
        *entries = prvFiles;
    }


    if (numberOfEntries != NULL) {
        *numberOfEntries = prvNumberOfFiles;
    }
}

static bool prv_create_file(void) {
    FRESULT status;
    char* timestamp = rtc_get_timestamp();
    char path[24];

    snprintf(path, 24, "%s.log", timestamp);

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

bool sd_delete_file(FILINFO file) {
    FRESULT res;
    res = f_unlink(file.fname);
    if (res != FR_OK) {
        return false;
    }
    return true;
}

void sd_format(void) {
    if (prvFormatStatus != SD_FORMAT_BUSY) {
        //Do not request formatting if it is already in progress!
        prvFormatStatus = SD_FORMAT_DONE; //Initialize with default value
        prvFormatCard = true;
    }
}

uint8_t sd_format_status(void) {
    return prvFormatStatus;
}

static void prv_sd_format() {
    if (prvFormatStatus == SD_FORMAT_BUSY) {
        return; //If formatting already in process, stop
    }

    FRESULT res;
    static BYTE work[4 * FF_MAX_SS];
    TickType_t start;
    TickType_t finish;

    prvFormatStatus = SD_FORMAT_BUSY;
    PRINTF("Formatting SD card...\n");
    start = xTaskGetTickCount();
    f_unmount("");

    res = f_mkfs("", 0, work, sizeof work);

    if (res != FR_OK) {
        PRINTF("Formatting SD card failed!\n");
        prvFormatStatus = SD_FORMAT_ERROR;
        return;
    }

    prv_mount();
    finish = xTaskGetTickCount();

    PRINTF("Done! Took %lu seconds\n", (finish - start) / 1000);
    prv_get_file_list();
    prvFormatStatus = SD_FORMAT_DONE;
    return;
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

static bool prv_get_oldest_file(FILINFO *oldest) {
    FRESULT res;
    DIR dir;
    static FILINFO fno;

    uint32_t oldestFile = 0xFFFFFFFF;
    uint32_t lastModified = 0;

    res = f_opendir(&dir, "");

    if (res != FR_OK) {
        return false;
    }

    res = f_findfirst(&dir, &fno, "", "*.log");

    if (res != FR_OK) {
        f_closedir(&dir);
        return false;
    }

    while (res == FR_OK && fno.fname[0]) {
        lastModified = ((uint32_t)(fno.fdate << 16) | fno.ftime);

        if (lastModified < oldestFile) {
            oldestFile = lastModified;
            *oldest = fno;
        }

        res = f_findnext(&dir, &fno);
    }

    f_closedir(&dir);

    return true;
}

static bool prv_get_file_list(void) {
    FRESULT res;
    DIR dir;
    static FILINFO fno;

    res = f_opendir(&dir, "");

    if (res != FR_OK) {
        return false;
    }

    prvNumberOfFiles = 0;

    res = f_findfirst(&dir, &fno, "", "*.log");
    prvFiles[0] = fno;

    if (res != FR_OK) {
        f_closedir(&dir);
        return false;
    }

    while (res == FR_OK && fno.fname[0]) {
        PRINTF("%u: %s\n", prvNumberOfFiles, fno.fname);
        prvNumberOfFiles++;

        if (prvNumberOfFiles >= MAX_NUMBER_OF_LOGFILES) {
            f_closedir(&dir);
            return false;
        }

        res = f_findnext(&dir, &fno);
        prvFiles[prvNumberOfFiles] = fno;
    }

    f_closedir(&dir);

    return true;
}

bool sd_open_file_read(char *name) {
    FRESULT res;
    char path[32];
    memset(path, 0, sizeof(path));
    strcat(path, "0:");
    strcat(path, name);
    PRINTF("path: %s\n", path);
    res = f_open(&prvFile, path, FA_READ);
    PRINTF("res: %u\n", res);
    if (res != FR_OK) {
        return false;
    }
    return true;
}

bool sd_close_file(void) {
    FRESULT res;
    res = f_close(&prvFile);
    if (res != FR_OK) {
        return false;
    }
    return true;
}

bool sd_read_file(uint8_t *buffer, size_t btr, size_t *br) {
    FRESULT res;
    res = f_read(&prvFile, buffer, btr, br);

    if (res == FR_OK) {
        return true;
    } else {
        return false;
    }
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

            res = prv_get_file_list();

            if (res != true) {
                PRINTF("Could not read from SD card!\n");
                continue;
            }

            PRINTF("Number of files: %u\n", prvNumberOfFiles);

            /* Either delete the oldest file oder stop with an error,
             * if more than MAX_NUMBER_OF_LOGFILES files are on the card.
             * This can be configured by the user.*/

            FILINFO oldest;
            bool maxNumberReached = false;

            if (prvLoggerEnabled) {
                while (prvNumberOfFiles > MAX_NUMBER_OF_LOGFILES) {
                    if (prvDeleteOldest) {
                        PRINTF("Deleting oldest file...\n");
                        prv_get_oldest_file(&oldest);
                        sd_delete_file(oldest);
                        prvNumberOfFiles--;
                        PRINTF("Done!\n");
                    } else {
                        maxNumberReached = true;
                        break;
                    }
                }

                if (maxNumberReached) {
                    PRINTF("Cannot store more than %u logfiles!\n", MAX_NUMBER_OF_LOGFILES);
                    continue;
                }

                PRINTF("Creating logfile...\n");

                res = prv_create_file();

                if (res != true) {
                    PRINTF("SD file creation failed!\n");
                    continue;
                }

                PRINTF("Done!\n");
            }

            sd_get_file_list(NULL, NULL);

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

        if (prvFormatCard) {
            prvFormatCard = false;
            if (!prvSdInitialized) {
                prvFormatStatus = SD_FORMAT_NO_CARD;
            } else {
                prv_sd_format();
            }
        }

        vTaskDelayUntil(&lastWake, period);
    }
}

