/*
 * soc.c
 *
 *  Created on: Apr 30, 2022
 *      Author: Luca Engelmann
 */

#include "soc.h"

static void soc_task(void *p);
static void load_soc(void);
static void store_soc(void);
static void init_nvm(void);
static float max_soc(float cellSoc[][MAXCELLS], uint8_t stacks);
static float min_soc(float cellSoc[][MAXCELLS], uint8_t stacks);
static float avg_soc(float cellSoc[][MAXCELLS], uint8_t stacks);

static uint16_t find_lut_element(float voltage);
static float linear_interpolate(float interpolPoint, uint16_t lutIndex);


static cellSoc_t _soc;
static SemaphoreHandle_t _socMutex = NULL;

#define SOC_LOOKUP_SIZE 256
#define ELEMENT_SOC 0
#define ELEMENT_VOLTAGE 1
static const float socLookup[SOC_LOOKUP_SIZE][2];


void init_soc(void) {
    memset(&_soc, 0, sizeof(cellSoc_t));
    _socMutex = xSemaphoreCreateMutex();
    configASSERT(_socMutex);
    load_soc();
    xTaskCreate(soc_task, "soc", SOC_TASK_STACK, NULL, SOC_TASK_PRIO, NULL);
}

bool soc_lookup(void) {
    uint16_t cellVoltage[MAXSTACKS][MAXCELLS];
    uint8_t cellVoltageStatus[MAXSTACKS][MAXCELLS+1];
    stacks_data_t* stacksData = get_stacks_data(pdMS_TO_TICKS(200));
    if (stacksData != NULL) {
        memcpy(cellVoltage, stacksData->cellVoltage, sizeof(cellVoltage));
        memcpy(cellVoltageStatus, stacksData->cellVoltageStatus, sizeof(cellVoltageStatus));
        release_stacks_data();
    } else {
        return false;
    }

    bool voltagesValid = false;
    voltagesValid = check_voltage_validity(cellVoltageStatus, NUMBEROFSLAVES);

    if (!voltagesValid) {
        return false;
    }

    cellSoc_t* soc = get_soc(pdMS_TO_TICKS(300));

    if (soc == NULL) {
       PRINTF("Error while updating SOC data!\n");
       return false;
    }

    for (size_t stack = 0; stack < NUMBEROFSLAVES; stack++) {
        for (size_t cell = 0; cell < MAXCELLS; cell++) {
            uint16_t lutIndex;
            float voltage = cellVoltage[stack][cell];
            float socCurrent = 0.0f;

            lutIndex = find_lut_element(voltage);
            socCurrent = linear_interpolate(voltage, lutIndex);
            soc->cellSoc[stack][cell] = socCurrent;
        }
    }

    release_soc();
    store_soc();
    return true;
}

cellSoc_t* get_soc(TickType_t blocktime) {
    if (xSemaphoreTake(_socMutex, blocktime)) {
        return &_soc;
    } else {
        return NULL;
    }
}

bool copy_soc(cellSoc_t *dest, TickType_t blocktime) {
    cellSoc_t* src = get_soc(blocktime);
    if (src != NULL) {
        memcpy(dest, src, sizeof(cellSoc_t));
        release_soc();
        return true;
    } else {
        return false;
    }
}

void release_soc(void) {
    xSemaphoreGive(_socMutex);
}

soc_stats_t get_soc_stats(void) {
    soc_stats_t stats;
    memset(&stats, 0, sizeof(soc_stats_t));

    cellSoc_t* soc = get_soc(pdMS_TO_TICKS(100));

    if (soc == NULL) {
        PRINTF("Could not lock EEPROM mutex!\n");
        return stats;
    }

    float minSoc = min_soc(soc->cellSoc, NUMBEROFSLAVES);
    float maxSoc = max_soc(soc->cellSoc, NUMBEROFSLAVES);
    float avgSoc = avg_soc(soc->cellSoc, NUMBEROFSLAVES);

    release_soc();

    stats.minSoc = minSoc;
    stats.maxSoc = maxSoc;
    stats.avgSoc = avgSoc;
    stats.valid = true;

    return stats;
}

