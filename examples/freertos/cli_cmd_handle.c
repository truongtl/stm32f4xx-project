#include "stm32f4xx_hal.h"
#include "cli_cmds.h"

static volatile rtc_time_t rtc_time = {0};
static volatile uint16_t rtc_ms = 0;
static const uint8_t days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};

static uint8_t is_leap_year(uint16_t year);
static void rtc_tick_update(void);

static uint8_t is_leap_year(uint16_t year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

static void rtc_tick_update(void)
{
    rtc_ms++;
    if (rtc_ms >= 1000)
    {
        rtc_ms = 0;
        rtc_time.sec++;
        if (rtc_time.sec >= 60)
        {
            rtc_time.sec = 0;
            rtc_time.min++;
            if (rtc_time.min >= 60)
            {
                rtc_time.min = 0;
                rtc_time.hour++;
                if (rtc_time.hour >= 24)
                {
                    rtc_time.hour = 0;
                    rtc_time.day++;

                    uint8_t dim = days_in_month[rtc_time.month - 1];
                    if ((rtc_time.month == 2) && is_leap_year(rtc_time.year)) dim = 29;

                    if (rtc_time.day > dim)
                    {
                        rtc_time.day = 1;
                        rtc_time.month++;
                        if (rtc_time.month > 12)
                        {
                            rtc_time.month = 1;
                            rtc_time.year++;
                        }
                    }
                }
            }
        }
    }
}

void HAL_SysTick_UserCallback(void)
{
    rtc_tick_update();
}

void board_led_write(int led_state)
{
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, (GPIO_PinState)led_state);
}

rtc_time_t board_gettime(void)
{
    return rtc_time;
}

void board_settime(rtc_time_t t)
{
    rtc_time.year = t.year;
    rtc_time.month = t.month;
    rtc_time.day = t.day;
    rtc_time.hour = t.hour;
    rtc_time.min = t.min;
    rtc_time.sec = t.sec;
}

void board_reboot(void)
{
    HAL_NVIC_SystemReset();
}