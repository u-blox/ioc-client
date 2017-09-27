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
 * VARIABLES
 * -------------------------------------------------------------- */

// A logging buffer
__attribute__ ((section ("CCMRAM")))
static LogEntry gLog[MAX_NUM_LOG_ENTRIES];
static LogEntry *gpLogNext = gLog;
static unsigned int gNumLogEntries = 0;
// A logging timestamp
static Timer gLogTime;

// The events as strings (must be kept in line with the
// LogEvent enum).
// Conventionally, a "*" prefix means that a bad thing
// has happened, makes them easier to spot in a stream
// of prints flowing up the console window.
static const char * gLogStrings[] = {
    "  EMPTY",
    "  USER_1",
    "  USER_2",
    "  USER_3",
    "  LOG_START",
    "  LOG_STOP",
    "  FILE_OPEN",
    "  FILE_OPEN_FAILURE",
    "  FILE_CLOSE",
    "  NETWORK_START",
    "  NETWORK_START_FAILURE",
    "  NETWORK_STOP",
    "  TCP_CONNECTED",
    "* TCP_CONNECTION_PROBLEM",
    "  TCP_CONFIGURED",
    "* TCP_CONFIGURATION_PROBLEM",
    "  I2S_START",
    "  I2S_STOP",
    "  BUTTON_PRESSED",
    "  I2S_DMA_RX_HALF_FULL",
    "  I2S_DMA_RX_FULL",
    "* I2S_DMA_UNKNOWN",
    "  CONTAINER_STATE_EMPTY",
    "  CONTAINER_STATE_WRITING",
    "  CONTAINER_STATE_READY_TO_READ",
    "  CONTAINER_STATE_READING",
    "  CONTAINER_STATE_READ",
    "  DATAGRAM_NUM_SAMPLES",
    "  DATAGRAM_SIZE",
    "* DATAGRAM_OVERFLOW_BEGINS",
    "* DATAGRAM_NUM_OVERFLOWS",
    "  RAW_AUDIO_DATA_0",
    "  RAW_AUDIO_DATA_1",
    "  RAW_AUDIO_POSSIBLE_ROTATION",
    "  RAW_AUDIO_ROTATION_VOTE",
    "  RAW_AUDIO_DATA_ROTATION",
    "  RAW_AUDIO_DATA_ROTATION_NOT_FOUND",
    "  STREAM_MONO_SAMPLE_DATA",
    "  MONO_SAMPLE_UNUSED_BITS",
    "  MONO_SAMPLE_UNUSED_BITS_MIN",
    "  MONO_SAMPLE_AUDIO_SHIFT",
    "  STREAM_MONO_SAMPLE_PROCESSED_DATA",
    "  UNICAM_MAX_ABS_VALUE",
    "  UNICAM_MAX_VALUE_USED_BITS",
    "  UNICAM_SHIFT_VALUE",
    "  UNICAM_CODED_SHIFT_VALUE",
    "  UNICAM_CODED_SHIFTS_BYTE",
    "  UNICAM_SAMPLE",
    "  UNICAM_COMPRESSED_SAMPLE",
    "  UNICAM_10_BIT_CODED_SAMPLE",
    "  UNICAM_BLOCKS_CODED",
    "  UNICAM_BYTES_CODED",
    "  SEND_START",
    "  SEND_STOP",
    "* SEND_FAILURE",
    "* SOCKET_GONE_BAD",
    "* SOCKET_ERRORS_FOR_TOO_LONG",
    "* TCP_SEND_TIMEOUT",
    "  SEND_SEQ",
    "  FILE_WRITE_START",
    "  FILE_WRITE_STOP",
    "* FILE_WRITE_FAILURE",
    "* SEND_DURATION_GREATER_THAN_BLOCK_DURATION",
    "  SEND_DURATION",
    "  NEW_PEAK_SEND_DURATION",
    "  NUM_DATAGRAMS_FREE",
    "  NUM_DATAGRAMS_QUEUED",
    "  THROUGHPUT_BITS_S",
    "  TCP_WRITE",
    "  TCP_QUEUELEN",
    "  TCP_SEQ",
    "  TCP_SNDWND",
    "  TCP_CWND",
    "  TCP_WND",
    "  TCP_EFFWND",
    "  TCP_ACK"
};

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise logging
void initLog()
{
    memset (gLog, 0, sizeof(gLog));
    gLogTime.reset();
    gLogTime.start();
}

#ifdef ENABLE_LOG_AS_FUNCTION
// Log an event plus parameter
void LOG(LogEvent event, int parameter)
{
    gpLogNext->timestamp = gLogTime.read_us();
    gpLogNext->event = event;
    gpLogNext->parameter = parameter;
    gpLogNext++;

    if (gNumLogEntries < sizeof (gLog) / sizeof (gLog[0])) {
        gNumLogEntries++;
    }

    if (gpLogNext >= gLog + sizeof (gLog) / sizeof (gLog[0])) {
        gpLogNext = gLog;
    }
}
#endif

// Print out the log
void printLog()
{
    LogEntry * pItem = gpLogNext;

    // Rotate to the start of the log
    for (unsigned int x = 0; x < (sizeof (gLog) / sizeof (gLog[0])) - gNumLogEntries; x++) {
        pItem++;
        if (pItem >= gLog + sizeof (gLog) / sizeof (gLog[0])) {
            pItem = gLog;
        }
    }

    printf ("------------- Log starts -------------\n");
    for (unsigned int x = 0; x < gNumLogEntries; x++) {
        if (pItem->event > sizeof (gLogStrings) / sizeof (gLogStrings[0])) {
            printf("%.3f: out of range event at entry %d (%d when max is %d)\n",
                   (float) pItem->timestamp / 1000, x, pItem->event,
                   sizeof (gLogStrings) / sizeof (gLogStrings[0]));
        } else {
            printf ("%6.3f: %s %d (%#x)\n", (float) pItem->timestamp / 1000,
                    gLogStrings[pItem->event], pItem->parameter, pItem->parameter);
        }
        pItem++;
        if (pItem >= gLog + sizeof (gLog) / sizeof (gLog[0])) {
            pItem = gLog;
        }
    }
    printf ("-------------- Log ends --------------\n");
}
