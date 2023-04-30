#include "sd.h"


volatile bool prvSdInitialized = true;
static FATFS FatFs;
static FIL file;
static sd_status_hook_t prvSdInitHook = NULL;

static void sd_init_task(void *p);
static bool prv_mount(void);
static bool prv_create_file(void);
static bool prv_get_number_of_files(uint8_t *numberOfFiles);
static bool prv_get_oldest_file(FILINFO *oldest);

static bool prv_delete_oldest = true;


void sd_init(sd_status_hook_t sdInitHook) {
    prvSdInitHook = sdInitHook;
    xTaskCreate(sd_init_task, "sd", SD_TASK_STACK, NULL, SD_TASK_PRIO, NULL);
}

bool sd_initialized(void) {
    return prvSdInitialized;
}

bool sd_get_file_list(FILINFO *entries, uint8_t *numberOfEntries) {
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
        files[index] = fno;
        res = f_findnext(&dir, &fno);
    }

    if (entries != NULL) {
        entries = files;
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
    char path[32];

    snprintf(path, 32, "%s.log", timestamp);

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
    f_close(&file);

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

bool sd_format(void) {
    FRESULT res;
    static BYTE work[4 * FF_MAX_SS];
    TickType_t start;
    TickType_t finish;

    PRINTF("Formatting SD card...\n");
    start = xTaskGetTickCount();
    f_unmount("");

    res = f_mkfs("", 0, work, sizeof work);

    if (res != FR_OK) {
        PRINTF("Formatting SD card failed!\n");
        return false;
    }

    prv_mount();
    finish = xTaskGetTickCount();

    PRINTF("Done! Took %lu seconds\n", (finish - start) / 1000);
    return true;
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


static void sd_init_task(void *p) {
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
             * if more than MAX_NUMBER_OF_LOGFILES files are on the card. */

            FILINFO oldest;
            bool maxNumberReached = false;

            while (numberOfFiles > MAX_NUMBER_OF_LOGFILES) {
                if (prv_delete_oldest) {
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

            sd_get_file_list(NULL, NULL);

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

        vTaskDelayUntil(&lastWake, period);
    }
}