static void soc_task(void *p) {
    (void) p;
    const TickType_t period = pdMS_TO_TICKS(25);
    const TickType_t savePeriod = pdMS_TO_TICKS(180000); //3 minutes
    TickType_t saveCounter = 0;


    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1) {
        sensor_data_t *sensorData = get_sensor_data(portMAX_DELAY);
        float current = 0.0f;
        bool currentValid = false;

        if (sensorData != NULL) {
            current = sensorData->current;
            currentValid = sensorData->currentValid;
            release_sensor_data();
        }

        cellSoc_t* soc = get_soc(pdMS_TO_TICKS(15));
        if (soc != NULL) {
            if (currentValid) {
                current = 100.0f;
                float chargeDelta = current * period * 2.7777E-4; //Charge delta in mAh
                float socDelta = 100.0f * chargeDelta / NOMINAL_CELL_CAPACITY_mAh; //in percent

                for (size_t stack = 0; stack < NUMBEROFSLAVES; stack++) {
                    for (size_t cell = 0; cell < MAXCELLS; cell++) {
                        soc->cellSoc[stack][cell] += socDelta;
                        //Limit values to 0 to 100 percent
                        if (soc->cellSoc[stack][cell] < 0.0f) {
                            soc->cellSoc[stack][cell] = 0.0f;
                        }
                        if (soc->cellSoc[stack][cell] > 100.0f) {
                            soc->cellSoc[stack][cell] = 100.0f;
                        }
                    }
                }
            } else {
                PRINTF("Current invalid!\n");
            }

            release_soc();

            saveCounter++;
            if (saveCounter >= (savePeriod / period)) {
                saveCounter = 0;
                store_soc();
            }

        } else {
            PRINTF("Couldn't obtain SOC mutex!\n");
        }

        vTaskDelayUntil(&lastWakeTime, period);
    }
}

static void load_soc(void) {
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    uint16_t crcShould;
    bool crcError = false;

    cellSoc_t* soc = get_soc(pdMS_TO_TICKS(300));

    if (soc == NULL) {
        PRINTF("Error while loading SOC data!\n");
        configASSERT(0);
    }

    if (eeprom_mutex_take(pdMS_TO_TICKS(1000))) {
        eeprom_read_array(pageBuffer, EEPROM_SOC_PAGE_1, EEPROM_PAGESIZE);
        crcShould = (pageBuffer[254] << 8) | pageBuffer[255];

        crcError |= !eeprom_check_crc(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t), crcShould);

        memcpy(&soc->cellSoc[0][0], pageBuffer, 48 * sizeof(float));

        eeprom_read_array(pageBuffer, EEPROM_SOC_PAGE_2, EEPROM_PAGESIZE);
        crcShould = (pageBuffer[254] << 8) | pageBuffer[255];

        crcError |= !eeprom_check_crc(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t), crcShould);

        memcpy(&soc->cellSoc[4][0], pageBuffer, 48 * sizeof(float));

        eeprom_read_array(pageBuffer, EEPROM_SOC_PAGE_3, EEPROM_PAGESIZE);
        crcShould = (pageBuffer[254] << 8) | pageBuffer[255];

        crcError |= !eeprom_check_crc(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t), crcShould);

        memcpy(&soc->cellSoc[8][0], pageBuffer, 48 * sizeof(float));
        eeprom_mutex_give();
    }

    release_soc();

    if (crcError) {
        PRINTF("CRC error while loading SOC data! Restoring with default values...\n");
        init_nvm();
        PRINTF("Restoring done!\n");
        load_soc(); //Let's hope that the CRC error does not persist.
    } else {
        PRINTF("Loading SOC data successful!\n");
    }
}

