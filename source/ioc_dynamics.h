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
#include "low_power.h"
#include "ioc_utils.h"

#ifndef _IOC_DYNAMICS_
#define _IOC_DYNAMICS_

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// Maximum sleep period.
#define MAX_SLEEP_SECONDS ((WATCHDOG_WAKEUP_MS / 1000) - 1)

// The partition on the SD card used by us.
#define IOC_PARTITION "ioc"

// The log file path on the partition.
// Note: I couldn't manage to open files using
// relative paths, hence this is the absolute
// path to the root of our partition.
#define LOG_FILE_PATH "/" IOC_PARTITION

// The log write interval.
#define LOG_WRITE_INTERVAL_MS 1000

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

 /** The possible sleep states of the MCU.
  */
typedef enum {
    MCU_STATE_OFF,
    MCU_STATE_STANDBY,
    MCU_STATE_NORMAL,
    MCU_STATE_UNKNOWN
} McuState;

/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */
/** Initialise dynamics.
 */
void initDynamics();

/** Shut down the file system.
 */
void deinitFileSystem();

/** Get the intended MCU state.
 * @return the MCU state.
 */
McuState getMcuState();

/** Set the intended MCU state.
 * @param mcuState the MCU state.
 */
void setMcuState(McuState mcuState);

/** Enter Standby for the given time.
 * @param standbyTimeSeconds the duration.
 */
void enterStandby(time_t standbyTimeSeconds);

/** Get the time sleep (of an form) was entered.
 */
time_t getTimeEnterSleep();

/** Get the time sleep (of an form) was left.
 */
time_t getTimeLeaveSleep();

/** Set the sleep level to registered sleep.
 * @param sleepDurationSeconds the duration of the sleep.
 */
void setSleepLevelRegisteredSleep(time_t sleepDurationSeconds);

/** Set the sleep level to deregistered sleep.
 * @param sleepDurationSeconds the duration of the sleep.
 */
void setSleepLevelDeregisteredSleep(time_t sleepDurationSeconds);

/** Set the sleep level to off.
 */
void setSleepLevelOff();

/** Enter Initialisation mode.
 */
void initialisationMode();

/** Enter Ready mode.
 */
void readyMode();

/** Deal with the fact that an instruction
 * has been received from the server.
 */
void readyModeInstructionReceived();

#endif // _IOC_DYNAMICS_

// End of file
