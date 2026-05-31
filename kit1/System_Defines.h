#ifndef SYSTEM_DEFINES_H
#define SYSTEM_DEFINES_H

#include <stdint.h>

/* CAN Message IDs */
#define CAN_ID_SENSOR_DATA   0x101  // Kit 1 -> Kit 2 (Sensor values)
#define CAN_ID_CONFIG_CMD    0x201  // Kit 2 -> Kit 1 (New Base values/Modes)
#define CAN_ID_LED_CTRL      0x301  // Kit 2 -> Kit 1 (LED On/Off command)

/* Operation Modes */
typedef enum {
    MODE_THRESHOLD = 0,
    MODE_TIMER     = 1,
    MODE_AUTO      = 2,
    MODE_SAFE      = 3
} OperationMode_t;

/* Sensor Data Structure */
typedef struct {
    uint16_t current_value;
    uint16_t base_value;
    uint32_t active_timer;
    uint8_t  mode;
    uint8_t  alarm_status; // 0: OK, 1: Triggered
} SensorUnit_t;

#endif