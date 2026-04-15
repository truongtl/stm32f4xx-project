#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cli_cmds.h"

static volatile sys_time_t sys_time = { .sec = 0U, .min = 0U, .hour = 0U, .day = 1U, .month = 1U, .year = 2025U };
static volatile uint16_t sys_ms = 0;
static const uint8_t days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};

static uint8_t is_leap_year(uint16_t year);
static void sys_tick_update(void);

/**
 * @brief Returns whether a year is a leap year.
 *
 * @why Used by sys_tick_update to correctly advance the day counter in February.
 *
 * @param year Four-digit year to test.
 * @return Non-zero if leap year, zero otherwise.
 */
static uint8_t is_leap_year(uint16_t year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

/**
 * @brief Advances the software real-time clock by one millisecond.
 *
 * Called every millisecond from vApplicationTickHook. Handles rollover
 * of seconds, minutes, hours, days, months, and years including leap years.
 *
 * @why Maintains an accurate software clock for CLI time commands without
 *      requiring a dedicated RTC peripheral.
 */
static void sys_tick_update(void)
{
    sys_ms++;
    if (sys_ms >= 1000)
    {
        sys_ms = 0;
        sys_time.sec = (uint8_t)(sys_time.sec + 1U);
        if (sys_time.sec >= 60U)
        {
            sys_time.sec = 0U;
            sys_time.min = (uint8_t)(sys_time.min + 1U);
            if (sys_time.min >= 60U)
            {
                sys_time.min = 0U;
                sys_time.hour = (uint8_t)(sys_time.hour + 1U);
                if (sys_time.hour >= 24U)
                {
                    sys_time.hour = 0U;
                    sys_time.day = (uint8_t)(sys_time.day + 1U);

                    uint8_t dim = days_in_month[sys_time.month - 1];
                    if ((sys_time.month == 2) && is_leap_year(sys_time.year)) dim = 29;

                    if (sys_time.day > dim)
                    {
                        sys_time.day = 1U;
                        sys_time.month = (uint8_t)(sys_time.month + 1U);
                        if (sys_time.month > 12U)
                        {
                            sys_time.month = 1U;
                            sys_time.year++;
                        }
                    }
                }
            }
        }
    }
}

/**
 * @brief FreeRTOS tick hook that advances HAL tick and the software clock.
 *
 * @why Drives both HAL_Delay/HAL_GetTick and the software RTC from the
 *      FreeRTOS scheduler tick, replacing a separate SysTick callback.
 */
void vApplicationTickHook(void)
{
    HAL_IncTick();
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
 * @brief Retrieves the current system time using a FreeRTOS critical section.
 *
 * @why Provides interrupt-safe atomic read of the shared clock for CLI
 *      commands running in a FreeRTOS task context.
 *
 * @return Current system time structure.
 */
sys_time_t board_gettime(void)
{
    taskENTER_CRITICAL();
    sys_time_t t = sys_time;
    taskEXIT_CRITICAL();
    return t;
}

/**
 * @brief Sets the system time using a FreeRTOS critical section.
 *
 * @why Provides interrupt-safe atomic write of the shared clock for CLI
 *      commands running in a FreeRTOS task context.
 *
 * @param t New time value to set.
 */
void board_settime(sys_time_t t)
{
    taskENTER_CRITICAL();
    sys_time.year  = t.year;
    sys_time.month = t.month;
    sys_time.day   = t.day;
    sys_time.hour  = t.hour;
    sys_time.min   = t.min;
    sys_time.sec   = t.sec;
    taskEXIT_CRITICAL();
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