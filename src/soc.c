/*
 * soc.c
 *
 *  Created on: Apr 30, 2022
 *      Author: Luca Engelmann
 */

#include "soc.h"

static void prv_soc_task(void *p);
static void prv_load_soc(void);
static void prv_store_soc(void);
static void prv_init_nvm(void);
static float prv_max_soc(float cellSoc[][MAX_NUM_OF_CELLS], uint8_t stacks);
static float prv_min_soc(float cellSoc[][MAX_NUM_OF_CELLS], uint8_t stacks);
static float prv_avg_soc(float cellSoc[][MAX_NUM_OF_CELLS], uint8_t stacks);

static uint16_t prv_find_lut_element(uint16_t voltage);
static uint16_t prv_linear_interpolate(uint16_t interpolPoint, uint16_t lutIndex);


static cell_soc_t prvSoc;
static SemaphoreHandle_t prvSocMutex = NULL;
static double prvmAhIntegrationSinceSystemStart = 0.0;

#define SOC_LOOKUP_SIZE 164
#define ELEMENT_SOC 0
#define ELEMENT_VOLTAGE 1
static const uint16_t prvSocLookup[SOC_LOOKUP_SIZE][2];


void init_soc(void) {
    memset(&prvSoc, 0, sizeof(cell_soc_t));
    for (size_t stack = 0; stack < MAX_NUM_OF_SLAVES; stack++) {
        for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
            prvSoc.depthOfDischarge[stack][cell] = (float)NOMINAL_CELL_CAPACITY_mAh / 2;
            prvSoc.cellSoc[stack][cell] = 50.0f;
        }
    }

    prvSocMutex = xSemaphoreCreateMutex();
    configASSERT(prvSocMutex);
    prv_load_soc();
    xTaskCreate(prv_soc_task, "soc", SOC_TASK_STACK, NULL, SOC_TASK_PRIO, NULL);
}

bool soc_lookup(void) {
    uint16_t cellVoltage[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS];
    uint8_t cellVoltageStatus[MAX_NUM_OF_SLAVES][MAX_NUM_OF_CELLS+1];
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

    cell_soc_t* soc = get_soc(pdMS_TO_TICKS(300));

    if (soc == NULL) {
       PRINTF("Error while updating SOC data!\n");
       return false;
    }

    for (size_t stack = 0; stack < NUMBEROFSLAVES; stack++) {
        for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
            uint16_t lutIndex;
            uint16_t voltage = cellVoltage[stack][cell];
            uint16_t socCurrent = 0;

            if (voltage < CELL_UNDERVOLTAGE) {
                socCurrent = 0;
            } else {
                lutIndex = prv_find_lut_element(voltage);
                socCurrent = prv_linear_interpolate(voltage, lutIndex);
            }
            soc->cellSoc[stack][cell] = socCurrent * 0.1f;
        }
    }

    release_soc();
    prv_store_soc();
    return true;
}

void soc_coulomb_count(adc_data_t newData) {
    if (!newData.currentValid) {
        //PRINTF("SOC: current invalid!\n");
        return;
    }

    contactor_SM_state_t tsState = get_contactor_SM_state();
    if (tsState != CONTACTOR_STATE_OPERATE) {
        // Enable Coulomb Counter only when TS is active
        // This removes the error in the SOC calculation due to the hysteresis error of the current sensor in idle
        return;
    }

    cell_soc_t* soc = get_soc(pdMS_TO_TICKS(15));
    if (soc == NULL) {
        PRINTF("Could not get SOC mutex!\n");
        return;
    }

    double chargeDelta = newData.current * (ADC_SAMPLE_PERIOD / 3600.0); //Charge delta in mAh
    prvmAhIntegrationSinceSystemStart += chargeDelta;

    float socDelta = 100.0f * chargeDelta / NOMINAL_CELL_CAPACITY_mAh; //in percent

    for (size_t stack = 0; stack < NUMBEROFSLAVES; stack++) {
        for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
            soc->cellSoc[stack][cell] += socDelta;
            soc->depthOfDischarge[stack][cell] += chargeDelta;
            //Limit values to 0 to 100 percent
            if (soc->cellSoc[stack][cell] < 0.0f) {
                soc->cellSoc[stack][cell] = 0.0f;
            }
            if (soc->cellSoc[stack][cell] > 100.0f) {
                soc->cellSoc[stack][cell] = 100.0f;
            }
        }
    }

    release_soc();
}

