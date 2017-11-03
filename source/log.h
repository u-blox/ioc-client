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

/* This logging utility allows events to be logged to RAM and, optionally
 * to file at minimal run-time cost.
 *
 * Each entry includes an event, a 32 bit parameter (which is printed with
 * the event) and a microsecond time-stamp.
 */

#include "stdbool.h"
#include "log_enum.h"

#ifndef _LOG_
#define _LOG_

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** The number of log entries.
 */
#ifndef MAX_NUM_LOG_ENTRIES
# define MAX_NUM_LOG_ENTRIES 500
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** An entry in the log.
 */
typedef struct {
    unsigned int timestamp;
    LogEvent event;
    int parameter;
} LogEntry;


/** The size of the log store, given the number of entries requested.
 */
#define LOG_STORE_SIZE (sizeof (LogEntry) * MAX_NUM_LOG_ENTRIES)

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/** Log an event plus parameter.
 *
 * @param event     the event.
 * @param parameter the parameter.
 */
void LOG(LogEvent event, int parameter);

/** Initialise logging.
 *
 * @param pBuffer    must point to LOG_STORE_SIZE bytes of storage.
 * @param pFileName  should point to the desired name of a log file if
 *                   writing to file is required, otherwise NULL.
 *                   NOTE: must be a const as this function does
 *                   not take a copy of it.
 * @return           true if successful, otherwise false.
 */
bool initLog(void * pBuffer, const char *pFileName);

/** Initialise a log file.  May be used if no file system was available
 * at the time of the call to initLog.
 *
 * @param pFileName  the name of the log file  to open.
 *                   NOTE: must be a const as this function does
 *                   not take a copy of it.
 * @return           true if successful, otherwise false.
 */
bool initLogFile(const char *pFileName);

/** Close down logging.
 */
void deinitLog();

/** Write the logging buffer to the log file.
 */
void writeLog();

/** Print out the logged items.
 */
void printLog();

#ifdef __cplusplus
}
#endif

#endif

// End of file
