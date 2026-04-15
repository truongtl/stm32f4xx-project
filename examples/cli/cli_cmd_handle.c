#include "stm32f4xx_hal.h"
#include "cli_cmds.h"

static volatile sys_time_t sys_time = { .sec=0, .min=0, .hour=0, .day=1, .month=1, .year=2025 };
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

/**
 * @brief SysTick user callback that updates the system time.
 *
 * @why Provides a millisecond-resolution clock for CLI time commands
 *      by incrementing time on each SysTick interrupt.
 */
void HAL_SysTick_UserCallback(void)
{
    sys_tick_update();
}

/**
 * @brief Controls the board LED state.
 *
 * @why Allows CLI commands to visually indicate status or provide user feedback.
 *
 * @param led_state 1 to turn LED on, 0 to turn off.
 */
void board_led_write(int led_state)
{
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, (GPIO_PinState)led_state);
}

/**
 * @brief Retrieves the current system time.
 *
 * Uses interrupt-safe atomic read to prevent corruption during updates.
 *
 * @why Provides the current time for CLI display and time-based operations.
 *
 * @return Current system time structure.
 */
sys_time_t board_gettime(void)
{
    sys_time_t snap;
    __disable_irq();
    snap = sys_time;
    __enable_irq();
    return snap;
}

/**
 * @brief Sets the system time to a new value.
 *
 * Uses interrupt-safe atomic write to prevent corruption during updates.
 *
 * @why Allows CLI commands to set the system clock for time synchronization.
 *
 * @param t New time value to set.
 */
void board_settime(sys_time_t t)
{
    __disable_irq();
    sys_time.year = t.year;
    sys_time.month = t.month;
    sys_time.day = t.day;
    sys_time.hour = t.hour;
    sys_time.min = t.min;
    sys_time.sec = t.sec;
    __enable_irq();
}

/**
 * @brief Performs a software reset of the board.
 *
 * @why Allows CLI commands to reboot the system for recovery or updates.
 */
void board_reboot(void)
{
    HAL_NVIC_SystemReset();
}