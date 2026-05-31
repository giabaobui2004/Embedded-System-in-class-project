#ifndef CAN_MCAL_H
#define CAN_MCAL_H

#include "LPC17xx.h"
#include "System_Defines.h"

typedef uint32_t Can_IdType;
typedef struct {
    Can_IdType id;
    uint8_t    length;
    uint8_t* sdu; 
} Can_PduType;

void CAN_Init(uint32_t baudrate);
void CAN_Init_Interrupt(uint32_t baudrate);
void CAN_Transmit(uint32_t id, uint8_t* data, uint8_t len);
void Can_Write(uint8_t hth, const Can_PduType* PduInfo);

uint8_t CAN_Receive(uint32_t* id, uint8_t* data);

#endif
