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

static uint16_t find_lut_element(uint16_t voltage);
static uint16_t linear_interpolate(uint16_t interpolPoint, uint16_t lutIndex);


static cellSoc_t _soc;
static SemaphoreHandle_t _socMutex = NULL;

#define SOC_LOOKUP_SIZE 256
#define ELEMENT_SOC 0
#define ELEMENT_VOLTAGE 1
static const uint16_t socLookup[SOC_LOOKUP_SIZE][2];


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
            uint16_t voltage = cellVoltage[stack][cell];
            uint16_t socCurrent = 0;

            if (voltage < CELL_UNDERVOLTAGE) {
                socCurrent = 0;
            } else {
                lutIndex = find_lut_element(voltage);
                socCurrent = linear_interpolate(voltage, lutIndex);
            }
            soc->cellSoc[stack][cell] = socCurrent * 0.1f;
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
    stats.valid = true; //TODO: Find a way to check the validity of the SOC values

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
                //PRINTF("Current invalid!\n");
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

static uint16_t find_lut_element(uint16_t voltage) {
    for (size_t pos = 0; pos < SOC_LOOKUP_SIZE - 1; pos++) {
        //Find the two points in which the actual voltage lies in between
        uint16_t voltage1 = socLookup[pos][ELEMENT_VOLTAGE];
        uint16_t voltage2 = socLookup[pos + 1][ELEMENT_VOLTAGE];

        if (voltage >= voltage1 && voltage < voltage2) {
            return pos;
        }
    }

    //We have reached the end of the LUT. Cell is probably fully charged
    return SOC_LOOKUP_SIZE;
}

static uint16_t linear_interpolate(uint16_t interpolPoint, uint16_t lutIndex) {
    uint16_t x0, x1, y0, y1;

    if (lutIndex >= SOC_LOOKUP_SIZE) {
        return 1000;
    }

    y0 = socLookup[lutIndex][ELEMENT_SOC];
    x0 = socLookup[lutIndex][ELEMENT_VOLTAGE];
    y1 = socLookup[lutIndex + 1][ELEMENT_SOC];
    x1 = socLookup[lutIndex + 1][ELEMENT_VOLTAGE];

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

static const uint16_t socLookup[SOC_LOOKUP_SIZE][2] = { // {soc, cell voltage}
{02,   2535}, {06,   2597}, {10,   2760},
{14,   2845}, {18,   2899}, {22,   2943},
{26,   2981}, {29,   3013}, {33,   3042},
{37,   3067}, {41,   3090}, {45,   3111},
{49,   3129}, {53,   3146}, {57,   3161},
{61,   3175}, {65,   3187}, {69,   3198},
{73,   3209}, {76,   3218}, {80,   3227},
{84,   3236}, {88,   3244}, {92,   3252},
{96,   3260}, {100,  3268}, {104,  3275},
{108,  3283}, {112,  3290}, {116,  3297},
{119,  3304}, {123,  3311}, {127,  3317},
{131,  3324}, {135,  3330}, {139,  3335},
{143,  3340}, {147,  3346}, {151,  3351},
{155,  3356}, {159,  3361}, {163,  3366},
{166,  3371}, {170,  3376}, {174,  3381},
{178,  3386}, {182,  3391}, {186,  3396},
{190,  3401}, {194,  3406}, {198,  3411},
{202,  3416}, {206,  3421}, {209,  3427},
{213,  3433}, {217,  3439}, {221,  3445},
{225,  3452}, {229,  3459}, {233,  3466},
{237,  3474}, {241,  3481}, {245,  3488},
{249,  3497}, {253,  3505}, {256,  3514},
{260,  3520}, {264,  3525}, {268,  3529},
{272,  3534}, {276,  3538}, {280,  3541},
{284,  3545}, {288,  3548}, {292,  3552},
{296,  3555}, {299,  3558}, {303,  3562},
{307,  3566}, {311,  3569}, {315,  3573},
{319,  3577}, {323,  3580}, {327,  3584},
{331,  3588}, {335,  3593}, {339,  3598},
{343,  3602}, {346,  3606}, {350,  3610},
{354,  3615}, {358,  3619}, {362,  3624},
{366,  3628}, {370,  3632}, {374,  3636},
{378,  3640}, {382,  3643}, {386,  3647},
{389,  3651}, {393,  3654}, {397,  3658},
{401,  3662}, {405,  3666}, {409,  3669},
{413,  3673}, {417,  3676}, {421,  3680},
{425,  3683}, {429,  3687}, {433,  3690},
{436,  3694}, {440,  3697}, {444,  3701},
{448,  3704}, {452,  3708}, {456,  3712},
{460,  3715}, {464,  3719}, {468,  3723},
{472,  3726}, {476,  3730}, {479,  3734},
{483,  3738}, {487,  3741}, {491,  3745},
{495,  3749}, {499,  3752}, {503,  3756},
{507,  3760}, {511,  3763}, {515,  3767},
{519,  3770}, {523,  3774}, {526,  3777},
{530,  3781}, {534,  3784}, {538,  3788},
{542,  3791}, {546,  3795}, {550,  3798},
{554,  3801}, {558,  3805}, {562,  3808},
{566,  3811}, {569,  3814}, {573,  3817},
{577,  3821}, {581,  3824}, {585,  3827},
{589,  3830}, {593,  3833}, {597,  3836},
{601,  3840}, {605,  3843}, {609,  3847},
{613,  3851}, {616,  3855}, {620,  3859},
{624,  3863}, {628,  3867}, {632,  3871},
{636,  3873}, {640,  3876}, {644,  3879},
{648,  3881}, {652,  3884}, {656,  3887},
{660,  3889}, {663,  3892}, {667,  3895},
{671,  3898}, {675,  3901}, {679,  3904},
{683,  3908}, {687,  3911}, {691,  3915},
{695,  3918}, {699,  3922}, {703,  3926},
{706,  3930}, {710,  3934}, {714,  3939},
{718,  3943}, {722,  3947}, {726,  3952},
{730,  3956}, {734,  3960}, {738,  3965},
{742,  3969}, {746,  3974}, {750,  3979},
{753,  3983}, {757,  3988}, {761,  3993},
{765,  3997}, {769,  4002}, {773,  4007},
{777,  4011}, {781,  4016}, {785,  4021},
{789,  4025}, {793,  4029}, {796,  4033},
{800,  4037}, {804,  4041}, {808,  4045},
{812,  4048}, {816,  4052}, {820,  4055},
{824,  4059}, {828,  4062}, {832,  4064},
{836,  4067}, {840,  4069}, {843,  4071},
{847,  4073}, {851,  4075}, {855,  4077},
{859,  4078}, {863,  4079}, {867,  4081},
{871,  4082}, {875,  4083}, {879,  4084},
{883,  4085}, {887,  4086}, {890,  4088},
{894,  4089}, {898,  4090}, {902,  4091},
{906,  4092}, {910,  4094}, {914,  4095},
{918,  4096}, {922,  4098}, {926,  4100},
{930,  4101}, {933,  4103}, {937,  4105},
{941,  4107}, {945,  4109}, {949,  4111},
{953,  4114}, {957,  4117}, {961,  4119},
{965,  4123}, {969,  4126}, {973,  4130},
{977,  4134}, {980,  4138}, {984,  4143},
{988,  4151}, {992,  4157}, {996,  4165},
{1000, 4175}};


