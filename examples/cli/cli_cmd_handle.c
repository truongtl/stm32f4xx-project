#include "stm32f4xx_hal.h"
#include "cli_cmds.h"

static volatile sys_time_t sys_time = {0};
static volatile uint16_t sys_ms = 0;
static const uint8_t days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};

static uint8_t is_leap_year(uint16_t year);
static void sys_tick_update(void);

static uint8_t is_leap_year(uint16_t year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

static void sys_tick_update(void)
{
    sys_ms++;
    if (sys_ms >= 1000)
    {
        sys_ms = 0;
        sys_time.sec++;
        if (sys_time.sec >= 60)
        {
            sys_time.sec = 0;
            sys_time.min++;
            if (sys_time.min >= 60)
            {
                sys_time.min = 0;
                sys_time.hour++;
                if (sys_time.hour >= 24)
                {
                    sys_time.hour = 0;
                    sys_time.day++;

                    uint8_t dim = days_in_month[sys_time.month - 1];
                    if ((sys_time.month == 2) && is_leap_year(sys_time.year)) dim = 29;

                    if (sys_time.day > dim)
                    {
                        sys_time.day = 1;
                        sys_time.month++;
                        if (sys_time.month > 12)
                        {
                            sys_time.month = 1;
                            sys_time.year++;
                        }
                    }
                }
            }
        }
    }
}

void HAL_SysTick_UserCallback(void)
{
    sys_tick_update();
}

void board_led_write(int led_state)
{
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, (GPIO_PinState)led_state);
}

sys_time_t board_gettime(void)
{
    return sys_time;
}

void board_settime(sys_time_t t)
{
    sys_time.year = t.year;
    sys_time.month = t.month;
    sys_time.day = t.day;
    sys_time.hour = t.hour;
    sys_time.min = t.min;
    sys_time.sec = t.sec;
}

void board_reboot(void)
{
    HAL_NVIC_SystemReset();
}