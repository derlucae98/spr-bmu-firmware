#include "clock.h"

void clock_init(void) {

    //Setup crystal oscillator
    SCG->SOSCDIV = SCG_SOSCDIV_SOSCDIV1(1) | SCG_SOSCDIV_SOSCDIV2(1); //SOSCDIV1 = 1, SOSCDIV2 = 1
    SCG->SOSCCFG = SCG_SOSCCFG_RANGE(2) | SCG_SOSCCFG_EREFS(1); //High frequency range external crystal
    while(SCG->SOSCCSR & SCG_SOSCCSR_LK_MASK); //Ensure SOSCCSR is unlocked
    SCG->SOSCCSR = SCG_SOSCCSR_SOSCEN(1);    //Enable oscillator
    while(!(SCG->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK));  //Wait for the clock to be valid

    //Setup PLL
    while(SCG->SPLLCSR & SCG_SPLLCSR_LK_MASK); //Wait for register to be written
    SCG->SPLLCSR &= ~SCG_SPLLCSR_SPLLEN_MASK;  //Disable PLL

    SCG->SPLLDIV |=   SCG_SPLLDIV_SPLLDIV1(2) | //SPLLDIV1 divide by 2
                      SCG_SPLLDIV_SPLLDIV2(3);  // SPLLDIV2 divide by 4

    SCG->SPLLCFG = SCG_SPLLCFG_MULT(24); //SPLL_CLK = ((8 MHz / 1) * 40) / 2 = 160 MHz

    SCG->SPLLCSR |= SCG_SPLLCSR_SPLLEN_MASK; //Enable PLL
    while(!(SCG->SPLLCSR & SCG_SPLLCSR_SPLLVLD_MASK)); //Wait for PLL to be valid

    SCG->SIRCDIV = SCG_SIRCDIV_SIRCDIV1(1) | SCG_SIRCDIV_SIRCDIV2(1);

    //Normal run mode (80 MHz)
    SCG->RCCR = SCG_RCCR_SCS(6) | SCG_RCCR_DIVCORE(1) | SCG_RCCR_DIVBUS(1) | SCG_RCCR_DIVSLOW(2);

    while (((SCG->CSR & SCG_CSR_SCS_MASK) >> SCG_CSR_SCS_SHIFT ) != 6); //Wait for clock source = SPLL

    SystemCoreClockUpdate();
}
