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

#ifndef _IOC_UTILS_
#define _IOC_UTILS_

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// Interval at which the watchdog should be fed.
#define WATCHDOG_WAKEUP_MS 32000

// The period after which the "bad" status LED is tidied up.
#define BAD_OFF_PERIOD_MS 10000

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// The possible wake-up reasons.
typedef enum {
  RESET_REASON_UNKNOWN,
  RESET_REASON_POWER_ON,
  RESET_REASON_SOFTWARE,
  RESET_REASON_WATCHDOG,
  RESET_REASON_PIN,
  RESET_REASON_LOW_POWER,
  NUM_RESET_REASONS
} ResetReason;

/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Indicate "good".
 */
void good();

/** Indicate "not bad".
 */
void notBad();

/** Indicate "bad".
 */
void bad();

/** Toggle the green LED.
 */
void toggleGreen();

/** Flag an event.
 */
void event();

/** Clear an event flag.
 */
void notEvent();

/** Flash the LED briefly.
 */
void flash();

/** Switch all LEDs off.
 */
void ledOff();

/** Print heap stats.
 */
void heapStats();

/** Find out what woke us up.   Use
 * this at power on.
 * @return the reason we woke up.
 */
ResetReason setResetReason();

/** Return the wake-up reason.  Use
 * this to obtain the reset reason
 * after setResetReason() has already
 * been called.
 * @return the reason we woke up.
 */
ResetReason getResetReason();

/** Initialise the watchdog.
 */
void initWatchdog();

/** Feed the watchdog.
 */
void feedWatchdog();

/** Initialise the event queue in its own thread.
 */
void initEventQueue();

/** Shut down the event queue/thread.
 */
void deinitEventQueue();

/** Get a pointer to the event queue.
 * @return a pointer to the event queue.
 */
EventQueue *pGetEventQueue();

#endif // _IOC_UTILS_

// End of file
