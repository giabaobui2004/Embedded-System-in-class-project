#ifndef UART_DEBUG_H
#define UART_DEBUG_H

#include "LPC17xx.h"

void UART0_Init(uint32_t baudrate);
void UART0_SendString(char* str);

#endif