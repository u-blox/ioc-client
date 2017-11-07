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
#include "log.h"

// To obtain file system errors.
extern int errno;

// The strings associated with the enum values.
extern const char *gLogStrings[];
extern const int gNumLogStrings;

// Mutex to arbitrate logging.
// The callback which writes logging to disk
// will attempt to lock this mutex while the
// function that prints out the log owns the
// mutex. Note that the logging functions
// themselves shouldn't wait on it (they have
// no reason to as the buffering should
// handle any overlap); they MUST return quickly.
static Mutex logMutex;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// A logging buffer.
static LogEntry *gpLog = NULL;
static LogEntry *gpLogNextEmpty = NULL;
static LogEntry const *gpLogFirstFull = NULL;

// A logging timestamp.
static Timer gLogTime;

// A file to write logs to.
static FILE *gpFile = NULL;

// The name of the log file (needed
// so that we can open and close it in
// order to flush it since fflush()
// does nothing).
static const char *gpFileName = NULL;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Print a single item from a log.
void printLogItem(const LogEntry *pItem, unsigned int itemIndex)
{
    if (pItem->event > gNumLogStrings) {
        printf("%.3f: out of range event at entry %d (%d when max is %d)\n",
               (float) pItem->timestamp / 1000, itemIndex, pItem->event, gNumLogStrings);
    } else {
        printf ("%6.3f: %s %d (%#x)\n", (float) pItem->timestamp / 1000,
                gLogStrings[pItem->event], pItem->parameter, pItem->parameter);
    }

}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise logging.
bool initLog(void * pBuffer, const char * pFileName)
{
    bool success = false;

    gpLog = (LogEntry * ) pBuffer;
    gpFileName = NULL;
    gpLogNextEmpty = gpLog;
    gpLogFirstFull = gpLog;

    gLogTime.reset();
    gLogTime.start();

    LOG(EVENT_LOG_START, 0);

    if (pFileName != NULL) {
        gpFile = fopen (pFileName, "wb+");
        if (gpFile != NULL) {
            gpFileName = pFileName;
            LOG(EVENT_FILE_OPEN, 0);
            success = true;
        } else {
            LOG(EVENT_FILE_OPEN_FAILURE, errno);
            perror ("Error initialising log file");
        }
    } else {
        success = true;
    }

    return success;
}

// Initialise the log file.
bool initLogFile(const char * pFileName)
{
    bool success = false;

    if (gpFile == NULL) {
        gpFileName = NULL;
        gpFile = fopen (pFileName, "wb+");
        if (gpFile) {
            gpFileName = pFileName;
            LOG(EVENT_FILE_OPEN, 0);
            success = true;
        } else {
            LOG(EVENT_FILE_OPEN_FAILURE, errno);
            perror ("Error initialising log file");
        }
    }

    return success;
}

// Close down logging.
void deinitLog()
{
    LOG(EVENT_LOG_STOP, 0);
    if (gpFile != NULL) {
        LOG(EVENT_FILE_CLOSE, 0);
        fclose(gpFile);
        gpFile = NULL;
    }

    // Don't reset the variables
    // here so that printLog() still
    // works afterwards if we're just
    // logging to RAM rather than
    // to file.
}

// Log an event plus parameter.
void LOG(LogEvent event, int parameter)
{
    if (gpLogNextEmpty) {
        gpLogNextEmpty->timestamp = gLogTime.read_us();
        gpLogNextEmpty->event = event;
        gpLogNextEmpty->parameter = parameter;
        if (gpLogNextEmpty < gpLog + MAX_NUM_LOG_ENTRIES - 1) {
            gpLogNextEmpty++;
        } else {
            gpLogNextEmpty = gpLog;
        }

        if (gpLogNextEmpty == gpLogFirstFull) {
            // Logging has wrapped, so move the
            // first pointer on to reflect the
            // overwrite
            if (gpLogFirstFull < gpLog + MAX_NUM_LOG_ENTRIES - 1) {
                gpLogFirstFull++;
            } else {
                gpLogFirstFull = gpLog;
            }
        }
    }
}

// This should be called periodically to write the log
// to file, if a filename was provided to initLog().
void writeLog()
{
    if (logMutex.trylock()) {
        if (gpFile != NULL) {
            while (gpLogNextEmpty != gpLogFirstFull) {
                fwrite(gpLogFirstFull, sizeof(LogEntry), 1, gpFile);
                if (gpLogFirstFull < gpLog + MAX_NUM_LOG_ENTRIES - 1) {
                    gpLogFirstFull++;
                } else {
                    gpLogFirstFull = gpLog;
                }
            }
        }
        logMutex.unlock();
    }
}

// Print out the log.
void printLog()
{
    const LogEntry *pItem = gpLogNextEmpty;
    LogEntry fileItem;
    FILE *pFile = gpFile;
    unsigned int x = 0;

    logMutex.lock();
    printf ("------------- Log starts -------------\n");
    if ((gpFile != NULL) && (gpFileName != NULL)) {
        // If we were logging to file, read it back
        // First need to flush the file to disk
        fclose(gpFile);
        gpFile = NULL;
        LOG(EVENT_FILE_CLOSE, 0);
        pFile = fopen (gpFileName, "rb");
        if (pFile != NULL) {
            LOG(EVENT_FILE_OPEN, 0);
            while (fread(&fileItem, sizeof(fileItem), 1, pFile) == 1) {
                printLogItem(&fileItem, x);
                x++;
            }
            // If we're not at the end of the file, there must have been an error
            if (!feof(pFile)) {
                perror ("Error reading portion of log stored in file system");
            }
            fclose(pFile);
            LOG(EVENT_FILE_CLOSE, 0);
        } else {
            perror ("Error opening portion of log stored in file system");
        }
    }

    // Print the log items remaining in RAM
    pItem = gpLogFirstFull;
    x = 0;
    while (pItem != gpLogNextEmpty) {
        printLogItem(pItem, x);
        x++;
        pItem++;
        if (pItem >= gpLog + MAX_NUM_LOG_ENTRIES) {
            pItem = gpLog;
        }
    }

    // Allow writeLog() to resume
    if (gpFileName != NULL) {
        gpFile = fopen (gpFileName, "wb+");
        if (gpFile) {
            LOG(EVENT_FILE_OPEN, 0);
        } else {
            LOG(EVENT_FILE_OPEN_FAILURE, errno);
            perror ("Error initialising log file");
        }
    }

    printf ("-------------- Log ends --------------\n");
    logMutex.unlock();
}

// End of file
