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
#include "MbedCloudClient.h"
#include "m2m_object_helper.h"
#include "urtp.h" // for BLOCK_DURATION_MS
#include "log.h"

#include "ioc_cloud_client_dm.h"
#include "ioc_audio.h"
#include "ioc_utils.h"
#include "ioc_diagnostics.h"

/* This file implements the LWM2M diagnostics object.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The default config data.
#define CONFIG_DEFAULT_INIT_WAKE_UP_TICK_COUNTER_PERIOD     600
#define CONFIG_DEFAULT_INIT_WAKE_UP_TICK_COUNTER_MODULO     3
#define CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_PERIOD_1  60
#define CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_PERIOD_2  600
#define CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_MODULO    60
#define CONFIG_DEFAULT_GNSS_ENABLE                          true

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

static DiagnosticsLocal gDiagnostics = {0};
static Ticker gSecondTicker;
static int gStartTime = 0;
static IocM2mDiagnostics *gpM2mObject = NULL;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: HOOK FOR DIAGNOSTICS M2M C++ OBJECT
 * -------------------------------------------------------------- */

// Callback that gets diagnostic data for the IocM2mDiagnostics object.
static bool getDiagnosticsData(IocM2mDiagnostics::Diagnostics *pData)
{
    if (gStartTime > 0) {
        pData->upTime = time(NULL) - gStartTime;
    } else {
        pData->upTime = 0;
    }
    pData->resetReason = getResetReason();
    pData->worstCaseSendDuration = (float) gDiagnostics.worstCaseAudioDatagramSendDuration / 1000000;
    pData->averageSendDuration = (float) (gDiagnostics.averageAudioDatagramSendDuration /
                                          gDiagnostics.numAudioDatagrams) / 1000000;
    pData->minNumDatagramsFree = getUrtpDatagramsFreeMin();
    pData->numSendFailures = gDiagnostics.numAudioSendFailures;
    pData->percentageSendsTooLong = (int64_t) gDiagnostics.numAudioDatagramsSendTookTooLong * 100 /
                                              gDiagnostics.numAudioDatagrams;

    return true;
}

/* ----------------------------------------------------------------
 * PUBLIC: INITIALISATION
 * -------------------------------------------------------------- */

// Initialise the diagnostics object.
IocM2mDiagnostics *pInitDiagnostics()
{
    gpM2mObject = new IocM2mDiagnostics(getDiagnosticsData, MBED_CONF_APP_OBJECT_DEBUG_ON);
    return gpM2mObject;
}

// Shut down diagnostics object.
void deinitDiagnostics()
{
    delete gpM2mObject;
    gpM2mObject = NULL;

    if (gDiagnostics.numAudioDatagrams > 0) {
        printf("Stats:\n");
        printf("Worst case time to perform a send: %u us.\n", gDiagnostics.worstCaseAudioDatagramSendDuration);
        printf("Average time to perform a send: %llu us.\n", gDiagnostics.averageAudioDatagramSendDuration /
                                                             gDiagnostics.numAudioDatagrams);
        printf("Minimum number of datagram(s) free %d.\n", getUrtpDatagramsFreeMin());
        printf("Number of send failure(s) %d.\n", gDiagnostics.numAudioSendFailures);
        printf("%d send(s) took longer than %d ms (%llu%% of the total).\n",
               gDiagnostics.numAudioDatagramsSendTookTooLong,
               BLOCK_DURATION_MS, (uint64_t) gDiagnostics.numAudioDatagramsSendTookTooLong * 100 /
                                             gDiagnostics.numAudioDatagrams);
    }
}

// Reset all diagnostics values.
void resetDiagnostics()
{
    memset(&gDiagnostics, 0, sizeof (gDiagnostics));
}

// Set the start time.
void setStartTime(int num)
{
    gStartTime = num;
}

// Get the start time.
int getStartTime()
{
    return gStartTime;
}

// Set the number of audio bytes sent.
void setNumAudioBytesSent(unsigned int num)
{
    gDiagnostics.numAudioBytesSent = num;
}

// Get the number of audio bytes sent.
unsigned int getNumAudioBytesSent()
{
    return gDiagnostics.numAudioBytesSent;
}

