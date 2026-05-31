#ifndef CAN_MCAL_H
#define CAN_MCAL_H

#include "LPC17xx.h"

// AUTOSAR Standard Data Types Configuration
typedef uint32_t Can_IdType;
typedef struct {
    Can_IdType id;
    uint8_t    length;
    uint8_t* sdu; 
} Can_PduType;

// Function Prototypes
void CAN_Init(uint32_t baudrate);
void CAN_Init_Interrupt(uint32_t baudrate);
void Can_Write(uint8_t hth, const Can_PduType* PduInfo);


#endif