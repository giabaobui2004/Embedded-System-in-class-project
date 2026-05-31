#include "Joystick_Driver.h"

void Joystick_Init(void) {
    // Configure all 5 joystick pins as digital inputs
    LPC_GPIO1->FIODIR &= ~(JOY_UP | JOY_DOWN | JOY_LEFT | JOY_RIGHT | JOY_CENTER);
}

uint32_t Joystick_GetState(void) {
    // Read the port and invert bits (Active Low keys change from 0 to 1 when pressed)
    uint32_t port_val = ~LPC_GPIO1->FIOPIN;
    return (port_val & (JOY_UP | JOY_DOWN | JOY_LEFT | JOY_RIGHT | JOY_CENTER));
}
