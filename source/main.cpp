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

#include "low_power.h"
#include "log.h"
#include "compile_time.h"

#include "ioc_temperature_battery.h"
#include "ioc_power_control.h"
#include "ioc_config.h"
#include "ioc_dynamics.h"
#include "ioc_utils.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// For logging.
__attribute__ ((section ("CCMRAM")))
static char gLogBuffer[LOG_STORE_SIZE];

/* ----------------------------------------------------------------
 * MAIN
 * -------------------------------------------------------------- */

int main()
{
    time_t sleepTimeLeft;
    Ticker *pSecondTicker = NULL;

    // If you find the IOC client is waking up in "OFF"
    // mode (and hence going back to sleep again) uncomment
    // this line while you debug why that's happening.
    //setMcuState(MCU_STATE_UNKNOWN);

    // If we've been in Standby need to feed
    // the watchdog nice and early
    feedWatchdog();

    flash();
    setResetReason();

    // If this is a power on reset, do a system
    // reset to get us out of our debug-mode
    // entanglement with the debug chip on the
    // mbed board and allow power saving
    if (getResetReason() == RESET_REASON_POWER_ON) {
        NVIC_SystemReset();
    }

    flash();
    initLog(gLogBuffer);

    LOG(EVENT_SYSTEM_START, getResetReason());
    LOG(EVENT_BUILD_TIME_UNIX_FORMAT, __COMPILE_TIME_UNIX__);

    // Bring up the battery charger and battery gauge on the I2C bus
    pInitI2c();

    // If we should be off, and there is no external
    // power to keep us going, go straight back to sleep
    if ((getMcuState() == MCU_STATE_OFF) &&
        !isExternalPowerPresent()) {
        feedWatchdog();
        LOG(EVENT_ENTER_STANDBY, MAX_SLEEP_SECONDS * 1000);
        deinitLog();  // So that we have a complete record
        enterStandby(MAX_SLEEP_SECONDS);
    }

    // If we've been in standby and the RTC is running,
    // check if it's actually time to wake up yet
    if ((getMcuState() == MCU_STATE_STANDBY) &&
        (time(NULL) != 0)) {
        sleepTimeLeft = getTimeLeaveSleep() - time(NULL);
        if (sleepTimeLeft > 0) {
            // Not time to wake up yet, feed the watchdog and go
            // back to sleep
            feedWatchdog();
            if (sleepTimeLeft > MAX_SLEEP_SECONDS) {
                sleepTimeLeft = MAX_SLEEP_SECONDS;
            }
            LOG(EVENT_ENTER_STANDBY, MAX_SLEEP_SECONDS * 1000);
            deinitLog();  // So that we have a complete record
            enterStandby(sleepTimeLeft);
        }
        printf("Awake from DEREGISTERED_SLEEP after %d second(s).\n",
               (int) (time(NULL) - getTimeEnterSleep()));
    }

    // If we were not running normally, this must have been a power-on reset,
    // so zero the wake-up tick counter and set up configuration defaults
    // Note: can't check for the gResetReason being RESET_REASON_POWER_ON because
    // that's not the case under the debugger.
    if (getMcuState() != MCU_STATE_NORMAL) {
        initDynamics();
        resetPowerControl();
        resetConfig();
    }

#if defined(MBED_CONF_MBED_TRACE_ENABLE) && MBED_CONF_MBED_TRACE_ENABLE
    // NOTE: the mutex causes output to stop under heavy load, hence
    // it is commented out here.
    // Create mutex for tracing to avoid broken lines in logs
    //MBED_ASSERT(mbed_trace_helper_create_mutex());

    // Initialize mbed trace
    mbed_trace_init();
    //mbed_trace_mutex_wait_function_set(mbed_trace_helper_mutex_wait);
    //mbed_trace_mutex_release_function_set(mbed_trace_helper_mutex_release);
#endif

    printf("\n********** START **********\n");

    heapStats();

    // Run through the Initialisation and Ready
    // modes.  Exit is via various forms of sleep
    // or reset, or most naturally via the user
    // button switching everything off, in which
    // case readyMode() will return.
    initialisationMode();
    readyMode();

    heapStats();

    feedWatchdog();
    flash();
    printf("Printing the log...\n");
    // Run a ticker to feed the watchdog while we print out the log
    pSecondTicker = new Ticker();
    pSecondTicker->attach_us(callback(&feedWatchdog), 1000000);
    printLog();
    pSecondTicker->detach();
    delete pSecondTicker;
    printf("Stopping logging...\n");
    deinitLog();
    deinitFileSystem();

    LOG(EVENT_SYSTEM_STOP, 0);
    printf("********** STOP **********\n");
    ledOff();

    setSleepLevelOff();
}

// End of file
