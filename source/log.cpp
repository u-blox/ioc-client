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

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

#define NUM_WRITES_BEFORE_FLUSH 10

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

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

// The number of calls to writeLog().
static int numWrites = 0;

// A logging buffer.
static LogEntry *gpLog = NULL;
static LogEntry *gpLogNextEmpty = NULL;
static LogEntry const *gpLogFirstFull = NULL;

// A logging timestamp.
static Timer gLogTime;

// A file to write logs to.
static FILE *gpFile = NULL;

// The name of the log file.
static char gFileNameBuffer[64];

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

// Open a log file, storing its name in gFileNameBuffer
// and returning a handle to it.
FILE * newLogFile(const char * pPartition)
{
    FILE *pFile = NULL;

    if (strlen(pPartition) < sizeof (gFileNameBuffer) - 11) {
        // 11 above is for two file separators, "xxxx.log" and a null terminator
        // (see sprintf below)
        for (unsigned int x = 0; (x < 1000) && (pFile == NULL); x++) {
            sprintf(gFileNameBuffer, "/%s/%04d.log", pPartition, x);
            // Try to open the file to see if it exists
            pFile = fopen(gFileNameBuffer, "r");
            // If it doesn't exist, use it, otherwise close
            // it and go around again
            if (pFile == NULL) {
                printf("Log file will be \"%s\".\n", gFileNameBuffer);
                pFile = fopen (gFileNameBuffer, "wb+");
                if (pFile != NULL) {
                    LOG(EVENT_FILE_OPEN, 0);
                } else {
                    LOG(EVENT_FILE_OPEN_FAILURE, errno);
                    perror ("Error initialising log file");
                }
            } else {
                fclose(pFile);
                pFile = NULL;
            }
        }
    }

    return pFile;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise logging.
bool initLog(void * pBuffer, const char * pPartition)
{
    gLogTime.reset();
    gLogTime.start();
    gpLog = (LogEntry * ) pBuffer;
    gpLogNextEmpty = gpLog;
    gpLogFirstFull = gpLog;
    LOG(EVENT_LOG_START, LOG_VERSION);

    if (pPartition != NULL) {
        gpFile = newLogFile(pPartition);
    }

    return (pPartition == NULL) || (gpFile != NULL);
}

// Initialise the log file.
bool initLogFile(const char * pPartition)
{
    if (gpFile == NULL) {
        gpFile = newLogFile(pPartition);
    }

    return (gpFile != NULL);
}

// Close down logging.
void deinitLog()
{
    LOG(EVENT_LOG_STOP, LOG_VERSION);
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

// Flush the log file.
// Note: log file mutex must be locked before calling.
void flushLog()
{
    if (gpFile != NULL) {
        fclose(gpFile);
        LOG(EVENT_FILE_CLOSE, 0);
        gpFile = fopen (gFileNameBuffer, "wb+");
        if (gpFile) {
            LOG(EVENT_FILE_OPEN, 0);
        } else {
            LOG(EVENT_FILE_OPEN_FAILURE, errno);
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
                numWrites++;
                if (numWrites > NUM_WRITES_BEFORE_FLUSH) {
                    numWrites = 0;
                    //flushLog();
                }
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
    bool loggingToFile = false;
    FILE *pFile = gpFile;
    unsigned int x = 0;

    logMutex.lock();
    printf ("------------- Log starts -------------\n");
    if (gpFile != NULL) {
        // If we were logging to file, read it back
        // First need to flush the file to disk
        loggingToFile = true;
        fclose(gpFile);
        gpFile = NULL;
        LOG(EVENT_FILE_CLOSE, 0);
        pFile = fopen (gFileNameBuffer, "rb");
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

    // Allow writeLog() to resume with the same file name
    if (loggingToFile) {
        gpFile = fopen (gFileNameBuffer, "wb+");
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
