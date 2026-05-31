#include "UI_Control.h"
#include "Board_GLCD.h"
#include "Joystick_Driver.h"
#include "Can_MCAL.h"
#include "cmsis_os.h"
#include <stdio.h>

// Shared external variables defined by your original working main.c
extern volatile uint8_t current_temperature;
extern volatile uint8_t current_humidity;
extern volatile uint8_t kit1_ac_feedback;
extern volatile uint8_t kit1_status_code;

extern GLCD_FONT GLCD_Font_16x24;
static uint8_t selected_row = 0;      // 0=AC Mode, 1=Monitor, 2=Sub-UI Row
uint8_t ac_mode = 0;           // 0=Auto, 1=Man_On, 2=Man_Off, 3=Timer Mode
static uint8_t display_focus = 0;     // 0=Temp, 1=Humidity

// Sub-UI Management variables
static uint8_t in_sub_ui = 0;          // 0=Main, 1=Manual Config, 2=Auto Diags, 3=Timer Config
static uint8_t sub_ui_row = 0;         // 0=S1 Time, 1=S2 Time, 2=Step Size, 3=Save/Exit
static uint8_t is_editing = 0;         // 0=Navigating Rows, 1=Editing Values Inside Row
static uint8_t time_field_focus = 0;   // 0=Hours, 1=Minutes, 2=Seconds

// Scheduled Configuration Registers
int16_t s1_h = 0, s1_m = 0, s1_s = 0;        // REMOVED 'static' -> Visible to main.c!
static int16_t s2_h = 0, s2_m = 0, s2_s = 0; // Can leave s2 as static until we use it later
static uint8_t time_step_value = 1;

// Local Live Countdown Registers (Safely contained inside this file)
int32_t s1_countdown_seconds = -1; // -1 means timer is disabled/idle
static int32_t s2_countdown_seconds = -1;
static uint32_t ms_tick_accumulator = 0;   // Tracks time passes without an extra thread

 uint8_t sensor_control_mask = 0x03; 

void Update_Kit2_Hardware_LEDs(uint8_t mask) {
    LPC_GPIO1->FIODIR |= (1 << 28) | (1 << 29);
    if (mask & 0x01) LPC_GPIO1->FIOSET = (1 << 28); else LPC_GPIO1->FIOCLR = (1 << 28);
    if (mask & 0x02) LPC_GPIO1->FIOSET = (1 << 29); else LPC_GPIO1->FIOCLR = (1 << 29);
}

