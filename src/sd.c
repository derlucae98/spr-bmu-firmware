#include "sd.h"


volatile bool prvSdInitialized = true;
static FATFS FatFs;
static FIL file;
static sd_status_hook_t prvSdInitHook = NULL;

static void sd_init_task(void *p);

void sd_init(sd_status_hook_t sdInitHook) {
    prvSdInitHook = sdInitHook;
    xTaskCreate(sd_init_task, "sd", SD_TASK_STACK, NULL, SD_TASK_PRIO, NULL);
}

bool sd_initialized(void) {
    return prvSdInitialized;
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

            PRINTF("Done!");
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