cell_soc_t* get_soc(TickType_t blocktime) {
    if (xSemaphoreTake(prvSocMutex, blocktime)) {
        return &prvSoc;
    } else {
        return NULL;
    }
}

bool copy_soc(cell_soc_t *dest, TickType_t blocktime) {
    cell_soc_t* src = get_soc(blocktime);
    if (src != NULL) {
        memcpy(dest, src, sizeof(cell_soc_t));
        release_soc();
        return true;
    } else {
        return false;
    }
}

void release_soc(void) {
    xSemaphoreGive(prvSocMutex);
}

soc_stats_t get_soc_stats(void) {
    soc_stats_t stats;
    memset(&stats, 0, sizeof(soc_stats_t));

    cell_soc_t* soc = get_soc(pdMS_TO_TICKS(100));

    if (soc == NULL) {
        PRINTF("Could not lock EEPROM mutex!\n");
        return stats;
    }

    float minSoc = prv_min_soc(soc->cellSoc, NUMBEROFSLAVES);
    float maxSoc = prv_max_soc(soc->cellSoc, NUMBEROFSLAVES);
    float avgSoc = prv_avg_soc(soc->cellSoc, NUMBEROFSLAVES);

    release_soc();

    stats.minSoc = minSoc;
    stats.maxSoc = maxSoc;
    stats.avgSoc = avgSoc;
    stats.mAhSinceStartup = prvmAhIntegrationSinceSystemStart;
    stats.valid = true; //TODO: Find a way to check the validity of the SOC values

    return stats;
}

static void prv_soc_task(void *p) {
    (void) p;
    const TickType_t period = pdMS_TO_TICKS(2000);
    const TickType_t savePeriod = pdMS_TO_TICKS(180000); //3 minutes
    TickType_t saveCounter = 0;


    TickType_t lastWakeTime = xTaskGetTickCount();

    cell_soc_t soc;
    memset(&soc, 0, sizeof(cell_soc_t));

    while (1) {

        if (saveCounter++ >= (savePeriod / period)) {
            saveCounter = 0;
            prv_store_soc();
        }

        vTaskDelayUntil(&lastWakeTime, period);
    }
}

static void prv_load_soc(void) {
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    uint16_t crcShould;
    bool crcError = false;

    cell_soc_t* soc = get_soc(pdMS_TO_TICKS(300));

    if (soc == NULL) {
        PRINTF("Error while loading SOC data!\n");
        configASSERT(0);
    }


    eeprom_read(pageBuffer, EEPROM_SOC_PAGE_1, EEPROM_PAGESIZE, pdMS_TO_TICKS(500));
    crcShould = (pageBuffer[254] << 8) | pageBuffer[255];

    crcError |= !eeprom_check_crc(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t), crcShould);

    memcpy(&soc->cellSoc[0][0], pageBuffer, 48 * sizeof(float));

    eeprom_read(pageBuffer, EEPROM_SOC_PAGE_2, EEPROM_PAGESIZE, pdMS_TO_TICKS(500));
    crcShould = (pageBuffer[254] << 8) | pageBuffer[255];

    crcError |= !eeprom_check_crc(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t), crcShould);

    memcpy(&soc->cellSoc[4][0], pageBuffer, 48 * sizeof(float));

    eeprom_read(pageBuffer, EEPROM_SOC_PAGE_3, EEPROM_PAGESIZE, pdMS_TO_TICKS(500));
    crcShould = (pageBuffer[254] << 8) | pageBuffer[255];

    crcError |= !eeprom_check_crc(pageBuffer, sizeof(pageBuffer) - sizeof(uint16_t), crcShould);

    memcpy(&soc->cellSoc[8][0], pageBuffer, 48 * sizeof(float));



    release_soc();

    if (crcError) {
        PRINTF("CRC error while loading SOC data! Restoring with default values...\n");
        prv_init_nvm();
        PRINTF("Restoring done!\n");
        prv_load_soc(); //Let's hope that the CRC error does not persist.
    } else {
        PRINTF("Loading SOC data successful!\n");
    }
}

