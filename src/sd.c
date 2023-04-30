#include "sd.h"


volatile bool prvSdInitialized = true;
static FATFS FatFs;
static FIL file;
static sd_status_hook_t prvSdInitHook = NULL;

static void sd_init_task(void *p);
static bool prv_delete_oldest_file(void);

void sd_init(sd_status_hook_t sdInitHook) {
    prvSdInitHook = sdInitHook;
    xTaskCreate(sd_init_task, "sd", SD_TASK_STACK, NULL, SD_TASK_PRIO, NULL);
}

bool sd_initialized(void) {
    return prvSdInitialized;
}

bool sd_list_files(void) {
    FRESULT res;
    DIR dir;
    static FILINFO fno;
    uint8_t index = 0;


    uint32_t oldest = 0xFFFFFFFF;
    uint32_t lastModified = 0;
    char oldestFile[32];

    res = f_opendir(&dir, "");

    if (res != FR_OK) {
        PRINTF("SD: Failed to open directory!\n");
        return false;
    }

    PRINTF("Done!\n");
    PRINTF("SD: Reading files...\n");

    res = f_findfirst(&dir, &fno, "", "*.log");
    index++;

    if (res != FR_OK) {
        PRINTF("SD: Error reading files!\n");
        f_closedir(&dir);
        return false;
    }

    while (res == FR_OK && fno.fname[0]) {
        PRINTF("%u: %s\n", index, fno.fname);
        index++;

        lastModified = ((uint32_t)(fno.fdate << 16) | fno.ftime);
        if (lastModified < oldest) {
            oldest = lastModified;
            strcpy(oldestFile, fno.fname);
        }

        res = f_findnext(&dir, &fno);
    }

    if (index >= 30) {
        PRINTF("Deleting oldest file...\n");
        res = f_unlink(oldestFile);
        if (res != FR_OK) {
            PRINTF("SD: Error deleting file!\n");
            f_closedir(&dir);
            return false;
        }
        PRINTF("Done!\n");
    } else {
        sd_list_files();
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
            PRINTF("SD inserted!\n");
            sdAvailableLast = true;
            FRESULT status = f_mount(&FatFs, "", 1);

            if (status != FR_OK) {
                PRINTF("SD initialization failed!\n");
                prvSdInitialized = false;
                continue;
            }

            PRINTF("SD ready!\n");
            char* timestamp = rtc_get_timestamp();
            char path[32];

            snprintf(path, 32, "%s.log", timestamp);

            PRINTF("Creating file...\n");
            uint8_t timeout = 100;
            do {
                status = f_open(&file, path, FA_WRITE | FA_OPEN_APPEND | FA_CREATE_NEW);
                if (--timeout == 0) {
                    break;
                }
            } while (status != FR_OK);

            if (status != FR_OK) {
                PRINTF("SD file creation failed!\n");
                prvSdInitialized = false;
                continue;
            }

            f_sync(&file);

            PRINTF("Done!\n");

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

