/* Kit 2 - main.c (Safe Non-Blocking Background Network Engine) */
#include "LPC17xx.h"
#include "cmsis_os.h"
#include "Can_MCAL.h"
#include "UI_Control.h" 
#include <stdio.h> // <-- ADDED: Fixes implicit declaration of sprintf

// Global memory allocations
volatile uint8_t current_temperature = 0;
volatile uint8_t current_humidity = 0;
volatile uint8_t kit1_ac_feedback = 0;
volatile uint8_t kit1_status_code = 0;

// Inter-thread trigger handshake variable
volatile uint8_t transmit_timer_request = 0; 

// References to countdown structures matching UI tracking
extern volatile int32_t s1_countdown_seconds;
extern volatile int32_t s2_countdown_seconds;
extern int16_t s1_h, s1_m, s1_s; 

// ==========================================
// ADDED: UI EXTENSIONS TO EMIT TO CAN BUS
// ==========================================
extern uint8_t ac_mode;
extern uint8_t sensor_control_mask;

// Explicit declaration of your UART function from Uart_Debug.c
extern void UART0_SendString(const char* str);
extern void UART0_Init(uint32_t baudrate);

// OS Message Queue Configurations
osMessageQId can_queue_id;
osMessageQDef(can_queue, 16, uint32_t); 

void Event_RxTask(void const *argument);
osThreadDef(Event_RxTask, osPriorityHigh, 1, 0); 

// ==========================================
// Background Asynchronous Communications Task
// ==========================================
void Event_RxTask(void const *argument) {
    osEvent event;
    CAN_Init_Interrupt(500000); 

    // Tracker states to avoid spamming the serial terminal window
    static uint8_t last_printed_status = 0xFF;
    static int32_t last_printed_time = -2;
    char uart_buffer[100];

    for(;;) {
        // ==========================================================
        // 1. ASYNCHRONOUS CHECK: Send Sensor Mask (0x124) or Timer (0x125)
        // ==========================================================
        if (transmit_timer_request == 1) {
            transmit_timer_request = 0; // Reset handshake flag instantly
            
            Can_PduType tx_pdu;
            uint8_t command_payload[4] = {0};
            
            // If UI is in MAN_ON Mode, send Sensor Mask Configurations
            if (ac_mode == 1) {
                tx_pdu.id = 0x124;
                tx_pdu.length = 4;
                tx_pdu.sdu = command_payload;
                
                command_payload[0] = sensor_control_mask;
                command_payload[1] = 0x00;
                command_payload[2] = 0x00;
                command_payload[3] = 0x00;
                
                Can_Write(0, &tx_pdu); 
                UART0_SendString("\r\n[UART] CAN Command Sent: Sensor Control Mask Updated.\r\n");
            }
            // Otherwise, process standard Timer configuration sync pipeline
            else if (ac_mode == 3) {
                tx_pdu.id = 0x125;
                tx_pdu.length = 4;
                tx_pdu.sdu = command_payload;
                
                command_payload[0] = (uint8_t)s1_h;   
                command_payload[1] = (uint8_t)s1_m;   
                command_payload[2] = (uint8_t)s1_s;   
                command_payload[3] = 0x88; // Unique design signature
                
                Can_Write(0, &tx_pdu); 
                UART0_SendString("\r\n[UART] CAN Command Sent: New Timer Config Sync Loaded.\r\n");
            }
        } 

        // 2. Poll the queue with a tiny 50ms window instead of waiting forever
        event = osMessageGet(can_queue_id, 50);
        
        if (event.status == osEventMessage) {
            uint32_t raw_bundle = event.value.v;
            current_temperature = (raw_bundle >> 0)  & 0xFF;
            current_humidity    = (raw_bundle >> 8)  & 0xFF;
            kit1_ac_feedback    = (raw_bundle >> 16) & 0xFF;
            kit1_status_code    = (raw_bundle >> 24) & 0xFF; 
        }

        // ==========================================
        // 3. LIVE UART MONITORING DASHBOARD
        // ==========================================
        if (kit1_status_code != last_printed_status || s1_countdown_seconds != last_printed_time) {
            
            UART0_SendString("\r\n=================================\r\n"); // <-- ADDED '0'
            UART0_SendString("    KIT 2 REMOTE CAN MONITOR    \r\n");     // <-- ADDED '0'
            UART0_SendString("=================================\r\n");     // <-- ADDED '0'

            if (kit1_status_code == 3) {
                UART0_SendString("CAN NODE 1 : [ALERT] DISCONNECTED / WIRE DETACHED\r\n"); // '0'
            } 
            else if (current_temperature == 0 && current_humidity == 0 && kit1_status_code == 0) {
                UART0_SendString("CAN NODE 1 : [CONNECTING] Waiting for Bus Activity...\r\n"); // '0'
            } 
            else {
                UART0_SendString("CAN NODE 1 : [CONNECTED] Link Stable (ID: 0x123)\r\n"); // '0'
            }

            sprintf(uart_buffer, "Telemetry  : Temp = %u C, Humidity = %u%%\r\n", current_temperature, current_humidity);
            UART0_SendString(uart_buffer); // '0'

            if (kit1_status_code == 0)      UART0_SendString("Node Mode  : AUTOMATIC (SAFE ENGINE ACTIVE)\r\n"); // '0'
            else if (kit1_status_code == 1) UART0_SendString("Node Mode  : AUTOMATIC (ALARM CONDITION!)\r\n"); // '0'
            else if (kit1_status_code == 2) UART0_SendString("Node Mode  : MANUAL SHUTDOWN (REMOTE KNOB OVERRIDE)\r\n"); // '0'
            else if (kit1_status_code == 4) UART0_SendString("Node Mode  : TIMED SHUTDOWN (REMOTE SCHEDULE EXPIRED)\r\n"); // '0'

            if (s1_countdown_seconds > 0) {
                int16_t h = s1_countdown_seconds / 3600;
                int16_t m = (s1_countdown_seconds % 3600) / 60;
                int16_t s = s1_countdown_seconds % 60;
                sprintf(uart_buffer, "Local Clock: Active Countdown -> %02d:%02d:%02d\r\n", h, m, s);
                UART0_SendString(uart_buffer); // '0'
            } 
            else if (s1_countdown_seconds == 0) {
                UART0_SendString("Local Clock: EXPIRED (Shutdown signal active)\r\n"); // '0'
            } 
            else {
                UART0_SendString("Local Clock: STANDBY (No Timer Active)\r\n"); // '0'
            }
            
            UART0_SendString("---------------------------------\r\n"); // '0'

            last_printed_status = kit1_status_code;
            last_printed_time = s1_countdown_seconds;
        }
    }
}

osThreadDef(UI_MenuTask, osPriorityNormal, 1, 0);

int main(void) {
    SystemInit();
    osKernelInitialize();
    UART0_Init(115200);
    can_queue_id = osMessageCreate(osMessageQ(can_queue), NULL);
    
    osThreadCreate(osThread(Event_RxTask), NULL);
    osThreadCreate(osThread(UI_MenuTask), NULL);
    
    osKernelStart();
    while(1);
}