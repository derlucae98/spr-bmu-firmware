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
static FATFS FatFs;
static FIL file;
static sd_status_hook_t prvSdInitHook = NULL;
static bool prvDeleteOldest = false;
static bool prvLoggerEnabled = false;
static uint8_t prvFormatStatus = 0;
static bool prvFormatCard = false;

static void prv_sd_init_task(void *p);
static bool prv_mount(void);
static bool prv_create_file(void);
static bool prv_get_number_of_files(uint8_t *numberOfFiles);
static bool prv_get_oldest_file(FILINFO *oldest);
static void prv_sd_format();

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

bool sd_get_file_list(FILINFO **entries, uint8_t *numberOfEntries) {
    if (prvLoggerEnabled) {
        return false;
    }

    FRESULT res;
    DIR dir;
    static FILINFO fno;
    uint8_t index = 0;
    static FILINFO files[MAX_NUMBER_OF_LOGFILES];

    res = f_opendir(&dir, "");

    if (res != FR_OK) {
        return false;
    }

    res = f_findfirst(&dir, &fno, "", "*.log");
    files[0] = fno;

    if (res != FR_OK) {
        f_closedir(&dir);
        return false;
    }

    while (res == FR_OK && fno.fname[0]) {
        PRINTF("%u: %s\n", index, fno.fname);
        index++;

        if (index >= MAX_NUMBER_OF_LOGFILES) {
            f_closedir(&dir);
            return false;
        }

        res = f_findnext(&dir, &fno);
        files[index] = fno;
    }

    if (entries != NULL) {
        *entries = files;
    }


    if (numberOfEntries != NULL) {
        *numberOfEntries = index;
    }

    f_closedir(&dir);

    return true;
}

static bool prv_create_file(void) {
    FRESULT status;
    char* timestamp = rtc_get_timestamp();
    char path[24];

    snprintf(path, 24, "%s.log", timestamp);

    uint8_t timeout = 100;
    do {
        status = f_open(&file, path, FA_WRITE | FA_OPEN_APPEND | FA_CREATE_NEW);
        if (--timeout == 0) {
            break;
        }
    } while (status != FR_OK);

    if (status != FR_OK) {

        prvSdInitialized = false;
        return false;
    }

    f_sync(&file);

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
    prvFormatStatus = SD_FORMAT_DONE;
    return;
}

static bool prv_mount(void) {
    FRESULT status;
    status = f_mount(&FatFs, "", 1);

    if (status != FR_OK) {
        prvSdInitialized = false;
        return false;
    }
    return true;
}

static bool prv_get_number_of_files(uint8_t *numberOfFiles) {
    FRESULT res;
    DIR dir;
    static FILINFO fno;
    uint8_t index = 0;

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
        index++;
        res = f_findnext(&dir, &fno);
    }

    f_closedir(&dir);

    *numberOfFiles = index;
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

            uint8_t numberOfFiles;
            res = prv_get_number_of_files(&numberOfFiles);

            if (res != true) {
                PRINTF("Could not read from SD card!\n");
                continue;
            }

            PRINTF("Number of files: %u\n", numberOfFiles);

            /* Either delete the oldest file oder stop with an error,
             * if more than MAX_NUMBER_OF_LOGFILES files are on the card.
             * This can be configured by the user.*/

            FILINFO oldest;
            bool maxNumberReached = false;

            if (prvLoggerEnabled) {
                while (numberOfFiles > MAX_NUMBER_OF_LOGFILES) {
                    if (prvDeleteOldest) {
                        PRINTF("Deleting oldest file...\n");
                        prv_get_oldest_file(&oldest);
                        sd_delete_file(oldest);
                        numberOfFiles--;
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

            FILINFO *files = NULL;
            sd_get_file_list(&files, NULL);

            prvSdInitialized = true;
            if (prvSdInitHook != NULL) {
                (prvSdInitHook)(prvSdInitialized, &file);
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

