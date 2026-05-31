/* Kit 1 - main.c (Dynamic Alarm Frenzy, Hardware Scan Sequencing & Timer Shutdown) */
#include "LPC17xx.h"
#include "cmsis_os.h"
#include "System_Defines.h"
#include "Can_MCAL.h"
#include "Board_GLCD.h"
#include "Adc_Driver.h"
#include <stdio.h>

extern GLCD_FONT GLCD_Font_16x24;

// Forward function declarations to eliminate Keil Implicit Warnings
void Sensor_TxTask(void const *argument);
void Command_RxTask(void const *argument);
void Update_Kit1_Hardware_LEDs(uint8_t mask);
uint8_t ADC_Read_Temp(void);
uint8_t ADC_Read_Humid(void);

volatile uint8_t rx_sensor_control_mask = 0x03; 
osMessageQId can_queue_id;
osMessageQDef(can_queue, 16, uint32_t); 

osThreadDef(Sensor_TxTask, osPriorityNormal, 1, 0);
osThreadDef(Command_RxTask, osPriorityHigh, 1, 0);

#define STATE_SAFE          0
#define STATE_ALARM         1
#define STATE_SYS_OFF       2
#define STATE_DISCONNECT    3
#define STATE_TIMER_EXPIRED 4

// ==========================================
// KIT 1 SCHEDULER CLOCK TRACKING REGISTERS
// ==========================================
volatile int32_t kit1_s1_countdown = -1;  // -1 means timer is uninitialized/idle
volatile uint8_t timer_mode_active = 0;   // 1 means actively ticking down

/* Helper Arrays to simplify the Scanner/Waterfall sequences across mixed GPIO ports */
const uint8_t PortMap[8] = {1, 1, 1, 2, 2, 2, 2, 2};
const uint8_t PinMap[8]  = {28, 29, 31, 2, 3, 4, 5, 6};

void Set_All_LEDs(uint8_t state) {
    int i;
    for (i = 0; i < 8; i++) {
        if (state) {
            if (PortMap[i] == 1) LPC_GPIO1->FIOSET = (1UL << PinMap[i]);
            else                 LPC_GPIO2->FIOSET = (1UL << PinMap[i]);
        } else {
            if (PortMap[i] == 1) LPC_GPIO1->FIOCLR = (1UL << PinMap[i]);
            else                 LPC_GPIO2->FIOCLR = (1UL << PinMap[i]);
        }
    }
}

// Controls specific hardware status LEDs based on the mask received from Kit 2
void Update_Kit1_Hardware_LEDs(uint8_t mask) {
    // Modify this if you want specific LEDs on Kit 1 to drop when a channel is killed
    if (mask & 0x01) LPC_GPIO1->FIOSET = (1UL << 28); else LPC_GPIO1->FIOCLR = (1UL << 28);
    if (mask & 0x02) LPC_GPIO1->FIOSET = (1UL << 29); else LPC_GPIO1->FIOCLR = (1UL << 29);
}

// ==========================================
// COMMAND RECEIVE TASK (INTERCEPT BUS PACKETS)
// ==========================================
void Command_RxTask(void const *argument) {
    osEvent event;
    CAN_Init_Interrupt(500000); 
    
    static uint8_t localized_h = 0;
    static uint8_t localized_m = 0;
    static uint8_t localized_s = 0;
    
    for(;;) {
        event = osMessageGet(can_queue_id, osWaitForever);
        
        if (event.status == osEventMessage) {
            uint32_t bundle = event.value.v;
            
            // Extract using 16 bits to match our new hardware driver package layout
            uint16_t extracted_id = (bundle >> 16) & 0xFFFF; 
            
            if (extracted_id == 0x124) {
                rx_sensor_control_mask = (bundle & 0xFF); // ERROR FIX: Removed broken duplicate line
            } 
            else if (extracted_id == 0x125) {
                localized_h = (bundle >> 0) & 0xFF;
                localized_m = (bundle >> 8) & 0xFF;
            }
            else if (extracted_id == 0x999) {
                localized_s = (bundle & 0xFF);
                
                kit1_s1_countdown = (localized_h * 3600) + (localized_m * 60) + localized_s;
                timer_mode_active = (kit1_s1_countdown > 0) ? 1 : 0;
            }
        }
    }
}

