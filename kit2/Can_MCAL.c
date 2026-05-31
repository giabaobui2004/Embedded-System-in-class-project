#include "Can_MCAL.h"
#include "cmsis_os.h"
#include "System_Defines.h"

extern osMessageQId can_queue_id;

void CAN_Init(uint32_t baudrate) {
    uint32_t pclk, brp;

    // 1. Power up CAN1 peripheral
    LPC_SC->PCONP |= (1 << 13);
    
    // 2. Configure P0.0/P0.1 AND P0.21/P0.22 for CAN1 (Universal pin mapping)
    LPC_PINCON->PINSEL0 &= ~((3 << 0) | (3 << 2));
    LPC_PINCON->PINSEL0 |=  ((1 << 0) | (1 << 2));  
    
    LPC_PINCON->PINSEL1 &= ~((3 << 10) | (3 << 12));
    LPC_PINCON->PINSEL1 |=  ((3 << 10) | (3 << 12)); 

    // 3. Put CAN1 into Reset Mode
    LPC_CAN1->MOD = 1;
    LPC_CAN1->IER = 0; 
    LPC_CAN1->GSR = 0;
    
    // 4. Determine CAN clock speed
    if (((LPC_SC->PCLKSEL0 >> 26) & 0x03) == 0) {
        pclk = SystemCoreClock / 4;
    } else if (((LPC_SC->PCLKSEL0 >> 26) & 0x03) == 1) {
        pclk = SystemCoreClock;
    } else if (((LPC_SC->PCLKSEL0 >> 26) & 0x03) == 2) {
        pclk = SystemCoreClock / 2;
    } else {
        pclk = SystemCoreClock / 8;
    }

    // 5. Calculate Bit Rate Prescaler for 500kbps (16 TQ total)
    brp = pclk / (baudrate * 16);

    // 6. Set timing matching standard CAN specs
    LPC_CAN1->BTR = ((brp - 1) & 0x3FF) | (0 << 14) | (7 << 16) | (6 << 20); 
    
    // 7. Take CAN1 out of Reset mode
    LPC_CAN1->MOD = 0;

    // 8. CRITICAL: Force Acceptance Filter to BYPASS Mode
    // Without this, the hardware silently deletes all incoming packets!
    LPC_CANAF->AFMR = 2; 
		
}

void CAN_Init_Interrupt(uint32_t baudrate) {
    // Run your standard working step-by-step setup first
    CAN_Init(baudrate);

    // Enable Receive Interrupt on CAN1 hardware controller
    LPC_CAN1->IER |= (1 << 0); 
    
    // Enable CAN interrupts in the ARM Cortex-M3 core NVIC
    NVIC_EnableIRQ(CAN_IRQn);
}

// Microcontroller Hardware Interrupt Vector Vector Handler
void CAN_IRQHandler(void) {
    uint32_t rx_id = 0;
    uint32_t raw_bundle = 0; // A 32-bit container to hold all sensor data

    // Check if CAN1 generated a Receive Interrupt
    if (LPC_CAN1->GSR & (1 << 0)) { 
        rx_id = LPC_CAN1->RID;
        
        if (rx_id == 0x123) { // Your CAN_ID_SENSOR_DATA
            /// Bundle all 4 bytes into a single 32-bit register transmission word
            raw_bundle = LPC_CAN1->RDA; 
            osMessagePut(can_queue_id, raw_bundle, 0);
        }

        // Release the hardware receive buffer instantly back to the network line
        LPC_CAN1->CMR = (1 << 2); 
    }
}

void Can_Write(uint8_t hth, const Can_PduType* PduInfo) {
    // Wait until Transmit Buffer 1 is completely free
    while (!(LPC_CAN1->SR & (1 << 2)));
    
    // Set ID and data length code (DLC) from PDU structure
    LPC_CAN1->TFI1 = (PduInfo->length << 16);
    LPC_CAN1->TID1 = PduInfo->id;
    
    // Pack the payload service data units into hardware registers
    LPC_CAN1->TDA1 = (PduInfo->sdu[3] << 24) | 
                     (PduInfo->sdu[2] << 16) | 
                     (PduInfo->sdu[1] << 8)  | 
                     PduInfo->sdu[0];
    
    // Command Transmit Buffer 1 to send frame instantly
    LPC_CAN1->CMR = (1 << 0) | (1 << 5); 
}

uint8_t CAN_Receive(uint32_t* id, uint8_t* data) {
    // Check if Receive Buffer has data available
    if (LPC_CAN1->GSR & (1 << 0)) {
        // Read Frame Information and ID
        *id = LPC_CAN1->RID;
        
        // Extract 8 bytes of data out of the hardware registers
        uint32_t rda = LPC_CAN1->RDA;
        uint32_t rdb = LPC_CAN1->RDB;
        
        data[0] = (rda >> 0)  & 0xFF;
        data[1] = (rda >> 8)  & 0xFF;
        data[2] = (rda >> 16) & 0xFF;
        data[3] = (rda >> 24) & 0xFF;
        data[4] = (rdb >> 0)  & 0xFF;
        data[5] = (rdb >> 8)  & 0xFF;
        data[6] = (rdb >> 16) & 0xFF;
        data[7] = (rdb >> 24) & 0xFF;
        
        // Release receive buffer
        LPC_CAN1->CMR = (1 << 2);
        return 1; // Message received successfully
    }
    return 0; // No message available
}