// Increment the number of audio send failures.
void incNumAudioSendFailures()
{
    gDiagnostics.numAudioSendFailures++;
}

// Increment the number of audio bytes sent.
void incNumAudioBytesSent(unsigned int num)
{
    gDiagnostics.numAudioBytesSent += num;
}

// Increment the audio send duration.
void incAverageAudioDatagramSendDuration(int64_t num)
{
    gDiagnostics.averageAudioDatagramSendDuration += num;
}

// Increment the number of audio datagrams.
void incNumAudioDatagrams()
{
    gDiagnostics.numAudioDatagrams++;
}

// Increment the number of occasions when sending an audio
// datagram took too long.
void incNumAudioDatagramsSendTookTooLong()
{
    gDiagnostics.numAudioDatagramsSendTookTooLong++;
}

// Get the worst case audio datagram send duration.
unsigned int getWorstCaseAudioDatagramSendDuration()
{
    return gDiagnostics.worstCaseAudioDatagramSendDuration;
}

// Set the worst case audio datagram send duration.
void setWorstCaseAudioDatagramSendDuration(unsigned int num)
{
    gDiagnostics.worstCaseAudioDatagramSendDuration = num;
}

/* ----------------------------------------------------------------
 * PUBLIC: DIAGNOSTICS M2M C++ OBJECT
 * -------------------------------------------------------------- */

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const M2MObjectHelper::DefObject IocM2mDiagnostics::_defObject =
    {0, "32771", 7,
        -1, RESOURCE_NUMBER_UP_TIME, "on time", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_RESET_REASON, "reset reason", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        RESOURCE_INSTANCE_WORST_CASE_SEND_DURATION, RESOURCE_NUMBER_WORST_CASE_SEND_DURATION, "duration", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, NULL,
        RESOURCE_INSTANCE_AVERAGE_SEND_DURATION, RESOURCE_NUMBER_AVERAGE_SEND_DURATION, "duration", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_MIN_NUM_DATAGRAMS_FREE, "down counter", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_NUM_SEND_FAILURES, "up counter", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_PERCENT_SENDS_TOO_LONG, "percent", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL
    };

// Constructor.
IocM2mDiagnostics::IocM2mDiagnostics(Callback<bool(Diagnostics *)> getCallback,
                                     bool debugOn)
                  :M2MObjectHelper(&_defObject, NULL, NULL, debugOn)
{
    _getCallback = getCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Update the values held in the resources
    updateObservableResources();

    printf("IocM2mDiagnostics: object initialised.\n");
}

// Destructor.
IocM2mDiagnostics::~IocM2mDiagnostics()
{
}

// Update the observable data for this object.
void IocM2mDiagnostics::updateObservableResources()
{
    Diagnostics data;

    // Update the data
    if (_getCallback) {
        if (_getCallback(&data)) {
            // Set the values in the resources based on the new data
            MBED_ASSERT(setResourceValue(data.upTime, RESOURCE_NUMBER_UP_TIME));
            MBED_ASSERT(setResourceValue(data.resetReason, RESOURCE_NUMBER_RESET_REASON));
            MBED_ASSERT(setResourceValue(data.worstCaseSendDuration,
                                         RESOURCE_NUMBER_WORST_CASE_SEND_DURATION,
                                         RESOURCE_INSTANCE_WORST_CASE_SEND_DURATION));
            MBED_ASSERT(setResourceValue(data.averageSendDuration,
                                         RESOURCE_NUMBER_AVERAGE_SEND_DURATION,
                                         RESOURCE_INSTANCE_AVERAGE_SEND_DURATION));
            MBED_ASSERT(setResourceValue(data.minNumDatagramsFree, RESOURCE_NUMBER_MIN_NUM_DATAGRAMS_FREE));
            MBED_ASSERT(setResourceValue(data.numSendFailures, RESOURCE_NUMBER_NUM_SEND_FAILURES));
            MBED_ASSERT(setResourceValue(data.percentageSendsTooLong, RESOURCE_NUMBER_PERCENT_SENDS_TOO_LONG));
        }
    }
}

// End of file
