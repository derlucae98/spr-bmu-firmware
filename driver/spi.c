#include <spi.h>

static SemaphoreHandle_t _spi0Mutex = NULL;
static SemaphoreHandle_t _spi1Mutex = NULL;
static SemaphoreHandle_t _spi2Mutex = NULL;

static volatile TaskHandle_t dmaTaskToNotify[3] = {NULL};
static volatile TaskHandle_t spiTaskToNotify[3] = {NULL};
static uint8_t dmaEnabled[3] = {0};

static void spi_dma_move_array(LPSPI_Type *spi, uint8_t *a, size_t len); //Use DMA for large transfers
static void spi_irq_move_array(LPSPI_Type *spi, uint8_t *a, size_t len); //Default mode

void spi_init(LPSPI_Type *spi, uint8_t presc, uint8_t mode) {
    configASSERT(spi);

    uintptr_t spiModule = (uintptr_t)spi;
    uint32_t spiIrq = 0;

    switch (spiModule) {
    case LPSPI0_BASE:
        spiIrq = LPSPI0_IRQn;
        dmaEnabled[0] = 0;
        PCC->PCCn[PCC_LPSPI0_INDEX]  = PCC_PCCn_PCS(1) | PCC_PCCn_CGC(1);  //Set clock to option 1: SOSCDIV2_CLK
        _spi0Mutex = xSemaphoreCreateRecursiveMutex();
        configASSERT(_spi0Mutex);
        break;
    case LPSPI1_BASE:
        spiIrq = LPSPI1_IRQn;
        dmaEnabled[1] = 0;
        PCC->PCCn[PCC_LPSPI1_INDEX]  = PCC_PCCn_PCS(6) | PCC_PCCn_CGC(1);  //Set clock to option 6: SPLLDIV2
        _spi1Mutex = xSemaphoreCreateRecursiveMutex();
        configASSERT(_spi1Mutex);
        break;
    case LPSPI2_BASE:
        dmaEnabled[2] = 0;
        spiIrq = LPSPI2_IRQn;
        PCC->PCCn[PCC_LPSPI2_INDEX]  = PCC_PCCn_PCS(1) | PCC_PCCn_CGC(1);  //Set clock to option 1: SOSCDIV2_CLK
        _spi2Mutex = xSemaphoreCreateRecursiveMutex();
        configASSERT(_spi2Mutex);
        break;
    }

    spi->CFGR1  = LPSPI_CFGR1_MASTER(1); //Set master mode
    spi->TCR    = LPSPI_TCR_PRESCALE(presc) | LPSPI_TCR_CPOL((mode & 0x2) >> 1) |
                  LPSPI_TCR_CPHA(mode & 0x1) | LPSPI_TCR_FRAMESZ(7); //8-bit frame size
    spi->IER    |= LPSPI_IER_TCIE_MASK; //Enable Transfer complete interrupt
    spi->CR     = LPSPI_CR_MEN_MASK; //Enable SPI module

    nvic_set_priority(spiIrq, 0xFF);
    nvic_enable_irq(spiIrq);

}

void spi_enable_dma(LPSPI_Type *spi) {
    configASSERT(spi);
    uint32_t dmaIRQ = 0;
    uint32_t spiIrq = 0;

    spi->IER &= ~LPSPI_IER_TCIE_MASK; //Disable transfer complete interrupt
    spi->DER = LPSPI_DER_TDDE(1) | LPSPI_DER_RDDE(1); //DMA RX/TX request enabled

    PCC->PCCn[PCC_DMAMUX_INDEX] |= PCC_PCCn_CGC_MASK;

    uintptr_t spiModule = (uintptr_t)spi;

    /* SPI0: DMA channel 0 TX, 1 RX
     * SPI1: DMA channel 2 TX, 3 RX
     * SPI2: DMA channel 4 TX, 5 RX
     */
    switch (spiModule) {
    case LPSPI0_BASE:
        dmaIRQ = DMA1_IRQn;
        spiIrq = LPSPI0_IRQn;
        dmaEnabled[0] = 1;
        DMAMUX->CHCFG[0] = DMAMUX_CHCFG_ENBL_MASK | DMAMUX_CHCFG_SOURCE(15); //Source: LPSPI0 TX request
        DMAMUX->CHCFG[1] = DMAMUX_CHCFG_ENBL_MASK | DMAMUX_CHCFG_SOURCE(14); //Source: LPSPI0 RX request
        break;
    case LPSPI1_BASE:
        dmaIRQ = DMA3_IRQn;
        spiIrq = LPSPI1_IRQn;
        dmaEnabled[1] = 1;
        DMAMUX->CHCFG[2] = DMAMUX_CHCFG_ENBL_MASK | DMAMUX_CHCFG_SOURCE(17); //Source: LPSPI1 TX request
        DMAMUX->CHCFG[3] = DMAMUX_CHCFG_ENBL_MASK | DMAMUX_CHCFG_SOURCE(16); //Source: LPSPI1 RX request
        break;
    case LPSPI2_BASE:
        dmaEnabled[2] = 1;
        dmaIRQ = DMA5_IRQn;
        spiIrq = LPSPI2_IRQn;
        DMAMUX->CHCFG[4] = DMAMUX_CHCFG_ENBL_MASK | DMAMUX_CHCFG_SOURCE(19); //Source: LPSPI2 TX request
        DMAMUX->CHCFG[5] = DMAMUX_CHCFG_ENBL_MASK | DMAMUX_CHCFG_SOURCE(18); //Source: LPSPI2 RX request
        break;
    }

    nvic_set_priority(dmaIRQ, 0xFF);
    nvic_enable_irq(dmaIRQ);
    nvic_disable_irq(spiIrq);
}

