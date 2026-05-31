#ifndef UI_CONTROL_H
#define UI_CONTROL_H

#include "stdint.h"

extern volatile uint8_t current_temperature;
extern volatile uint8_t current_humidity;
extern volatile uint8_t kit1_ac_feedback;

void UI_MenuTask(void const *argument);
void Update_Kit2_Hardware_LEDs(uint8_t mask);

#endif