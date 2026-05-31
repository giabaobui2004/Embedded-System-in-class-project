#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

#include "stdint.h"

// Public API prototypes
void ADC_Init_Hardware(void);
uint16_t ADC_Read_Knob(void);

#endif