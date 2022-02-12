#include <spi.h>

static SemaphoreHandle_t _spi0Mutex = NULL;
static SemaphoreHandle_t _spi1Mutex = NULL;
static SemaphoreHandle_t _spi2Mutex = NULL;

void spi_init(LPSPI_Type *spi, uint8_t presc, uint8_t mode) {
    configASSERT(spi);

    uintptr_t spiModule = (uintptr_t)spi;

    switch (spiModule) {
    case LPSPI0_BASE:
        PCC->PCCn[PCC_LPSPI0_INDEX]  = PCC_PCCn_PCS(1) | PCC_PCCn_CGC(1);  //Set clock to option 1: SOSCDIV2_CLK
        _spi0Mutex = xSemaphoreCreateRecursiveMutex();
        configASSERT(_spi0Mutex);
        break;
    case LPSPI1_BASE:
        PCC->PCCn[PCC_LPSPI1_INDEX]  = PCC_PCCn_PCS(1) | PCC_PCCn_CGC(1);  //Set clock to option 1: SOSCDIV2_CLK
        _spi1Mutex = xSemaphoreCreateRecursiveMutex();
        configASSERT(_spi1Mutex);
        break;
    case LPSPI2_BASE:
        PCC->PCCn[PCC_LPSPI2_INDEX]  = PCC_PCCn_PCS(1) | PCC_PCCn_CGC(1);  //Set clock to option 1: SOSCDIV2_CLK
        _spi2Mutex = xSemaphoreCreateRecursiveMutex();
        configASSERT(_spi2Mutex);
        break;
    }

    spi->CFGR1  = LPSPI_CFGR1_MASTER(1); //Set master mode
    spi->TCR    = LPSPI_TCR_PRESCALE(presc) | LPSPI_TCR_CPOL((mode & 0x2) >> 1) |
                  LPSPI_TCR_CPHA(mode & 0x1) | LPSPI_TCR_FRAMESZ(7); //8-bit frame size
    spi->CR     = LPSPI_CR_MEN_MASK; //Enable SPI module
}

uint8_t spi_move(LPSPI_Type *spi, uint8_t b) {
    uint8_t rec = 0;
    if (spi_mutex_take(spi, pdMS_TO_TICKS(10))) {
        while(!(spi->SR & LPSPI_SR_TDF_MASK)); //Wait for TX-Fifo
        spi->TDR = (uint32_t)b; //Transmit data
        while(!(spi->SR & LPSPI_SR_RDF_MASK)); //Wait for received data to be ready
        rec = (uint8_t)spi->RDR; //Read received data
        spi_mutex_give(spi);
    }
    return rec;
}

void spi_move_array(LPSPI_Type *spi, uint8_t *a, size_t len) {
    if (spi_mutex_take(spi, pdMS_TO_TICKS(10))) {
        for (size_t i = 0; i < len; i++) {
            a[i] = spi_move(spi, a[i]);
        }
        spi_mutex_give(spi);
    }
}

void spi_send_array(LPSPI_Type *spi, const uint8_t *a, size_t len) {
    for (size_t i = 0; i < len; i++) {
        spi_move(spi, a[i]);
    }
}

BaseType_t spi_mutex_take(LPSPI_Type *spi, TickType_t blockTime) {
    configASSERT(spi);
    uintptr_t spiModule = (uintptr_t)spi;
    switch (spiModule) {
    case LPSPI0_BASE:
        configASSERT(_spi0Mutex);
        return xSemaphoreTakeRecursive(_spi0Mutex, blockTime);
    case LPSPI1_BASE:
        configASSERT(_spi1Mutex);
        return xSemaphoreTakeRecursive(_spi1Mutex, blockTime);
    case LPSPI2_BASE:
        configASSERT(_spi2Mutex);
        return xSemaphoreTakeRecursive(_spi2Mutex, blockTime);
    default:
        configASSERT(0);
    }
}
void spi_mutex_give(LPSPI_Type *spi) {
    configASSERT(spi);
    uintptr_t spiModule = (uintptr_t)spi;
    switch (spiModule) {
    case LPSPI0_BASE:
        xSemaphoreGiveRecursive(_spi0Mutex);
        break;
    case LPSPI1_BASE:
        xSemaphoreGiveRecursive(_spi1Mutex);
        break;
    case LPSPI2_BASE:
        xSemaphoreGiveRecursive(_spi2Mutex);
        break;
    default:
        configASSERT(0);
    }
}



