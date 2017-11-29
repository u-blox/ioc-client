//----------------------------------------------------------------------------
// The confidential and proprietary information contained in this file may
// only be used by a person authorised under and to the extent permitted
// by a subsisting licensing agreement from ARM Limited or its affiliates.
//
// (C) COPYRIGHT 2016 ARM Limited or its affiliates.
// ALL RIGHTS RESERVED
//
// This entire notice must be reproduced on all copies of this file
// and copies of this file may only be made by a person if such person is
// permitted to do so under the terms of a subsisting license agreement
// from ARM Limited or its affiliates.
//----------------------------------------------------------------------------

#include "mbed.h"
#ifdef MBED_HEAP_STATS_ENABLED
#include "mbed_stats.h"
#endif
#if defined(MBED_CONF_MBED_TRACE_ENABLE) && MBED_CONF_MBED_TRACE_ENABLE
#include "mbed-trace/mbed_trace.h"
#include "mbed-trace-helper.h"
#endif
#include "stm32f4xx_hal_iwdg.h"
#include "low_power.h"
#include "log.h"

#include "ioc_utils.h"

/* This file implements the watchdog, the event queue, establishes the
 * system reset reason and includes some LED control for debugging.
 */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// The reason we woke up.
BACKUP_SRAM
static ResetReason gResetReason;

// For the watchdog.
// A prescaler value of 256 and a reload
// value of 0x0FFF gives a watchdog
// period of ~32 seconds (see STM32F4 manual
// section 21.3.3)
static IWDG_HandleTypeDef gWdt = {IWDG,
                                  {IWDG_PRESCALER_256, 0x0FFF}};

// LEDs for user feedback and debug.
static DigitalOut gLedRed(LED1, 1);
static DigitalOut gLedGreen(LED2, 1);
static DigitalOut gLedBlue(LED3, 1);

// The event loop and event queue.
static Thread *gpEventThread = NULL;
static EventQueue gEventQueue (32 * EVENTS_EVENT_SIZE);

/* ----------------------------------------------------------------
 * FUNCTIONS: DEBUG
 * -------------------------------------------------------------- */

// Indicate good (green).
void good() {
    gLedGreen = 0;
    gLedBlue = 1;
    gLedRed = 1;
}

// Switch bad (red) off again.
void notBad() {
    gLedRed = 1;
}

// Indicate bad (red).
void bad() {
    gLedRed = 0;
    gLedGreen = 1;
    gLedBlue = 1;
    if (gpEventThread != NULL) {
        gEventQueue.call_in(BAD_OFF_PERIOD_MS, notBad);
    }
}

// Toggle green.
void toggleGreen() {
    gLedGreen = !gLedGreen;
}

// Set blue.
void event() {
    gLedBlue = 0;
}

// Unset blue.
void notEvent() {
    gLedBlue = 1;
}

// Flash blue.
void flash() {
    gLedBlue = !gLedBlue;
    wait_ms(50);
    gLedBlue = !gLedBlue;
}

// All off.
void ledOff() {
    gLedBlue = 1;
    gLedRed = 1;
    gLedGreen = 1;
}

// Print heap stats.
void heapStats()
{
#ifdef MBED_HEAP_STATS_ENABLED
    mbed_stats_heap_t stats;
    mbed_stats_heap_get(&stats);

    printf("HEAP size:     %" PRIu32 ".\n", stats.current_size);
    printf("HEAP maxsize:  %" PRIu32 ".\n", stats.max_size);
#endif
}

/* ----------------------------------------------------------------
 * FUNCTIONS: MISC
 * -------------------------------------------------------------- */

// Find out what woke us up
ResetReason setResetReason()
{
    gResetReason = RESET_REASON_UNKNOWN;

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST)) {
        gResetReason = RESET_REASON_POWER_ON;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)) {
        gResetReason = RESET_REASON_SOFTWARE;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) {
        gResetReason = RESET_REASON_WATCHDOG;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST)) {
        gResetReason = RESET_REASON_PIN;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)) {
        gResetReason = RESET_REASON_LOW_POWER;
    }

    __HAL_RCC_CLEAR_RESET_FLAGS();

    return gResetReason;
}

ResetReason getResetReason()
{
    return gResetReason;
}

// Initialise the watchdog.
void initWatchdog()
{
    LOG(EVENT_WATCHDOG_START, 0);
    printf("Starting watchdog timer (%d seconds)...\n", WATCHDOG_WAKEUP_MS / 1000);
    if (HAL_IWDG_Init(&gWdt) != HAL_OK) {
        bad();
        LOG(EVENT_WATCHDOG_START_FAILURE, 0);
        printf("WARNING: unable to initialise watchdog, it is NOT running.\n");
    }
}
 
// Stop the watchdog going off.
void feedWatchdog()
{
    HAL_IWDG_Refresh(&gWdt);
}

// Initialise the event queue in its own thread.
void initEventQueue()
{
    gpEventThread = new Thread();
    gpEventThread->start(callback(&gEventQueue, &EventQueue::dispatch_forever));
}

// Shut down the event queue/thread.
void deinitEventQueue()
{
    gpEventThread->terminate();
    gpEventThread->join();
    delete gpEventThread;
    gpEventThread = NULL;
}

// Get the event queue.
EventQueue *pGetEventQueue()
{
    return &gEventQueue;
}

// End of file
