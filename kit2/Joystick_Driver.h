#ifndef JOYSTICK_DRIVER_H
#define JOYSTICK_DRIVER_H

#include "LPC17xx.h"

// Your exact hardware layout
#define JOY_UP     (1 << 23)
#define JOY_DOWN   (1 << 25)
#define JOY_LEFT   (1 << 26)
#define JOY_RIGHT  (1 << 24)
#define JOY_CENTER (1 << 20) // Testing P1.27 for Center

void Joystick_Init(void);
uint32_t Joystick_GetState(void);

#endif
