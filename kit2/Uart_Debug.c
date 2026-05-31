#include "Uart_Debug.h"

void UART0_Init(uint32_t baudrate) {
    uint32_t pclk, dl;

    // 1. Power up UART0 peripheral
    LPC_SC->PCONP |= (1 << 3);

    // 2. Configure P0.2 as TXD0 and P0.3 as RXD0
    LPC_PINCON->PINSEL0 &= ~((3 << 4) | (3 << 6));
    LPC_PINCON->PINSEL0 |=  ((1 << 4) | (1 << 6));

    // 3. Set standard 8N1 frame format and enable DLAB to set baud rate
    LPC_UART0->LCR = 0x83; 

    // 4. Calculate baud rate registers assuming 25MHz Peripheral Clock (PCLK)
    pclk = 25000000;
    dl = pclk / (16 * baudrate);
    LPC_UART0->DLM = (dl >> 8) & 0xFF;
    LPC_UART0->DLL = dl & 0xFF;

    // 5. Clear DLAB bit to lock baud rate
    LPC_UART0->LCR = 0x03;
    LPC_UART0->FCR = 0x07; // Enable and flush FIFOs
}

void UART0_SendString(char* str) {
    while (*str) {
        // Wait until TX FIFO Transmit Holding Register is completely empty
        while (!(LPC_UART0->LSR & (1 << 5)));
        LPC_UART0->THR = *str++;
    }
}

void UART_SendChar(char ch) {
    while (!(LPC_UART0->LSR & (1 << 5))); // Wait until Transmit holding register is empty
    LPC_UART0->THR = ch;
}