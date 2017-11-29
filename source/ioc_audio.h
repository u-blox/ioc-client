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

#ifndef _IOC_AUDIO_
#define _IOC_AUDIO_

/* ----------------------------------------------------------------
 * GENERAL TYPES
 * -------------------------------------------------------------- */

// Union of socket types.
typedef union {
    TCPSocket *pTcpSock;
    UDPSocket *pUdpSock;
} SocketPointerUnion;

// Local version of audio parameters.
typedef struct {
    bool streamingEnabled;
    int duration;  ///< -1 = no limit.
    int fixedGain; ///< -1 = use automatic gain.
    int socketMode; // Either COMMS_TCP or COMMS_UDP
    String audioServerUrl;
    SocketPointerUnion sock;
    SocketAddress server;
} AudioLocal;

/* ----------------------------------------------------------------
 * AUDIO M2M C++ OBJECT DEFINITION
 * -------------------------------------------------------------- */

/** Control for the audio stream.
 * Implementation is as a custom object, I have chosen
 * ID urn:oma:lwm2m:x:32770, with writable resources.
 */
class IocM2mAudio : public M2MObjectHelper {
public:

    /** The audio communication mode options.
     */
    typedef enum {
        AUDIO_COMMUNICATIONS_MODE_UDP,
        AUDIO_COMMUNICATIONS_MODE_TCP,
        MAX_NUM_AUDIO_COMMUNICATIONS_MODES
    } AudioCommunicationsMode;

    /** The audio control parameters (with
     * types that match the LWM2M types).
     */
    typedef struct {
        bool streamingEnabled;
        float duration;  ///< -1 = no limit.
        float fixedGain; ///< -1 = use automatic gain.
        int64_t audioCommunicationsMode; ///< valid values are
                                         /// those from the
                                         /// AudioCommunicationsMode
                                         /// enum (this has to be
                                         /// an int64_t as it is an
                                         /// integer type).
        String audioServerUrl;
    } Audio;

    /** Constructor.
     *
     * @param pSetCallback                 callback to set the audio
     *                                     parameter values.
     * @param pGetStreamingEnabledCallback since enabling streaming
     *                                     can fail, the Boolean value
     *                                     is observable through this
     *                                     callback.
     * @param pInitialValues               the initial state of the audio
     *                                     parameter values.
     * @param debugOn                      true if you want debug prints,
     *                                     otherwise false.
     */
    IocM2mAudio(Callback<void(const Audio *)> pSetCallback,
                Callback<bool(bool *)> pGetStreamingEnabledCallback,
                Audio *pInitialValues,
                bool debugOn = false);

    /** Destructor.
     */
    ~IocM2mAudio();

    /** Callback for when the object is updated, which
     * will set the local variables using setCallback().
     *
     * @param resourceName the resource that was updated.
     */
    void objectUpdated(const char *pResourceName);

    /** Update the observable resources (using getCallback()).
     */
    void updateObservableResources();

protected:

    /** The resource number for streamingEnabled,
     * a Boolean resource.
     */
#   define RESOURCE_NUMBER_STREAMING_ENABLED "5850"

    /** The resource number for duration,
     * a Duration resource.
     */
#   define RESOURCE_NUMBER_DURATION "5524"

    /** The resource number for fixedGain,
     *  a Level resource.
     */
#   define RESOURCE_NUMBER_FIXED_GAIN "5548"

    /** The resource number for audioCommunicationsMode,
     * a Mode resource.
     */
#   define RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE "5526"

    /** The resource number for audioServerUrl,
     * a Text resource.
     */
#   define RESOURCE_NUMBER_AUDIO_SERVER_URL "5527"

    /** Definition of this object.
     */
    static const DefObject _defObject;

    /** Callback to set audio parmeters.
     */
    Callback<void(const Audio *)> _pSetCallback;

    /** Callback to obtain the streaming enabled state.
     */
    Callback<bool(bool *)> _pGetStreamingEnabledCallback;
};

/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Initialise audio.
 *
 * @return  a pointer to the IocM2mAudio object.
 *          IMPORTANT: it is up to the caller to delete this
 *          object when done.
 */
IocM2mAudio *pInitAudio();

/** Shut down audio.
 */
void deinitAudio();

/** Determing if audio streaming is enabled.
 * @return true if audio streaming is enabled else false.
 */
bool isAudioStreamingEnabled();


/** Get the minimum number of URTP datagrams that are free.
 * @return the low water mark of free datagrams.
 */
int getUrtpDatagramsFreeMin();

#endif // _IOC_AUDIO_

// End of file