void spi_disable_dma(LPSPI_Type *spi) {
    configASSERT(spi);
    uint32_t dmaIRQ = 0;
    uint32_t spiIrq = 0;

    spi->DER = LPSPI_DER_TDDE(0) | LPSPI_DER_RDDE(0); //DMA RX/TX request disabled

    uintptr_t spiModule = (uintptr_t)spi;

    /* SPI0: DMA channel 0 TX, 1 RX
     * SPI1: DMA channel 2 TX, 3 RX
     * SPI2: DMA channel 4 TX, 5 RX
     */
    switch (spiModule) {
    case LPSPI0_BASE:
        dmaIRQ = DMA1_IRQn;
        spiIrq = LPSPI0_IRQn;
        dmaEnabled[0] = 0;
        DMAMUX->CHCFG[0] = 0;
        DMAMUX->CHCFG[1] = 0;
        break;
    case LPSPI1_BASE:
        dmaIRQ = DMA3_IRQn;
        spiIrq = LPSPI0_IRQn;
        dmaEnabled[1] = 0;
        DMAMUX->CHCFG[2] = 0;
        DMAMUX->CHCFG[3] = 0;
        break;
    case LPSPI2_BASE:
        dmaIRQ = DMA5_IRQn;
        spiIrq = LPSPI0_IRQn;
        dmaEnabled[2] = 0;
        DMAMUX->CHCFG[4] = 0;
        DMAMUX->CHCFG[5] = 0;
        break;
    }

    nvic_disable_irq(dmaIRQ);
    nvic_enable_irq(spiIrq);
}

uint8_t spi_move(LPSPI_Type *spi, uint8_t b) {
    uint8_t rec = 0;
    uintptr_t spiModule = (uintptr_t)spi;

    switch (spiModule) {
    case LPSPI0_BASE:
        spiTaskToNotify[0] = xTaskGetCurrentTaskHandle();
        break;
    case LPSPI1_BASE:
        spiTaskToNotify[1] = xTaskGetCurrentTaskHandle();
        break;
    case LPSPI2_BASE:
        spiTaskToNotify[2] = xTaskGetCurrentTaskHandle();
        break;
    }

    if (spi_mutex_take(spi, portMAX_DELAY)) {
        while(!(spi->SR & LPSPI_SR_TDF_MASK)); //Wait for TX-Fifo
        spi->TDR = (uint32_t)b; //Transmit data

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        rec = (uint8_t)spi->RDR; //Read received data
        spi_mutex_give(spi);
    }
    return rec;
}

static void spi_irq_move_array(LPSPI_Type *spi, uint8_t *a, size_t len) {
    if (spi_mutex_take(spi, portMAX_DELAY)) {
        for (size_t i = 0; i < len; i++) {
            a[i] = spi_move(spi, a[i]);
        }
        spi_mutex_give(spi);
    }
}

void spi_move_array(LPSPI_Type *spi, uint8_t *a, size_t len) {
    uintptr_t spiModule = (uintptr_t)spi;
    uint8_t spi_module = 0;

    switch (spiModule) {
        case LPSPI0_BASE:
            spi_module = 0;
            break;
        case LPSPI1_BASE:
            spi_module = 1;
            break;
        case LPSPI2_BASE:
            spi_module = 2;
            break;
    }
    if (dmaEnabled[spi_module]) {
        spi_dma_move_array(spi, a, len);
    } else {
        spi_irq_move_array(spi, a, len);
    }
}