static void store_soc(void) {
    PRINTF("Storing SOC data...\n");
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    uint16_t crc;

    cellSoc_t* soc = get_soc(pdMS_TO_TICKS(300));

    if (soc == NULL) {
        PRINTF("Could not lock SOC mutex!\n");
        return;
    }

    memset(pageBuffer, 0xFF, EEPROM_PAGESIZE);
    memcpy(pageBuffer, &soc->cellSoc[0][0], 48 * sizeof(float));
    crc = eeprom_get_crc16(pageBuffer, EEPROM_PAGESIZE - 2);
    pageBuffer[254] = crc >> 8;
    pageBuffer[255] = crc & 0xFF;

    if (eeprom_mutex_take(pdMS_TO_TICKS(1000))) {
        eeprom_write_page(pageBuffer, EEPROM_SOC_PAGE_1, EEPROM_PAGESIZE);
        while(!eeprom_has_write_finished()) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }

        memset(pageBuffer, 0xFF, EEPROM_PAGESIZE);
        memcpy(pageBuffer, &soc->cellSoc[4][0], 48 * sizeof(float));
        crc = eeprom_get_crc16(pageBuffer, EEPROM_PAGESIZE - 2);
        pageBuffer[254] = crc >> 8;
        pageBuffer[255] = crc & 0xFF;
        eeprom_write_page(pageBuffer, EEPROM_SOC_PAGE_2, EEPROM_PAGESIZE);
        while(!eeprom_has_write_finished()) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }

        memset(pageBuffer, 0xFF, EEPROM_PAGESIZE);
        memcpy(pageBuffer, &soc->cellSoc[8][0], 48 * sizeof(float));
        crc = eeprom_get_crc16(pageBuffer, EEPROM_PAGESIZE - 2);
        pageBuffer[254] = crc >> 8;
        pageBuffer[255] = crc & 0xFF;
        eeprom_write_page(pageBuffer, EEPROM_SOC_PAGE_3, EEPROM_PAGESIZE);
        while(!eeprom_has_write_finished()) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }

        eeprom_mutex_give();
    } else {
        PRINTF("Could not lock EEPROM mutex!\n");
    }

    release_soc();
    PRINTF("Done!\n");
}

static void init_nvm(void) {
    //Initialize EEPROM with default values
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    uint16_t crc;

    memset(pageBuffer, 0xFF, EEPROM_PAGESIZE);
    memset(pageBuffer, 0,    48 * sizeof(float));
    crc = eeprom_get_crc16(pageBuffer, EEPROM_PAGESIZE - 2);
    pageBuffer[254] = crc >> 8;
    pageBuffer[255] = crc & 0xFF;

    if (eeprom_mutex_take(pdMS_TO_TICKS(1000))) {
        eeprom_write_page(pageBuffer, EEPROM_SOC_PAGE_1, EEPROM_PAGESIZE);

        while(!eeprom_has_write_finished());

        eeprom_write_page(pageBuffer, EEPROM_SOC_PAGE_2, EEPROM_PAGESIZE);
        while(!eeprom_has_write_finished());

        eeprom_write_page(pageBuffer, EEPROM_SOC_PAGE_3, EEPROM_PAGESIZE);
        while(!eeprom_has_write_finished());

        eeprom_mutex_give();
    } else {
        PRINTF("Could not lock EEPROM mutex!\n");
    }
}

static uint16_t find_lut_element(float voltage) {
    for (size_t pos = 0; pos < SOC_LOOKUP_SIZE - 1; pos++) {
        //Find the two points in which the actual voltage lies in between
        float voltage1 = socLookup[pos][ELEMENT_VOLTAGE];
        float voltage2 = socLookup[pos + 1][ELEMENT_VOLTAGE];

        if (voltage >= voltage1 && voltage < voltage2) {
            return pos;
        }
    }

    //We have reached the end of the LUT. Cell is probably fully charged
    return SOC_LOOKUP_SIZE;
}

static float linear_interpolate(float interpolPoint, uint16_t lutIndex) {
    float x0, x1, y0, y1;

    if (lutIndex >= SOC_LOOKUP_SIZE) {
        return 100.0f;
    }

    x0 = socLookup[lutIndex][ELEMENT_SOC];
    y0 = socLookup[lutIndex][ELEMENT_VOLTAGE];
    x1 = socLookup[lutIndex + 1][ELEMENT_SOC];
    y1 = socLookup[lutIndex + 1][ELEMENT_VOLTAGE];

    return (y0 + ((y1-y0)/(x1-x0)) * (interpolPoint - x0));
}

static float max_soc(float cellSoc[][MAXCELLS], uint8_t stacks) {
    float temp = -1.0f;
    for (size_t stack = 0; stack < stacks; stack++) {
        for (size_t cell = 0; cell < MAXCELLS; cell++) {
            if (cellSoc[stack][cell] > temp) {
                temp = cellSoc[stack][cell];
            }
        }
    }
    return temp;
}

static float min_soc(float cellSoc[][MAXCELLS], uint8_t stacks) {
    uint16_t temp = 100.0f;
    for (size_t stack = 0; stack < stacks; stack++) {
        for (size_t cell = 0; cell < MAXCELLS; cell++) {
            if (cellSoc[stack][cell] < temp) {
                temp = cellSoc[stack][cell];
            }
        }
    }
    return temp;
}

