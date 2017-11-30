/* mbed Microcontroller Library
 * Copyright (c) 2017 u-blox
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"
#include "SDBlockDevice.h"
#include "FATFileSystem.h"
#include "log.h"

#include "ioc_cloud_client_dm.h"
#include "ioc_diagnostics.h"
#include "ioc_temperature_battery.h"
#include "ioc_config.h"
#include "ioc_audio.h"
#include "ioc_dynamics.h"
#include "ioc_network.h"
#include "ioc_logging.h"
#include "ioc_utils.h"

/* This file implements the dynamic behaviour of the IOC client.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The maximum size of a history marker.
// The history marker is a string stored in battery-backed SRAM
// which allows us to tell what the system was doing previously.
// On a power-on reset is will contain random garbage.
#define HISTORY_MARKER_MAX_SIZE 6

// History marker indicating that the system was in standby.
#define HISTORY_MARKER_STANDBY "stdby"

// History marker indicating that the system was off.
#define HISTORY_MARKER_OFF "off"

// History marker indicating that the system was running
// normally.
#define HISTORY_MARKER_NORMAL "norm"

// The interval at which we check for exit.
#define BUTTON_CHECK_INTERVAL_MS 1000

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// The SD card, which is instantiated in the Mbed Cloud Client
// in pal_plat_fileSystem.cpp.
extern SDBlockDevice sd;

// A file system (noting that Mbed Cloud Client has another
// file system entirely of its own named "sd").
FATFileSystem gFs(IOC_PARTITION, &sd);

// The user button.
static InterruptIn *gpUserButton = NULL;
static volatile bool gUserButtonPressed = false;

// The low power driver.
static LowPower gLowPower;

// Storage for a marker in back-up SRAM to indicate
// what was happening before the system started.
BACKUP_SRAM
static char gHistoryMarker[HISTORY_MARKER_MAX_SIZE];

// Put the time that we went to sleep in back-up SRAM.
BACKUP_SRAM
static time_t gTimeEnterSleep;

// Put the time that we should wake from sleep in back-up SRAM.
BACKUP_SRAM
static time_t gTimeLeaveSleep;

// The wake-up tick counter
BACKUP_SRAM
static int gWakeUpTickCounter;

// Event ID for wake-up tick handler.
static int gWakeUpTickHandler = -1;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: MISC
 * -------------------------------------------------------------- */

