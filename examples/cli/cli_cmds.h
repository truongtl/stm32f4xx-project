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
} rtc_time_t;

void cli_register_default_table(void);
