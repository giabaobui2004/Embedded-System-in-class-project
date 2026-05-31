#include "Adc_Driver.h"
#include "LPC17xx.h"

void ADC_Init_Hardware(void) {
    LPC_SC->PCONP |= (1 << 12);        // Power up ADC core clock channel
    LPC_PINCON->PINSEL1 &= ~(3 << 18); // Clear configuration bits
    LPC_PINCON->PINSEL1 |=  (1 << 18); // Map P0.25 function line to AD0.2 input
    
    LPC_ADC->ADCR = (1 << 2)  |        // Select Channel AD0.2
                    (4 << 8)  |        // Set internal clock divider
                    (1 << 21);         // Set Operational Operational Power Mode (PDN)
}

uint16_t ADC_Read_Knob(void) {
    LPC_ADC->ADCR |= (1 << 24);              // Force immediate start bit conversion
    while (!(LPC_ADC->ADGDR & (1UL << 31))); // Block loop efficiently until Done bit registers
    return (LPC_ADC->ADGDR >> 4) & 0xFFF;     // Isolate clean 12-bit register value
}