// Function to attach to the user button.
static void buttonCallback()
{
    gUserButtonPressed = true;
    LOG(EVENT_BUTTON_PRESSED, 0);
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: INITIALISATION AND DEINITIALISATION
 * -------------------------------------------------------------- */

// Initialise everything, bringing us to SleepLevel REGISTERED.
// If you add anything here, be sure to add the opposite
// to deinit().
// Note: here be multiple return statements.
static bool init()
{
    bool success = false;
    NetworkInterface *pNetworkInterface;

    setStartTime(time(NULL));
    initWatchdog();

    flash();
    printf("Creating user button...\n");
    gpUserButton = new InterruptIn(SW0);
    gpUserButton->rise(&buttonCallback);

    if ((pInitCloudClientDm() != NULL) &&
        ((pNetworkInterface = pInitNetwork()) != NULL) &&
        connectCloudClientDm(pNetworkInterface)) {
        success = true;
    }

    return success;
}

// Initialise file system.
static bool initFileSystem()
{
    int x;

    flash();
    LOG(EVENT_SD_CARD_START, 0);
    printf("Starting SD card...\n");
    x = sd.init();
    if (x == 0) {
        printf("Mounting file system...\n");
        gFs.mount(&sd);
    } else {
        bad();
        LOG(EVENT_SD_CARD_START_FAILURE, 0);
        printf("Error initialising SD card (%d).\n", x);
        return false;
    }
    printf("SD card started.\n");
    
    if (isLoggingToFileEnabled()) {
        flash();
        printf("Starting logging to file...\n");
        if (initLogFile(LOG_FILE_PATH)) {
            pGetEventQueue()->call_every(LOG_WRITE_INTERVAL_MS, writeLog);
        } else {
            printf("WARNING: unable to initialise logging to file.\n");
        }
    }

    return true;
}

// Shut everything down.
// Anything that was set up in init() should be cleared here.
static void deinit()
{
    int x;

    deinitCloudClientDm();
    deinitNetwork();

    if (gpUserButton != NULL) {
        flash();
        printf("Removing user button...\n");
        delete gpUserButton;
        gpUserButton = NULL;
    }

    x = time(NULL) - getStartTime();
    printf("Up for %d second(s).\n", x);
    LOG(EVENT_SYSTEM_UP_FOR, x);

    printf("All stop.\n");
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: OPERATING MODES AND SLEEP
 * -------------------------------------------------------------- */

/* The dynamic behaviour of the Internet Of Chuffs client is as
 * follows:
 *
 * - there is a wakeUpTick, a wakeUpTickCounter, a
 *   wakeUpTickCounterModulo and a sleepLevel,
 *
 * - wakeUpTickCounter is incremented every wakeUpTick, modulo
 *   wakeUpTickCounterModulo,
 *
 * - the possible sleepLevels are:
 *
 *   REGISTERED:         peripherals are up (though they may be quiescent),
 *                       GNSS may be on, modem is on, MCU is clocked
 *                       normally, the IOC Client is registered with the
 *                       LWM2M Mbed Cloud Server; successful init() brings
 *                       the IOC Client into this sleepLevel,
 *   REGISTERED_SLEEP:   as "REGISTERED" except MCU is in clock-stop so no
 *                       timers are running, the MCU will wake up from RTC
 *                       interrupt,
 *   DEREGISTERED_SLEEP: peripherals are in lowest power state, GNSS and
 *                       modem are off, MCU is in deep sleep (so RAM is off
 *                       as well) but will awake from RTC interrupt, the IOC
 *                       Client is deregistered from the LWM2M Mbed Cloud
 *                       Server; deinit() puts the IOC Client into this
 *                       sleepLevel and init() must be run on return,
 *   OFF:                as "DEREGISTERED_SLEEP" except that a power-cycle
 *                       is required to wake the IOC Client up; note that,
 *                       since the watchdog is active, OFF isn't truly
 *                       off, the MCU will wake up at the watchdog interval
 *                       and put itself immediately back to sleep again.
 *
 * - the IOC client wakes up, either from sleepLevel or from
 *   power-cycle, performs some operation and, when the operation
 *   finishes, the IOC client is returned to sleepLevel,
 *
 * - there are two modes of behaviour, Initialisation and Ready:
 *
 *   - in Initialisation mode the IOC client is trying to register
 *     with the LWM2M server in the Mbed Cloud,
 *   - in Ready mode the IOC client reports-in to the Mbed Cloud
 *     on a regular basis and awaits instructions from it.
 *
 * - In summary, the life-cycle is as follows, implemented using
 *   the mechanisms above:
 *
 *   1. enter Initialisation mode for a time,
 *   2. if Initialisation mode is successfully completed, go to
 *      Ready mode for a period of time,
 *   3. if an instruction is received in Ready mode, reset
 *      the Ready mode timer,
 *   4. if Initialisation mode is not completed in time or the Ready
 *      mode timer expires, return to OFF,
 *   5. the timers will be set such that, if there is no external
 *      power, the IOC client stays awake for about 60 minutes and
 *      no longer.
 *
 * In detail, Initialisation mode dynamic behaviour is as follows:
 *
 * - the variables are set up on entry:
 *   - wakeUpTick period:       10 minutes [initWakeUpTickCounterPeriod],
 *   - sleepLevel:              DEREGISTERED_SLEEP,
 *   - wakeUpTickCounterModulo: 3 [initWakeUpTickCounterModulo],
 * - on wake-up, run init(),
 * - if init() is completed, move immediately to Ready mode,
 * - at each tick:
 *   - if wakeUpTickCounterModulo has been reached AND
 *     there is NO external power, go to sleepLevel OFF,
 *   - otherwise, run init().
 *
 * In detail, Ready mode dynamic behaviour is as follows:
 *
 * - the variables are set up on entry:
 *   - wakeUpTick period:       1 minute [readyWakeUpTickCounterPeriod1],
 *   - sleepLevel:              REGISTERED_SLEEP,
 *   - wakeUpTickCounterModulo: 60 [readyWakeUpTickCounterModulo],
 * - at each tick, report-in to the Mbed Cloud LWM2M server,
 * - if an instruction is received from the Mbed Cloud LWM2M
 *   server then:
 *   - act upon it,
 *   - if there is external power, reset the wakeUpTickCounter,
 * - if no instruction is received, or the instruction
 *   has been completed, go to sleepLevel until the next
 *   tick,
 * - if wakeUpTickCounterModulo is reached:
 *   - if audio streaming is in progress, keep wakeUpTick
 *     at 1 minute, otherwise:
 *     - if there is external power, set wakeUpTick to 10
 *       minutes [readyWakeUpTickCounterPeriod2],
 *     - if there is no external power, go to sleepLevel OFF.
 */

// The Initialisation mode wake-up tick handler.
static void initialisationModeWakeUpTickHandler()
{
    gWakeUpTickCounter++;
    LOG(EVENT_INITIALISATION_MODE_WAKE_UP_TICK, gWakeUpTickCounter);
    if (gWakeUpTickCounter >= getInitWakeUpTickCounterModulo()) {
        gWakeUpTickCounter = 0;
        if (isExternalPowerPresent()) {
            // If there is no external power and we've got here, it's been
            // far too long so just give up
            setSleepLevelOff();
        } else {
            // Otherwise, enter standby with a short
            // timer, which will reset us to start trying
            // again
            LOG(EVENT_ENTER_STANDBY, 100);
            deinitLog();  // So that we have a complete record
            gLowPower.enterStandby(100);
        }
    }
}

// The Ready mode wake-up tick handler.
static void readyModeWakeUpTickHandler()
{
    gWakeUpTickCounter++;
    LOG(EVENT_READY_MODE_WAKE_UP_TICK, gWakeUpTickCounter);
    if (gWakeUpTickCounter >= getReadyWakeUpTickCounterModulo()) {
        gWakeUpTickCounter = 0;
        if (isAudioStreamingEnabled()) {
            // If we're streaming, make sure we stay awake
            pGetEventQueue()->cancel(gWakeUpTickHandler);
            gWakeUpTickHandler = pGetEventQueue()->call_every(getReadyWakeUpTickCounterPeriod1() * 1000, readyModeWakeUpTickHandler);
        } else {
            if (isExternalPowerPresent()) {
                // If there is no external power we've been awake for long enough
                setSleepLevelOff();
            } else {
                // Otherwise, just switch to the long repeat period as obviously
                // nothing much is happening
                pGetEventQueue()->cancel(gWakeUpTickHandler);
                gWakeUpTickHandler = pGetEventQueue()->call_every(getReadyWakeUpTickCounterPeriod2() * 1000, readyModeWakeUpTickHandler);
            }
        }
    }

    // Update the objects that the LWM2M server can observe
    cloudClientObjectUpdate();

    // TODO call Mbed Cloud Client keep alive once we've
    // understood how to drive Mbed Cloud Client in that way
}

/* ----------------------------------------------------------------
 * PUBLIC
 * -------------------------------------------------------------- */

// Initialise dynamics.
void initDynamics()
{
    gWakeUpTickCounter = 0;
    gTimeEnterSleep = 0;
    gTimeLeaveSleep = 0;
    setMcuState(MCU_STATE_NORMAL);
}

// Shutdown file system
void deinitFileSystem()
{
    flash();
    LOG(EVENT_SD_CARD_STOP, 0);
    printf("Closing SD card and unmounting file system...\n");
    sd.deinit();
    gFs.unmount();
}

// Get the intended MCU state.
McuState getMcuState()
{
    McuState mcuState = MCU_STATE_UNKNOWN;
    
    if (memcmp(gHistoryMarker, HISTORY_MARKER_OFF, sizeof (HISTORY_MARKER_OFF)) == 0) {
        mcuState = MCU_STATE_OFF;
    } else if (memcmp(gHistoryMarker, HISTORY_MARKER_STANDBY, sizeof (HISTORY_MARKER_STANDBY)) == 0) {
        mcuState = MCU_STATE_STANDBY;
    } else if (memcmp(gHistoryMarker, HISTORY_MARKER_NORMAL, sizeof (HISTORY_MARKER_NORMAL)) == 0) {
        mcuState = MCU_STATE_NORMAL;
    }
    
    return mcuState;
}
 
// Set the intended MCU state.
void setMcuState(McuState mcuState)
{
    switch (mcuState) {
        case MCU_STATE_UNKNOWN:
            memset(gHistoryMarker, 0, sizeof (gHistoryMarker));
            break;
        case MCU_STATE_OFF:
            memcpy(gHistoryMarker, HISTORY_MARKER_OFF, sizeof (gHistoryMarker));
            break;
        case MCU_STATE_STANDBY:
            memcpy(gHistoryMarker, HISTORY_MARKER_STANDBY, sizeof (gHistoryMarker));
            break;
        case MCU_STATE_NORMAL:
            memcpy(gHistoryMarker, HISTORY_MARKER_NORMAL, sizeof (gHistoryMarker));
            break;
        default:
            MBED_ASSERT(false);
            break;
    }
}
 
// Enter standby for a given time.
void enterStandby(time_t standbyTimeSeconds)
{
    gLowPower.enterStandby(standbyTimeSeconds * 1000);
}
 
// Return the time sleep was entered.
time_t getTimeEnterSleep()
{
    return gTimeEnterSleep;
}

// Return the time sleep was left.
time_t getTimeLeaveSleep()
{
    return gTimeLeaveSleep;
}

// Go to MCU sleep but remain registered, for the given time.
void setSleepLevelRegisteredSleep(time_t sleepDurationSeconds)
{
    time_t sleepTimeLeft;
    gTimeEnterSleep = time(NULL);
    gTimeLeaveSleep = gTimeEnterSleep + sleepDurationSeconds;

    LOG(EVENT_SLEEP_LEVEL_REGISTERED, sleepDurationSeconds);
    printf("Going to REGISTERED_SLEEP for %d second(s), until %s",
           (int) (sleepDurationSeconds), ctime(&gTimeLeaveSleep));

    // Need to wake-up at the watchdog interval to feed it
    while ((sleepTimeLeft = (gTimeLeaveSleep - time(NULL))) > 0) {
        if (sleepTimeLeft > MAX_SLEEP_SECONDS) {
            sleepTimeLeft = MAX_SLEEP_SECONDS;
        }

        feedWatchdog();
        LOG(EVENT_ENTER_STOP, sleepTimeLeft);
        gLowPower.enterStop(sleepTimeLeft);
        deinitLog();  // So that we have a complete record up to this point
    }

    printf("Awake from REGISTERED_SLEEP after %d second(s).\n", (int) (time(NULL) - gTimeEnterSleep));
}

// Go to deregistered sleep for the given time.
void setSleepLevelDeregisteredSleep(time_t sleepDurationSeconds)
{
    gTimeEnterSleep = time(NULL);
    gTimeLeaveSleep = gTimeEnterSleep + sleepDurationSeconds;

    LOG(EVENT_SLEEP_LEVEL_DEREGISTERED, sleepDurationSeconds);
    printf("Going to DEREGISTERED_SLEEP for %d second(s), until %s",
           (int) (sleepDurationSeconds), ctime(&gTimeLeaveSleep));

    // Need to wake-up at the watchdog interval to feed it
    if (sleepDurationSeconds > MAX_SLEEP_SECONDS) {
        sleepDurationSeconds = MAX_SLEEP_SECONDS;
    }
    setMcuState(MCU_STATE_STANDBY);
    feedWatchdog();
    LOG(EVENT_ENTER_STANDBY, sleepDurationSeconds * 1000);
    deinitLog();  // So that we have a complete record
    enterStandby(sleepDurationSeconds);
    // The wake-up process is handled on entry to main()
}

// Go to OFF sleep state, from which only a power
// cycle will awaken us (or the watchdog, but we'll
// immediately go back to sleep again)
void setSleepLevelOff()
{
    LOG(EVENT_SLEEP_LEVEL_OFF, 0);
    setMcuState(MCU_STATE_OFF);
    feedWatchdog();
    LOG(EVENT_ENTER_STANDBY, MAX_SLEEP_SECONDS * 1000);
    deinitLog();  // So that we have a complete record
    enterStandby(MAX_SLEEP_SECONDS);
}

// Perform Initialisation mode.
void initialisationMode()
{
    bool success = false;
    time_t timeStarted;

    // Start the event queue in the event thread
    initEventQueue();

    // Add the Initialisation mode wake-up handler
    LOG(EVENT_INITIALISATION_MODE_START, 0);
    gWakeUpTickHandler = pGetEventQueue()->call_every(getInitWakeUpTickCounterPeriod() * 1000, initialisationModeWakeUpTickHandler);

    // Initialise everything.  There are three possible outcomes:
    //
    // - initFileSystem() && init() succeeds, in which case this function returns,
    // - initFileSystem() && init() fails, in which case we go to deregistered sleep for
    //   the remainder of the wake up tick period,
    // - initFileSystem() && init() takes longer than the wake up tick period, in which case
    //   the initWakeUpTickHandler will kick in and restart things
    //   in the appropriate way.
    while (!success) {
        timeStarted = time(NULL);
        success = initFileSystem() && init();
        if (!success) {
            setSleepLevelDeregisteredSleep(getInitWakeUpTickCounterPeriod() - (time(NULL) - timeStarted));
        }
    }

    // The cloud client registration event is asynchronous
    // to the above, so need to wait for it now
    while (!isCloudClientConnected()) {
        feedWatchdog();
        wait_ms(CLOUD_CLIENT_REGISTRATION_CHECK_INTERVAL_MS);
    }

    // Having done all of that, it's now safe to begin
    // uploading any log files that might be lying around
    // from previous runs to a logging server
    if (isLoggingUploadEnabled()) {
        beginLogFileUpload(&gFs, pGetNetworkInterface(), pGetLoggingServerUrl());
    }

    // Remove the Initialisation mode wake-up handler
    pGetEventQueue()->cancel(gWakeUpTickHandler);
    gWakeUpTickHandler = -1;
}

// Deal with the fact that an instruction
// has been received from the server.
void readyModeInstructionReceived()
{
    LOG(EVENT_READY_MODE_INSTRUCTION_RECEIVED, 0);
    if (isExternalPowerPresent()) {
        // If there is external power, reset the tick counter
        // so that we stay awake
        LOG(EVENT_READY_MODE_WAKE_UP_TICK_COUNTER_RESET, 0);
        gWakeUpTickCounter = 0;
    }
}

// Perform Ready mode.
void readyMode()
{
    // Switch to the Ready mode wake-up handler and zero the tick count
    LOG(EVENT_READY_MODE_START, 0);
    gWakeUpTickCounter = 0;
    gWakeUpTickHandler = pGetEventQueue()->call_every(getReadyWakeUpTickCounterPeriod1() * 1000, readyModeWakeUpTickHandler);

    for (int x = 0; !gUserButtonPressed; x++) {
        feedWatchdog();
        // TODO should go to sleep here but we can't until we find out
        // how to drive Mbed Cloud Client in that way
        wait_ms(BUTTON_CHECK_INTERVAL_MS);
    }

    // Cancel the Ready mode wake-up handler
    pGetEventQueue()->cancel(gWakeUpTickHandler);

    // Stop the event queue
    deinitEventQueue();

    // Shut everything down
    deinit();
    deinitI2c();
    ledOff();
}

// End of file
