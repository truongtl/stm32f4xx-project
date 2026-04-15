#pragma once

#define LED_PORT    GPIOC
#define LED_PIN     GPIO_PIN_13

typedef struct {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} sys_time_t;

void cli_register_default_table(void);

/* Board abstraction — implemented in cli_cmd_handle.c */
void        board_led_write(int led_state);
sys_time_t  board_gettime(void);
void        board_settime(sys_time_t t);
void        board_reboot(void);