static void prv_store_soc(void) {
    PRINTF("Storing SOC data...\n");
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    uint16_t crc;

    cell_soc_t soc;
    copy_soc(&soc, pdMS_TO_TICKS(300));

    memset(pageBuffer, 0xFF, EEPROM_PAGESIZE);
    memcpy(pageBuffer, &soc.cellSoc[0][0], 48 * sizeof(float));
    crc = eeprom_get_crc16(pageBuffer, EEPROM_PAGESIZE - 2);
    pageBuffer[254] = crc >> 8;
    pageBuffer[255] = crc & 0xFF;


    eeprom_write(pageBuffer, EEPROM_SOC_PAGE_1, EEPROM_PAGESIZE, pdMS_TO_TICKS(1000));
    while(eeprom_busy(100)) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    memset(pageBuffer, 0xFF, EEPROM_PAGESIZE);
    memcpy(pageBuffer, &soc.cellSoc[4][0], 48 * sizeof(float));
    crc = eeprom_get_crc16(pageBuffer, EEPROM_PAGESIZE - 2);
    pageBuffer[254] = crc >> 8;
    pageBuffer[255] = crc & 0xFF;
    eeprom_write(pageBuffer, EEPROM_SOC_PAGE_2, EEPROM_PAGESIZE, pdMS_TO_TICKS(1000));
    while(eeprom_busy(pdMS_TO_TICKS(100))) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    memset(pageBuffer, 0xFF, EEPROM_PAGESIZE);
    memcpy(pageBuffer, &soc.cellSoc[8][0], 48 * sizeof(float));
    crc = eeprom_get_crc16(pageBuffer, EEPROM_PAGESIZE - 2);
    pageBuffer[254] = crc >> 8;
    pageBuffer[255] = crc & 0xFF;
    eeprom_write(pageBuffer, EEPROM_SOC_PAGE_3, EEPROM_PAGESIZE, pdMS_TO_TICKS(1000));
    while(eeprom_busy(pdMS_TO_TICKS(100))) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    release_soc();
    PRINTF("Done!\n");
}

static void prv_init_nvm(void) {
    //Initialize EEPROM with default values
    uint8_t pageBuffer[EEPROM_PAGESIZE];
    uint16_t crc;

    memset(pageBuffer, 0xFF, EEPROM_PAGESIZE);
    memset(pageBuffer, 0,    48 * sizeof(float));
    crc = eeprom_get_crc16(pageBuffer, EEPROM_PAGESIZE - 2);
    pageBuffer[254] = crc >> 8;
    pageBuffer[255] = crc & 0xFF;


    eeprom_write(pageBuffer, EEPROM_SOC_PAGE_1, EEPROM_PAGESIZE, pdMS_TO_TICKS(1000));

    while(eeprom_busy(pdMS_TO_TICKS(100)));

    eeprom_write(pageBuffer, EEPROM_SOC_PAGE_2, EEPROM_PAGESIZE, pdMS_TO_TICKS(1000));
    while(eeprom_busy(pdMS_TO_TICKS(100)));

    eeprom_write(pageBuffer, EEPROM_SOC_PAGE_3, EEPROM_PAGESIZE, pdMS_TO_TICKS(1000));
    while(eeprom_busy(pdMS_TO_TICKS(100)));
}

static uint16_t prv_find_lut_element(uint16_t voltage) {
    for (size_t pos = 0; pos < SOC_LOOKUP_SIZE - 1; pos++) {
        //Find the two points in which the actual voltage lies in between
        uint16_t voltage1 = prvSocLookup[pos][ELEMENT_VOLTAGE];
        uint16_t voltage2 = prvSocLookup[pos + 1][ELEMENT_VOLTAGE];

        if (voltage >= voltage1 && voltage < voltage2) {
            return pos;
        }
    }

    //We have reached the end of the LUT. Cell is probably fully charged
    return SOC_LOOKUP_SIZE;
}

static uint16_t prv_linear_interpolate(uint16_t interpolPoint, uint16_t lutIndex) {
    uint16_t x0, x1, y0, y1;

    if (lutIndex >= SOC_LOOKUP_SIZE) {
        return 1000;
    }

    y0 = prvSocLookup[lutIndex][ELEMENT_SOC];
    x0 = prvSocLookup[lutIndex][ELEMENT_VOLTAGE];
    y1 = prvSocLookup[lutIndex + 1][ELEMENT_SOC];
    x1 = prvSocLookup[lutIndex + 1][ELEMENT_VOLTAGE];

    return (y0 + ((y1-y0)/(x1-x0)) * (interpolPoint - x0));
}