static float avg_soc(float cellSoc[][MAXCELLS], uint8_t stacks) {
    double temp = 0.0;
    for (size_t stack = 0; stack < stacks; stack++) {
        for (size_t cell = 0; cell < MAXCELLS; cell++) {
            temp += (double)cellSoc[stack][cell];
        }
    }
    return (float)(temp / (MAXCELLS * stacks));
}

static const float socLookup[SOC_LOOKUP_SIZE][2] = { // {soc, cell voltage}
{0.2f,   2.535f}, {0.6f,   2.597f}, {1.0f,   2.760f},
{1.4f,   2.845f}, {1.8f,   2.899f}, {2.2f,   2.943f},
{2.6f,   2.981f}, {2.9f,   3.013f}, {3.3f,   3.042f},
{3.7f,   3.067f}, {4.1f,   3.090f}, {4.5f,   3.111f},
{4.9f,   3.129f}, {5.3f,   3.146f}, {5.7f,   3.161f},
{6.1f,   3.175f}, {6.5f,   3.187f}, {6.9f,   3.198f},
{7.3f,   3.209f}, {7.6f,   3.218f}, {8.0f,   3.227f},
{8.4f,   3.236f}, {8.8f,   3.244f}, {9.2f,   3.252f},
{9.6f,   3.260f}, {10.0f,  3.268f}, {10.4f,  3.275f},
{10.8f,  3.283f}, {11.2f,  3.290f}, {11.6f,  3.297f},
{11.9f,  3.304f}, {12.3f,  3.311f}, {12.7f,  3.317f},
{13.1f,  3.324f}, {13.5f,  3.330f}, {13.9f,  3.335f},
{14.3f,  3.340f}, {14.7f,  3.346f}, {15.1f,  3.351f},
{15.5f,  3.356f}, {15.9f,  3.361f}, {16.3f,  3.366f},
{16.6f,  3.371f}, {17.0f,  3.376f}, {17.4f,  3.381f},
{17.8f,  3.386f}, {18.2f,  3.391f}, {18.6f,  3.396f},
{19.0f,  3.401f}, {19.4f,  3.406f}, {19.8f,  3.411f},
{20.2f,  3.416f}, {20.6f,  3.421f}, {20.9f,  3.427f},
{21.3f,  3.433f}, {21.7f,  3.439f}, {22.1f,  3.445f},
{22.5f,  3.452f}, {22.9f,  3.459f}, {23.3f,  3.466f},
{23.7f,  3.474f}, {24.1f,  3.481f}, {24.5f,  3.488f},
{24.9f,  3.497f}, {25.3f,  3.505f}, {25.6f,  3.514f},
{26.0f,  3.520f}, {26.4f,  3.525f}, {26.8f,  3.529f},
{27.2f,  3.534f}, {27.6f,  3.538f}, {28.0f,  3.541f},
{28.4f,  3.545f}, {28.8f,  3.548f}, {29.2f,  3.552f},
{29.6f,  3.555f}, {29.9f,  3.558f}, {30.3f,  3.562f},
{30.7f,  3.566f}, {31.1f,  3.569f}, {31.5f,  3.573f},
{31.9f,  3.577f}, {32.3f,  3.580f}, {32.7f,  3.584f},
{33.1f,  3.588f}, {33.5f,  3.593f}, {33.9f,  3.598f},
{34.3f,  3.602f}, {34.6f,  3.606f}, {35.0f,  3.610f},
{35.4f,  3.615f}, {35.8f,  3.619f}, {36.2f,  3.624f},
{36.6f,  3.628f}, {37.0f,  3.632f}, {37.4f,  3.636f},
{37.8f,  3.640f}, {38.2f,  3.643f}, {38.6f,  3.647f},
{38.9f,  3.651f}, {39.3f,  3.654f}, {39.7f,  3.658f},
{40.1f,  3.662f}, {40.5f,  3.666f}, {40.9f,  3.669f},
{41.3f,  3.673f}, {41.7f,  3.676f}, {42.1f,  3.680f},
{42.5f,  3.683f}, {42.9f,  3.687f}, {43.3f,  3.690f},
{43.6f,  3.694f}, {44.0f,  3.697f}, {44.4f,  3.701f},
{44.8f,  3.704f}, {45.2f,  3.708f}, {45.6f,  3.712f},
{46.0f,  3.715f}, {46.4f,  3.719f}, {46.8f,  3.723f},
{47.2f,  3.726f}, {47.6f,  3.730f}, {47.9f,  3.734f},
{48.3f,  3.738f}, {48.7f,  3.741f}, {49.1f,  3.745f},
{49.5f,  3.749f}, {49.9f,  3.752f}, {50.3f,  3.756f},
{50.7f,  3.760f}, {51.1f,  3.763f}, {51.5f,  3.767f},
{51.9f,  3.770f}, {52.3f,  3.774f}, {52.6f,  3.777f},
{53.0f,  3.781f}, {53.4f,  3.784f}, {53.8f,  3.788f},
{54.2f,  3.791f}, {54.6f,  3.795f}, {55.0f,  3.798f},
{55.4f,  3.801f}, {55.8f,  3.805f}, {56.2f,  3.808f},
{56.6f,  3.811f}, {56.9f,  3.814f}, {57.3f,  3.817f},
{57.7f,  3.821f}, {58.1f,  3.824f}, {58.5f,  3.827f},
{58.9f,  3.830f}, {59.3f,  3.833f}, {59.7f,  3.836f},
{60.1f,  3.840f}, {60.5f,  3.843f}, {60.9f,  3.847f},
{61.3f,  3.851f}, {61.6f,  3.855f}, {62.0f,  3.859f},
{62.4f,  3.863f}, {62.8f,  3.867f}, {63.2f,  3.871f},
{63.6f,  3.873f}, {64.0f,  3.876f}, {64.4f,  3.879f},
{64.8f,  3.881f}, {65.2f,  3.884f}, {65.6f,  3.887f},
{66.0f,  3.889f}, {66.3f,  3.892f}, {66.7f,  3.895f},
{67.1f,  3.898f}, {67.5f,  3.901f}, {67.9f,  3.904f},
{68.3f,  3.908f}, {68.7f,  3.911f}, {69.1f,  3.915f},
{69.5f,  3.918f}, {69.9f,  3.922f}, {70.3f,  3.926f},
{70.6f,  3.930f}, {71.0f,  3.934f}, {71.4f,  3.939f},
{71.8f,  3.943f}, {72.2f,  3.947f}, {72.6f,  3.952f},
{73.0f,  3.956f}, {73.4f,  3.960f}, {73.8f,  3.965f},
{74.2f,  3.969f}, {74.6f,  3.974f}, {75.0f,  3.979f},
{75.3f,  3.983f}, {75.7f,  3.988f}, {76.1f,  3.993f},
{76.5f,  3.997f}, {76.9f,  4.002f}, {77.3f,  4.007f},
{77.7f,  4.011f}, {78.1f,  4.016f}, {78.5f,  4.021f},
{78.9f,  4.025f}, {79.3f,  4.029f}, {79.6f,  4.033f},
{80.0f,  4.037f}, {80.4f,  4.041f}, {80.8f,  4.045f},
{81.2f,  4.048f}, {81.6f,  4.052f}, {82.0f,  4.055f},
{82.4f,  4.059f}, {82.8f,  4.062f}, {83.2f,  4.064f},
{83.6f,  4.067f}, {84.0f,  4.069f}, {84.3f,  4.071f},
{84.7f,  4.073f}, {85.1f,  4.075f}, {85.5f,  4.077f},
{85.9f,  4.078f}, {86.3f,  4.079f}, {86.7f,  4.081f},
{87.1f,  4.082f}, {87.5f,  4.083f}, {87.9f,  4.084f},
{88.3f,  4.085f}, {88.7f,  4.086f}, {89.0f,  4.088f},
{89.4f,  4.089f}, {89.8f,  4.090f}, {90.2f,  4.091f},
{90.6f,  4.092f}, {91.0f,  4.094f}, {91.4f,  4.095f},
{91.8f,  4.096f}, {92.2f,  4.098f}, {92.6f,  4.100f},
{93.0f,  4.101f}, {93.3f,  4.103f}, {93.7f,  4.105f},
{94.1f,  4.107f}, {94.5f,  4.109f}, {94.9f,  4.111f},
{95.3f,  4.114f}, {95.7f,  4.117f}, {96.1f,  4.119f},
{96.5f,  4.123f}, {96.9f,  4.126f}, {97.3f,  4.130f},
{97.7f,  4.134f}, {98.0f,  4.138f}, {98.4f,  4.143f},
{98.8f,  4.151f}, {99.2f,  4.157f}, {99.6f,  4.165f},
{100.0f, 4.175f}};