void UI_MenuTask(void const *argument) {
    uint32_t joy_state = 0;
    char display_buffer[24]; 
    uint8_t need_refresh = 1;
    
    uint8_t last_selected_row = 0xFF; uint8_t last_ac_mode = 0xFF;
    uint8_t last_display_focus = 0xFF; uint8_t last_printed_value = 0xFF;
    uint8_t last_alarm_ui_state = 0xFF;

    Joystick_Init();
    GLCD_Initialize();
    GLCD_SetFont(&GLCD_Font_16x24);
    Update_Kit2_Hardware_LEDs(sensor_control_mask);

    for(;;) {
        joy_state = Joystick_GetState();

			

        // ==========================================
    // 1. LOCAL TIMER TICK ENGINE (Runs on a sharp 30ms base)
    // ==========================================
    ms_tick_accumulator += 130;
    
    // Create a flag to only update the screen when a second actually changes
    uint8_t second_changed_flag = 0; 
    
    if (ms_tick_accumulator >= 1000) { 
        ms_tick_accumulator = 0;
        if (s1_countdown_seconds > 0) {
            s1_countdown_seconds--;
            second_changed_flag = 1; // Signal a display refresh!
        }
        if (s2_countdown_seconds > 0) {
            s2_countdown_seconds--;
        }
        
        // Handle the exact moment it hits 0
        if (s1_countdown_seconds == 0) second_changed_flag = 1;
    }

        // ==========================================
        // LAYER A: TIMER CONFIGURATION SUB-UI PARSER
        // ==========================================
        if (in_sub_ui == 3) {
            if (is_editing) {
                if (joy_state & JOY_LEFT) {
                    if (time_field_focus > 0) time_field_focus--;
                    osDelay(200);
                }
                else if (joy_state & JOY_RIGHT) {
                    if (time_field_focus < 2) time_field_focus++;
                    osDelay(200);
                }
                else if (joy_state & JOY_UP) {
                    if (sub_ui_row == 0) { 
                        if (time_field_focus == 0) s1_h = (s1_h + time_step_value) % 24;
                        if (time_field_focus == 1) s1_m = (s1_m + time_step_value) % 60;
                        if (time_field_focus == 2) s1_s = (s1_s + time_step_value) % 60;
                    } else if (sub_ui_row == 1) { 
                        if (time_field_focus == 0) s2_h = (s2_h + time_step_value) % 24;
                        if (time_field_focus == 1) s2_m = (s2_m + time_step_value) % 60;
                        if (time_field_focus == 2) s2_s = (s2_s + time_step_value) % 60;
                    }
                    osDelay(150);
                }
                else if (joy_state & JOY_DOWN) {
                    if (sub_ui_row == 0) {
                        if (time_field_focus == 0) s1_h = (s1_h - time_step_value < 0) ? 23 : s1_h - time_step_value;
                        if (time_field_focus == 1) s1_m = (s1_m - time_step_value < 0) ? 59 : s1_m - time_step_value;
                        if (time_field_focus == 2) s1_s = (s1_s - time_step_value < 0) ? 59 : s1_s - time_step_value;
                    } else if (sub_ui_row == 1) {
                        if (time_field_focus == 0) s2_h = (s2_h - time_step_value < 0) ? 23 : s2_h - time_step_value;
                        if (time_field_focus == 1) s2_m = (s2_m - time_step_value < 0) ? 59 : s2_m - time_step_value;
                        if (time_field_focus == 2) s2_s = (s2_s - time_step_value < 0) ? 59 : s2_s - time_step_value;
                    }
                    osDelay(150);
                }
                else if (joy_state & JOY_CENTER) {
                    is_editing = 0; 
                    need_refresh = 1; 
                    osDelay(250);
                }
            } 
            else {
                if (joy_state & JOY_UP)   { if (sub_ui_row > 0) sub_ui_row--; osDelay(200); }
                if (joy_state & JOY_DOWN) { if (sub_ui_row < 3) sub_ui_row++; osDelay(200); }
                
                if (sub_ui_row == 2) {
                    if (joy_state & JOY_RIGHT) { if(time_step_value < 10) time_step_value++; osDelay(200); }
                    if (joy_state & JOY_LEFT)  { if(time_step_value > 1)  time_step_value--; osDelay(200); }
                }

                if (joy_state & JOY_CENTER) {
                    if (sub_ui_row == 0 || sub_ui_row == 1) {
                        is_editing = 1; 
                        time_field_focus = 0;
                        need_refresh = 1;
                    } 
                    else if (sub_ui_row == 3) {
                        // 1. Calculate absolute tracking seconds from menu setup
                        s1_countdown_seconds = (s1_h * 3600) + (s1_m * 60) + s1_s;
                        s2_countdown_seconds = (s2_h * 3600) + (s2_m * 60) + s2_s;

                        if (s1_countdown_seconds == 0) s1_countdown_seconds = -1;
                        if (s2_countdown_seconds == 0) s2_countdown_seconds = -1;

                        // 2. SAFE INTER-THREAD TRIGGER: 
                        // Instead of running blocking hardware code here, we tell main.c to handle it.
                        extern volatile uint8_t transmit_timer_request;
                        transmit_timer_request = 1; 

                        in_sub_ui = 0; selected_row = 0; need_refresh = 1;
                    }
                    osDelay(250);
                }
            }
        }
				// ==========================================================
        // FIXED: LAYER B INPUT PARSER (NO ACCIDENTAL SCREEN WIPING)
        // ==========================================================
        else if (in_sub_ui == 1) {
            // Joystick UP/DOWN: Move rows safely WITHOUT setting need_refresh = 1
            if (joy_state & JOY_UP) { 
                if (sub_ui_row > 0) {
                    sub_ui_row--; 
                    // REMOVED: need_refresh = 1; <- This was causing the wipe!
                }
                osDelay(200); 
            }
            if (joy_state & JOY_DOWN) { 
                if (sub_ui_row < 1) {
                    sub_ui_row++; 
                    // REMOVED: need_refresh = 1; <- This was causing the wipe!
                }
                osDelay(200); 
            }
            
            // Joystick LEFT: Toggle independent switches smoothly
            if (joy_state & JOY_LEFT) {
                if (sub_ui_row == 0) {
                    sensor_control_mask ^= 0x01; // Toggle Bit 0 (TEMP)
                }
                else if (sub_ui_row == 1) {
                    sensor_control_mask ^= 0x02; // Toggle Bit 1 (HUMID)
                }
                
                // Sync Kit 2 local hardware LEDs instantly
                if (sensor_control_mask & 0x01) LPC_GPIO1->FIOSET = (1UL << 28); else LPC_GPIO1->FIOCLR = (1UL << 28);
                if (sensor_control_mask & 0x02) LPC_GPIO1->FIOSET = (1UL << 29); else LPC_GPIO1->FIOCLR = (1UL << 29);
                
                // REMOVED: need_refresh = 1; <- Dynamic values overwrite perfectly without wiping
                osDelay(250); // Anti-bounce delay
            }
            
            // Joystick CENTER: Save data and exit
            if (joy_state & JOY_CENTER) {
                extern volatile uint8_t transmit_timer_request;
                transmit_timer_request = 1; 
                
                in_sub_ui = 0;     // Return to Main Menu
                need_refresh = 1;  // Clear screen canvas ONCE for main menu layout change
                osDelay(250);
            }
        }
        // ==========================================
        // LAYER C: MAIN HOME PANEL CONTROLS
        // ==========================================
        else {
            if (joy_state & JOY_UP)   { if (selected_row > 0) selected_row--; osDelay(200); }
            if (joy_state & JOY_DOWN) {
                if (selected_row == 1 && (ac_mode == 1 || ac_mode == 3)) selected_row = 2; 
                else if (selected_row < 1) selected_row++;
                osDelay(200);
            }
            if (joy_state & JOY_RIGHT) {
                if (selected_row == 0) ac_mode = (ac_mode + 1) % 4; 
                else if (selected_row == 1) display_focus = (display_focus == 0) ? 1 : 0;
                osDelay(200);
            }
            if (joy_state & JOY_LEFT) {
                if (selected_row == 0) ac_mode = (ac_mode == 0) ? 3 : ac_mode - 1;
                else if (selected_row == 1) display_focus = (display_focus == 0) ? 1 : 0;
                osDelay(200);
            }
            if (joy_state & JOY_CENTER) {
                if (selected_row == 0 && ac_mode == 0) { in_sub_ui = 2; need_refresh = 1; }
                else if (selected_row == 2 && ac_mode == 1) { in_sub_ui = 1; need_refresh = 1; }
                else if (selected_row == 2 && ac_mode == 3) { in_sub_ui = 3; need_refresh = 1; } 
                osDelay(250);
            }
        }

        // ==========================================
        // RENDER GENERATOR: DYNAMIC TIMER SUB-UI DRAW
        // ==========================================
        if (in_sub_ui == 3) {
            if (need_refresh) {
                GLCD_SetBackgroundColor(0x0010); 
                GLCD_SetForegroundColor(0xFFFF); GLCD_ClearScreen();
                GLCD_DrawString(0, 0 * 24, " - TIMER SETUP - ");
                GLCD_DrawString(0, 7 * 24, "C:Edit/Save | L/R");
                need_refresh = 0;
            }

            sprintf(display_buffer, "%sS1:   %02d:%02d:%02d", (sub_ui_row == 0) ? ">" : " ", s1_h, s1_m, s1_s);
            GLCD_DrawString(0, 2 * 24, display_buffer);

            sprintf(display_buffer, "%sS2:   %02d:%02d:%02d", (sub_ui_row == 1) ? ">" : " ", s2_h, s2_m, s2_s);
            GLCD_DrawString(0, 3 * 24, display_buffer);

            sprintf(display_buffer, "%sStep: <  %d  >", (sub_ui_row == 2) ? ">" : " ", time_step_value);
            GLCD_DrawString(0, 4 * 24, display_buffer);

            sprintf(display_buffer, "%s[SAVE & EXIT]", (sub_ui_row == 3) ? ">" : " ");
            GLCD_DrawString(0, 5 * 24, display_buffer);

            if (is_editing) {
                GLCD_SetForegroundColor(0xF800); 
                uint8_t field_char_offset = 7 + (time_field_focus * 3);
                GLCD_DrawString(field_char_offset * 16, (sub_ui_row == 0 ? 2 : 3) * 24, "^^");
                GLCD_SetForegroundColor(0xFFFF);
            } else {
                GLCD_DrawString(7 * 16, 2 * 24, "        ");
                GLCD_DrawString(7 * 16, 3 * 24, "        ");
            }
        }
				// ==========================================================
        // OPTIMIZED RENDER GENERATOR: STATIC CANVAS & LOCAL REFRESHEE
        // ==========================================================
        else if (in_sub_ui == 1) {
            // 1. STATIC BACKGROUND LAYER: Run ONLY when entering the menu
            if (need_refresh) {
                GLCD_SetBackgroundColor(0x001F); // Dark blue canvas palette
                GLCD_SetForegroundColor(0xFFFF); 
                GLCD_ClearScreen(); // This is now the ONLY place we wipe the screen
                
                GLCD_DrawString(0, 0 * 24, " - REMOTE SETUP -");
                GLCD_DrawString(0, 7 * 24, "Left: Toggle | C: Save"); 
                
                // Print the text components that never move
                GLCD_DrawString(2 * 16, 2 * 24, "1.TEMP SENSOR:");
                GLCD_DrawString(2 * 16, 3 * 24, "2.HUMID SENSOR:");
                
                need_refresh = 0; // Lock the static background
            }

            // 2. DYNAMIC LAYER: Redraw row cursors smoothly without clearing screen
            GLCD_SetForegroundColor(0xFFFF);
            GLCD_DrawString(0, 2 * 24, (sub_ui_row == 0) ? "> " : "  ");
            GLCD_DrawString(0, 3 * 24, (sub_ui_row == 1) ? "> " : "  ");

            // 3. VARIABLE SWITCH LAYER: Update just the ON/OFF text coordinates
            // Row 1: Temperature Status Block
            GLCD_SetForegroundColor((sensor_control_mask & 0x01) ? 0x07E0 : 0xF800); // Green vs Red
            GLCD_DrawString(18 * 16, 2 * 24, (sensor_control_mask & 0x01) ? "ON " : "OFF");

            // Row 2: Humidity Status Block
            GLCD_SetForegroundColor((sensor_control_mask & 0x02) ? 0x07E0 : 0xF800); // Green vs Red
            GLCD_DrawString(18 * 16, 3 * 24, (sensor_control_mask & 0x02) ? "ON " : "OFF");
        }
				

        // ==========================================
        // HOME SCREEN RENDER LAYOUT WITH countdowns
        // ==========================================
        else if (in_sub_ui == 0) {
            if (need_refresh) {
                GLCD_SetBackgroundColor(0x0000); GLCD_SetForegroundColor(0xFFFF); GLCD_ClearScreen();
                GLCD_DrawString(0, 0 * 24, "=== CONTROL PANEL ===");
                GLCD_DrawString(0, 7 * 24, "Up/Dn: Move | L/R: Cycle");
                need_refresh = 0; last_selected_row = 0xFF; last_ac_mode = 0xFF;
                last_display_focus = 0xFF; last_printed_value = 0xFF; last_alarm_ui_state = 0xFF;
            }

            if (selected_row != last_selected_row) {
                GLCD_SetForegroundColor(0xFFFF);
                GLCD_DrawString(0, 2 * 24, (selected_row == 0) ? "> AC MODE:" : "  AC MODE:");
                GLCD_DrawString(0, 3 * 24, (selected_row == 1) ? "> MONITOR:" : "  MONITOR:");
                
                if (ac_mode == 1)      GLCD_DrawString(0, 4 * 24, (selected_row == 2) ? "> SETUP REMOTE SENS" : "  SETUP REMOTE SENS");
                else if (ac_mode == 3) GLCD_DrawString(0, 4 * 24, (selected_row == 2) ? "> ADJUST SCHEDULER " : "  ADJUST SCHEDULER ");
                else                   GLCD_DrawString(0, 4 * 24, "                   ");
                last_selected_row = selected_row;
            }

            if (ac_mode != last_ac_mode) {
                GLCD_SetForegroundColor(0x07E0); 
                if (ac_mode == 0)      GLCD_DrawString(11 * 16, 2 * 24, "[AUTO]   ");
                else if (ac_mode == 1) GLCD_DrawString(11 * 16, 2 * 24, "[MAN_ON] ");
                else if (ac_mode == 2) GLCD_DrawString(11 * 16, 2 * 24, "[MAN_OFF]");
                else if (ac_mode == 3) GLCD_DrawString(11 * 16, 2 * 24, "[TIMER]  ");
                last_selected_row = 0xFF; last_ac_mode = ac_mode;
            }

            if (display_focus != last_display_focus) {
                GLCD_SetForegroundColor(0x07E0);
                if (display_focus == 0) GLCD_DrawString(11 * 16, 3 * 24, "[TEMP]  ");
                else                    GLCD_DrawString(11 * 16, 3 * 24, "[HUMID] ");
                last_display_focus = display_focus; last_printed_value = 0xFF; 
            }

            uint8_t current_active_value = (display_focus == 0) ? current_temperature : current_humidity;
            uint8_t dynamic_alarm_active = (current_temperature > 38) ? 1 : 0;

            if ((current_active_value != last_printed_value) || (dynamic_alarm_active != last_alarm_ui_state)) {
                if (dynamic_alarm_active) GLCD_SetForegroundColor(0xF800); else GLCD_SetForegroundColor(0xFEE0);
                if (display_focus == 0) sprintf(display_buffer, "Active Temp: %-3u C %s", current_active_value, dynamic_alarm_active ? "[CRIT]" : "      ");
                else                    sprintf(display_buffer, "Active Hum : %-3u %% %s", current_active_value, dynamic_alarm_active ? "[CRIT]" : "      ");
                GLCD_DrawString(1 * 16, 5 * 24, display_buffer);
                last_printed_value = current_active_value; last_alarm_ui_state = dynamic_alarm_active;
            }

            // DYNAMIC CLOCK DISPLAY ON MAIN SCREEN LINE 6
            if (s1_countdown_seconds > 0) {
                int16_t disp_h = s1_countdown_seconds / 3600;
                int16_t disp_m = (s1_countdown_seconds % 3600) / 60;
                int16_t disp_s = s1_countdown_seconds % 60;
                sprintf(display_buffer, "Timer 1: %02d:%02d:%02d", disp_h, disp_m, disp_s);
                GLCD_SetForegroundColor(0x07E0); 
                GLCD_DrawString(1 * 16, 6 * 24, display_buffer);
            } else if (s1_countdown_seconds == 0) {
                GLCD_SetForegroundColor(0xF800); 
                GLCD_DrawString(1 * 16, 6 * 24, "Timer 1: EXPIRED!");
            } else {
                GLCD_SetForegroundColor(0x8410); 
                GLCD_DrawString(1 * 16, 6 * 24, "Timer 1: OFF     ");
            }
        }
        osDelay(30); 
    }
}