void spi_send_array(LPSPI_Type *spi, const uint8_t *a, size_t len) {
    if (spi_mutex_take(spi, portMAX_DELAY)) {
        for (size_t i = 0; i < len; i++) {
            spi_move(spi, a[i]);
        }
        spi_mutex_give(spi);
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

static void spi_dma_move_array(LPSPI_Type *spi, uint8_t *a, size_t len) {

    uintptr_t spiModule = (uintptr_t)spi;
    volatile uint8_t dmaChannel;

    switch (spiModule) {
    case LPSPI0_BASE:
       dmaChannel = 0;
       break;
    case LPSPI1_BASE:
       dmaChannel = 2;
       break;
    case LPSPI2_BASE:
       dmaChannel = 4;
       break;
    }
    if (spi_mutex_take(spi, portMAX_DELAY)) {

        //TX DMA
        DMA->TCD[dmaChannel].SADDR = (uint32_t)a; //Source address
        DMA->TCD[dmaChannel].SOFF = 1; //1 byte offset after each read
        DMA->TCD[dmaChannel].ATTR = 0; //8 bit transfers, no modulo
        DMA->TCD[dmaChannel].NBYTES.MLOFFNO = DMA_TCD_NBYTES_MLOFFNO_NBYTES(1); //1 byte per minor loop
        DMA->TCD[dmaChannel].SLAST = -len; //Address adjustment after a major loop
        DMA->TCD[dmaChannel].DADDR = (uint32_t)&(spi->TDR); //Destination address
        DMA->TCD[dmaChannel].DOFF = 0; //Destination offset
        DMA->TCD[dmaChannel].CITER.ELINKNO = len;
        DMA->TCD[dmaChannel].BITER.ELINKNO = len;
        DMA->TCD[dmaChannel].CSR = DMA_TCD_CSR_DREQ_MASK; //clear ERQ bit after major loop

        //RX DMA
        DMA->TCD[dmaChannel+1].SADDR = (uint32_t)&(spi->RDR); //Source address
        DMA->TCD[dmaChannel+1].SOFF = 0;
        DMA->TCD[dmaChannel+1].ATTR = 0; //8 bit transfers, no modulo
        DMA->TCD[dmaChannel+1].NBYTES.MLOFFNO = DMA_TCD_NBYTES_MLOFFNO_NBYTES(1); //1 byte per minor loop
        DMA->TCD[dmaChannel+1].SLAST = 0;
        DMA->TCD[dmaChannel+1].DADDR = (uint32_t)a;
        DMA->TCD[dmaChannel+1].DOFF = 1; //Destination offset
        DMA->TCD[dmaChannel+1].DLASTSGA = -len;
        DMA->TCD[dmaChannel+1].CITER.ELINKNO = len;
        DMA->TCD[dmaChannel+1].BITER.ELINKNO = len;
        DMA->TCD[dmaChannel+1].CSR = DMA_TCD_CSR_INTMAJOR_MASK; //Interrupt after major loop

        dmaTaskToNotify[dmaChannel / 2] = xTaskGetCurrentTaskHandle();

        //Start DMA
        DMA->SERQ = dmaChannel;
        DMA->SERQ = dmaChannel+1;

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        spi_mutex_give(spi);

    }
}

void DMA1_IRQHandler(void) {
    BaseType_t higherPrioTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(dmaTaskToNotify[0], &higherPrioTaskWoken);
    dmaTaskToNotify[0] = NULL;
    DMA->CINT = DMA_CINT_CINT(1);
    portYIELD_FROM_ISR(higherPrioTaskWoken);
}

void DMA3_IRQHandler(void) {
    BaseType_t higherPrioTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(dmaTaskToNotify[1], &higherPrioTaskWoken);
    dmaTaskToNotify[1] = NULL;
    DMA->CINT = DMA_CINT_CINT(3);
    portYIELD_FROM_ISR(higherPrioTaskWoken);
}

void DMA5_IRQHandler(void) {
    BaseType_t higherPrioTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(dmaTaskToNotify[2], &higherPrioTaskWoken);
    dmaTaskToNotify[2] = NULL;
    DMA->CINT = DMA_CINT_CINT(5);
    portYIELD_FROM_ISR(higherPrioTaskWoken);
}

void LPSPI0_IRQHandler(void) {
    BaseType_t higherPrioTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(spiTaskToNotify[0], &higherPrioTaskWoken);
    spiTaskToNotify[0] = NULL;
    LPSPI0->SR |= LPSPI_SR_TCF_MASK;
    portYIELD_FROM_ISR(higherPrioTaskWoken);
}

void LPSPI1_IRQHandler(void) {
    BaseType_t higherPrioTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(spiTaskToNotify[1], &higherPrioTaskWoken);
    LPSPI1->SR |= LPSPI_SR_TCF_MASK;
    spiTaskToNotify[1] = NULL;
    portYIELD_FROM_ISR(higherPrioTaskWoken);
}

void LPSPI2_IRQHandler(void) {
    BaseType_t higherPrioTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(spiTaskToNotify[2], &higherPrioTaskWoken);
    spiTaskToNotify[2] = NULL;
    LPSPI2->SR |= LPSPI_SR_TCF_MASK;
    portYIELD_FROM_ISR(higherPrioTaskWoken);
}