static float prv_max_soc(float cellSoc[][MAX_NUM_OF_CELLS], uint8_t stacks) {
    float temp = -1.0f;
    for (size_t stack = 0; stack < stacks; stack++) {
        for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
            if (cellSoc[stack][cell] > temp) {
                temp = cellSoc[stack][cell];
            }
        }
    }
    return temp;
}

static float prv_min_soc(float cellSoc[][MAX_NUM_OF_CELLS], uint8_t stacks) {
    uint16_t temp = 100.0f;
    for (size_t stack = 0; stack < stacks; stack++) {
        for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
            if (cellSoc[stack][cell] < temp) {
                temp = cellSoc[stack][cell];
            }
        }
    }
    return temp;
}

static float prv_avg_soc(float cellSoc[][MAX_NUM_OF_CELLS], uint8_t stacks) {
    double temp = 0.0;
    for (size_t stack = 0; stack < stacks; stack++) {
        for (size_t cell = 0; cell < MAX_NUM_OF_CELLS; cell++) {
            temp += (double)cellSoc[stack][cell];
        }
    }
    return (float)(temp / (MAX_NUM_OF_CELLS * stacks));
}

static const uint16_t prvSocLookup[SOC_LOOKUP_SIZE][2] = {
{5,   32384}, {11,  33406}, {17,  34121},
{23,  34674}, {29,  35125}, {35,  35504},
{41,  35829}, {47,  36113}, {53,  36362},
{60,  36578}, {66,  36744}, {72,  36830},
{78,  36868}, {84,  36890}, {90,  36905},
{96,  36916}, {102, 36925}, {108, 36933},
{115, 36941}, {121, 36950}, {127, 36961},
{133, 36981}, {139, 37021}, {145, 37070},
{151, 37113}, {157, 37149}, {163, 37179},
{169, 37205}, {176, 37231}, {182, 37258},
{188, 37288}, {194, 37320}, {200, 37350},
{206, 37378}, {212, 37403}, {218, 37425},
{224, 37445}, {231, 37464}, {237, 37481},
{243, 37498}, {249, 37515}, {255, 37534},
{261, 37554}, {267, 37576}, {273, 37598},
{279, 37619}, {285, 37639}, {292, 37657},
{298, 37673}, {304, 37687}, {310, 37700},
{316, 37712}, {322, 37723}, {328, 37734},
{334, 37744}, {340, 37754}, {347, 37765},
{353, 37777}, {359, 37789}, {365, 37802},
{371, 37815}, {377, 37828}, {383, 37842},
{389, 37855}, {395, 37869}, {402, 37883},
{408, 37898}, {414, 37912}, {420, 37927},
{426, 37942}, {432, 37958}, {438, 37973},
{444, 37989}, {450, 38006}, {456, 38023},
{463, 38040}, {469, 38057}, {475, 38075},
{481, 38094}, {487, 38112}, {493, 38132},
{499, 38151}, {505, 38172}, {511, 38193},
{518, 38214}, {524, 38236}, {530, 38258},
{536, 38281}, {542, 38305}, {548, 38329},
{554, 38355}, {560, 38381}, {566, 38407},
{573, 38435}, {579, 38464}, {585, 38494},
{591, 38525}, {597, 38557}, {603, 38590},
{609, 38625}, {615, 38661}, {621, 38698},
{627, 38738}, {634, 38779}, {640, 38822},
{646, 38866}, {652, 38912}, {658, 38959},
{664, 39007}, {670, 39055}, {676, 39102},
{682, 39148}, {689, 39193}, {695, 39236},
{701, 39279}, {707, 39320}, {713, 39360},
{719, 39400}, {725, 39439}, {731, 39476},
{737, 39510}, {744, 39541}, {750, 39570},
{756, 39595}, {762, 39619}, {768, 39643},
{774, 39668}, {780, 39697}, {786, 39728},
{792, 39762}, {798, 39799}, {805, 39839},
{811, 39883}, {817, 39933}, {823, 39988},
{829, 40051}, {835, 40123}, {841, 40207},
{847, 40303}, {853, 40404}, {860, 40506},
{866, 40600}, {872, 40678}, {878, 40737},
{884, 40782}, {890, 40814}, {896, 40839},
{902, 40862}, {908, 40892}, {915, 40934},
{921, 40981}, {927, 41031}, {933, 41083},
{939, 41136}, {945, 41191}, {951, 41246},
{957, 41301}, {963, 41357}, {969, 41413},
{976, 41469}, {982, 41527}, {988, 41585},
{994, 41646}, {1000, 41667}};