// ==========================================
// SENSOR TRANSMIT & TELEMETRY HARDWARE TASK
// ==========================================
void Sensor_TxTask(void const *argument) {
    uint8_t sensor_payload[4] = {0};
    uint16_t raw_adc = 0;
    uint8_t temp = 0;
    uint8_t humid = 0;
    uint8_t current_status = STATE_SAFE;
    uint8_t last_status = 0xFF; 
    
    uint8_t flash_toggle = 0;
    int16_t scan_index = 0;
    int8_t scan_direction = 1; // 1 = Forward, -1 = Reverse
    uint32_t variable_delay = 200;
    char lcd_buffer[32];
    
    // Tracking cadence clock timing register
    uint32_t ms_tick_accumulator = 0;

    Can_PduType tx_pdu;
    tx_pdu.id = 0x123;
    tx_pdu.length = 4; 
    tx_pdu.sdu = sensor_payload;

    // Direct configuration of execution registers as explicit outputs
    LPC_GPIO1->FIODIR |= (1UL << 28) | (1UL << 29) | (1UL << 31);
    LPC_GPIO2->FIODIR |= (1UL << 2) | (1UL << 3)  | (1UL << 4)  | (1UL << 5) | (1UL << 6);

    ADC_Init_Hardware();
    GLCD_Initialize();
    GLCD_SetFont(&GLCD_Font_16x24);

    for(;;) {
        // Sync hardware profile state indicator lights
        Update_Kit1_Hardware_LEDs(rx_sensor_control_mask);

        // ==========================================
        // 1. ENGINE TIMER CADENCE TICK DOWN (Run this first!)
        // ==========================================
        if (timer_mode_active && kit1_s1_countdown > 0) {
            ms_tick_accumulator += variable_delay;
            if (ms_tick_accumulator >= 1000) {
                ms_tick_accumulator = 0;
                kit1_s1_countdown--;
            }
        }
        
        // Catch the exact moment the timer expires
        if (timer_mode_active && kit1_s1_countdown <= 0) {
            kit1_s1_countdown = -1;
            timer_mode_active = 0;
            current_status = STATE_TIMER_EXPIRED; 
        }

        // ==========================================
        // 2. HARDWARE ENVIRONMENT EVALUATION
        // ==========================================
        if (current_status == STATE_TIMER_EXPIRED) {
            temp = 0; 
            humid = 0;
        } 
        else {
            raw_adc = ADC_Read_Knob();

            if (raw_adc <= 3) {
                current_status = STATE_DISCONNECT;
                temp = 0; humid = 0;
            } else if (raw_adc >= 4090) {
                current_status = STATE_SYS_OFF;
                temp = 0; humid = 0;
            } else {
                // WARNING FIX: Tied conditional mask checking directly to real execution variables
                if (rx_sensor_control_mask & 0x01) {
                    temp = 20 + ((raw_adc * 30) / 4095);
                } else {
                    temp = 0; // Inactive
                }

                if (rx_sensor_control_mask & 0x02) {
                    humid = 30 + ((raw_adc * 60) / 4095);
                } else {
                    humid = 0; // Inactive
                }
                
                current_status = (temp > 38) ? STATE_ALARM : STATE_SAFE;
            }
        }

        // Display Color Updates
        if (current_status != last_status) {
            if (current_status == STATE_DISCONNECT) {
                GLCD_SetBackgroundColor(0x0000); 
                GLCD_ClearScreen();
            } else {
                if (last_status == STATE_DISCONNECT) GLCD_Initialize();
                if (current_status == STATE_SAFE)    GLCD_SetBackgroundColor(0x07E0); // Green
                if (current_status == STATE_ALARM)   GLCD_SetBackgroundColor(0xF800); // Red
                if (current_status == STATE_SYS_OFF) GLCD_SetBackgroundColor(0xFEE0); // Yellow
                if (current_status == STATE_TIMER_EXPIRED) GLCD_SetBackgroundColor(0xFEE0);
                GLCD_SetForegroundColor(0x0000); 
                GLCD_ClearScreen();
                GLCD_DrawString(0, 0 * 24, "=== DIAGS K1 ===");
            }
            last_status = current_status;
        }

        // ==========================================
        // 3. ADVANCED HARDWARE LED LOGIC LAYER
        // ==========================================
        if (current_status == STATE_DISCONNECT) {
            Set_All_LEDs(0); 
            variable_delay = 200;
        } 
        else if (current_status == STATE_SAFE) {
            Set_All_LEDs(1); 
            variable_delay = 200;
        } 
        else if (current_status == STATE_ALARM) {
            if (temp >= 50) temp = 50;
            variable_delay = 200 - ((temp - 38) * 13);
            if (variable_delay < 30) variable_delay = 30; 

            flash_toggle = !flash_toggle;
            Set_All_LEDs(flash_toggle);
        } 
        else if (current_status == STATE_SYS_OFF || current_status == STATE_TIMER_EXPIRED) {
            // Knight-Rider / Scanner Mode Logic Sequence
            Set_All_LEDs(0); 
            
            if (PortMap[scan_index] == 1) LPC_GPIO1->FIOSET = (1UL << PinMap[scan_index]);
            else                          LPC_GPIO2->FIOSET = (1UL << PinMap[scan_index]);
            
            scan_index += scan_direction;
            if (scan_index >= 6) { scan_index = 6; scan_direction = -1; }
            else if (scan_index <= 0) { scan_index = 0; scan_direction = 1; }
            variable_delay = 90; 
        }

        // Packet Bus Assembly
        sensor_payload[0] = temp;
        sensor_payload[1] = humid;
        sensor_payload[2] = (current_status == STATE_ALARM) ? 1 : 0;
        sensor_payload[3] = current_status; 

        Can_Write(0, &tx_pdu); 

        if (current_status != STATE_DISCONNECT) {
            sprintf(lcd_buffer, "Knob ADC: %-4u ", raw_adc);
            GLCD_DrawString(0, 2 * 24, lcd_buffer);
            sprintf(lcd_buffer, "Temp: %u C Hum: %u%% ", sensor_payload[0], sensor_payload[1]);
            GLCD_DrawString(0, 4 * 24, lcd_buffer);
            
            if (current_status == STATE_SAFE)          GLCD_DrawString(0, 6 * 24, "STATUS: SAFE    ");
            if (current_status == STATE_ALARM)         GLCD_DrawString(0, 6 * 24, "STATUS: ALARM   ");
            if (current_status == STATE_SYS_OFF)       GLCD_DrawString(0, 6 * 24, "STATUS: SYS OFF ");
            if (current_status == STATE_TIMER_EXPIRED) GLCD_DrawString(0, 6 * 24, "TIME EXPIRED!   "); 
        }

        osDelay(variable_delay); 
    }
}

int main(void) {
    SystemInit();
    osKernelInitialize();
    can_queue_id = osMessageCreate(osMessageQ(can_queue), NULL);
    osThreadCreate(osThread(Sensor_TxTask), NULL);
    osThreadCreate(osThread(Command_RxTask), NULL);
    osKernelStart();
    while(1);
}