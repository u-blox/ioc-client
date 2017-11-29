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

#ifndef _IOC_DIAGNOSTICS_
#define _IOC_DIAGNOSTICS__

/* ----------------------------------------------------------------
 * GENERAL TYPES
 * -------------------------------------------------------------- */

// The local version of diagnostics data.
typedef struct {
    unsigned int worstCaseAudioDatagramSendDuration;
    uint64_t averageAudioDatagramSendDuration;
    uint64_t numAudioDatagrams;
    unsigned int numAudioSendFailures;
    unsigned int numAudioDatagramsSendTookTooLong;
    unsigned int numAudioBytesSent;
} DiagnosticsLocal;

/* ----------------------------------------------------------------
 * DIAGNOSTICS M2M C++ OBJECT DEFINITION
 * -------------------------------------------------------------- */
 
/** Diagnostics reporting.
 * Implementation is as a custom object, I have chosen ID urn:oma:lwm2m:x:32771.
 */
class IocM2mDiagnostics : public M2MObjectHelper {
public:

    /** The diagnostics information (with types that match
     * the LWM2M types).
     */
    typedef struct {
        int64_t upTime;
        int64_t resetReason;
        float worstCaseSendDuration;
        float averageSendDuration;
        int64_t minNumDatagramsFree;
        int64_t numSendFailures;
        int64_t percentageSendsTooLong;
    } Diagnostics;

    /** Constructor.
     *
     * @param getCallback callback to get diagnostics information.
     * @param debugOn     true if you want debug prints, otherwise false.
     */
    IocM2mDiagnostics(Callback<bool(Diagnostics *)> getCallback,
                      bool debugOn = false);

    /** Destructor.
     */
    ~IocM2mDiagnostics();

    /** Update the observable resources (using getCallback()).
     */
    void updateObservableResources();

protected:

    /** The resource number for upTime, an On Time
     * resource.
     */
#   define RESOURCE_NUMBER_UP_TIME "5852"

    /** The resource number for resetReason, a Mode
     * resource for the sake of anything better.
     */
#   define RESOURCE_NUMBER_RESET_REASON "5526"

    /** The resource number for worstCaseSendDuration,
     * a Duration resource.
     */
#   define RESOURCE_NUMBER_WORST_CASE_SEND_DURATION "5524"

    /** The resource instance for worstCaseSendDuration.
     */
#   define RESOURCE_INSTANCE_WORST_CASE_SEND_DURATION 0

    /** The resource number for averageDuration, a
     * Duration resource.
     */
#   define RESOURCE_NUMBER_AVERAGE_SEND_DURATION "5524"

    /** The resource instance for averageSendDuration.
     */
#   define RESOURCE_INSTANCE_AVERAGE_SEND_DURATION  1

    /** The resource number for minDatagramsFree, a
     * Down Counter resource.
     */
#   define RESOURCE_NUMBER_MIN_NUM_DATAGRAMS_FREE "5542"

    /** The resource number for numSendFailures, an
     * Up Counter resource.
     */
#   define RESOURCE_NUMBER_NUM_SEND_FAILURES "5541"

    /** The resource number for percentageSendsTooLong,
     * a Percent resource.
     */
#   define RESOURCE_NUMBER_PERCENT_SENDS_TOO_LONG "3320"

    /** Definition of this object.
     */
    static const DefObject _defObject;

    /** Callback to get diagnostics values.
     */
    Callback<bool(Diagnostics *)> _getCallback;
};
 
/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/**  Initialise the diagnostics object.
 *
 * @return  a pointer to the IocM2mDiagnostics object.
 */
IocM2mDiagnostics *pInitDiagnostics();

/**  Shut down the diagnostics object.
 */
void deinitDiagnostics();


/* Reset all diagnostics values.
 */
void resetDiagnostics();

/** Set the start time.
 * @param num the start time (Unix format).
 */
void setStartTime(int num);

/** Get the start time.
 * @return the start time (unix format).
 */
int getStartTime();

/** Set the number of audio bytes sent.
 * @param num the number of audio bytes sent.
 */
void setNumAudioBytesSent(unsigned int num);

/** Get the number of audio bytes sent.
 * @return the number of audio bytes sent.
 */
unsigned int getNumAudioBytesSent();

/** Increment the number of audio send failures.
 */
void incNumAudioSendFailures();

/* Increment the number of audio bytes sent.
 * @param num the amount to increment by.
 */
void incNumAudioBytesSent(unsigned int num);

/* Increment the average audio send duration.
 * @param the amount to increment by.
 */
void incAverageAudioDatagramSendDuration(int64_t num);

/* Increment the number of audio datagrams (sent or not).
 */
void incNumAudioDatagrams();

/* Increment the number of audio datagrams were sending
 * took too long.
 */
void incNumAudioDatagramsSendTookTooLong();

/* Get the worst case audio datagram send duration.
 * @return the worst case send duration.
 */
unsigned int getWorstCaseAudioDatagramSendDuration();

/* Set the worst case audio datagram send duration.
 * @param the worst case send duration.
 */
void setWorstCaseAudioDatagramSendDuration(unsigned int num);

#endif // _IOC_DIAGNOSTICS_

// End of file
