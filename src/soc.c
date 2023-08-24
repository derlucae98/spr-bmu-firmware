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

#define SOC_LOOKUP_SIZE 256
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

    cell_soc_t* soc = get_soc(pdMS_TO_TICKS(15));
    if (soc == NULL) {
        PRINTF("Could not get SOC mutex!\n");
        return;
    }

    double chargeDelta = newData.current * (ADC_SAMPLE_PERIOD / 3600.0); //Charge delta in mAh

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

static const uint16_t prvSocLookup[SOC_LOOKUP_SIZE][2] = { // {soc, cell voltage}
{02,   25350}, {06,   25970}, {10,   27600},
{14,   28450}, {18,   28990}, {22,   29430},
{26,   29810}, {29,   30130}, {33,   30420},
{37,   30670}, {41,   30900}, {45,   31110},
{49,   31290}, {53,   31460}, {57,   31610},
{61,   31750}, {65,   31870}, {69,   31980},
{73,   32090}, {76,   32180}, {80,   32270},
{84,   32360}, {88,   32440}, {92,   32520},
{96,   32600}, {100,  32680}, {104,  32750},
{108,  32830}, {112,  32900}, {116,  32970},
{119,  33040}, {123,  33110}, {127,  33170},
{131,  33240}, {135,  33300}, {139,  33350},
{143,  33400}, {147,  33460}, {151,  33510},
{155,  33560}, {159,  33610}, {163,  33660},
{166,  33710}, {170,  33760}, {174,  33810},
{178,  33860}, {182,  33910}, {186,  33960},
{190,  34010}, {194,  34060}, {198,  34110},
{202,  34160}, {206,  34210}, {209,  34270},
{213,  34330}, {217,  34390}, {221,  34450},
{225,  34520}, {229,  34590}, {233,  34660},
{237,  34740}, {241,  34810}, {245,  34880},
{249,  34970}, {253,  35050}, {256,  35140},
{260,  35200}, {264,  35250}, {268,  35290},
{272,  35340}, {276,  35380}, {280,  35410},
{284,  35450}, {288,  35480}, {292,  35520},
{296,  35550}, {299,  35580}, {303,  35620},
{307,  35660}, {311,  35690}, {315,  35730},
{319,  35770}, {323,  35800}, {327,  35840},
{331,  35880}, {335,  35930}, {339,  35980},
{343,  36020}, {346,  36060}, {350,  36100},
{354,  36150}, {358,  36190}, {362,  36240},
{366,  36280}, {370,  36320}, {374,  36360},
{378,  36400}, {382,  36430}, {386,  36470},
{389,  36510}, {393,  36540}, {397,  36580},
{401,  36620}, {405,  36660}, {409,  36690},
{413,  36730}, {417,  36760}, {421,  36800},
{425,  36830}, {429,  36870}, {433,  36900},
{436,  36940}, {440,  36970}, {444,  37010},
{448,  37040}, {452,  37080}, {456,  37120},
{460,  37150}, {464,  37190}, {468,  37230},
{472,  37260}, {476,  37300}, {479,  37340},
{483,  37380}, {487,  37410}, {491,  37450},
{495,  37490}, {499,  37520}, {503,  37560},
{507,  37600}, {511,  37630}, {515,  37670},
{519,  37700}, {523,  37740}, {526,  37770},
{530,  37810}, {534,  37840}, {538,  37880},
{542,  37910}, {546,  37950}, {550,  37980},
{554,  38010}, {558,  38050}, {562,  38080},
{566,  38110}, {569,  38140}, {573,  38170},
{577,  38210}, {581,  38240}, {585,  38270},
{589,  38300}, {593,  38330}, {597,  38360},
{601,  38400}, {605,  38430}, {609,  38470},
{613,  38510}, {616,  38550}, {620,  38590},
{624,  38630}, {628,  38670}, {632,  38710},
{636,  38730}, {640,  38760}, {644,  38790},
{648,  38810}, {652,  38840}, {656,  38870},
{660,  38890}, {663,  38920}, {667,  38950},
{671,  38980}, {675,  39010}, {679,  39040},
{683,  39080}, {687,  39110}, {691,  39150},
{695,  39180}, {699,  39220}, {703,  39260},
{706,  39300}, {710,  39340}, {714,  39390},
{718,  39430}, {722,  39470}, {726,  39520},
{730,  39560}, {734,  39600}, {738,  39650},
{742,  39690}, {746,  39740}, {750,  39790},
{753,  39830}, {757,  39880}, {761,  39930},
{765,  39970}, {769,  40020}, {773,  40070},
{777,  40110}, {781,  40160}, {785,  40210},
{789,  40250}, {793,  40290}, {796,  40330},
{800,  40370}, {804,  40410}, {808,  40450},
{812,  40480}, {816,  40520}, {820,  40550},
{824,  40590}, {828,  40620}, {832,  40640},
{836,  40670}, {840,  40690}, {843,  40710},
{847,  40730}, {851,  40750}, {855,  40770},
{859,  40780}, {863,  40790}, {867,  40810},
{871,  40820}, {875,  40830}, {879,  40840},
{883,  40850}, {887,  40860}, {890,  40880},
{894,  40890}, {898,  40900}, {902,  40910},
{906,  40920}, {910,  40940}, {914,  40950},
{918,  40960}, {922,  40980}, {926,  41000},
{930,  41010}, {933,  41030}, {937,  41050},
{941,  41070}, {945,  41090}, {949,  41110},
{953,  41140}, {957,  41170}, {961,  41190},
{965,  41230}, {969,  41260}, {973,  41300},
{977,  41340}, {980,  41380}, {984,  41430},
{988,  41510}, {992,  41570}, {996,  41650},
{1000, 41750}};


