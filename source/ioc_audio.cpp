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
#include "I2S.h"
#include "urtp.h"
#include "MbedCloudClient.h"
#include "m2m_object_helper.h"
#include "low_power.h"
#include "log.h"

#include "ioc_cloud_client_dm.h"
#include "ioc_diagnostics.h"
#include "ioc_network.h"
#include "ioc_audio.h"
#include "ioc_dynamics.h"
#include "ioc_utils.h"

/* This file contains the LWM2M audio object plus all the
 * I2S audio sample acquisition and audio streaming functionality.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The possible communications modes (for audio streaming).
#define COMMS_TCP 1
#define COMMS_UDP 0

// Length of one TCP packet, must be at least
// URTP_DATAGRAM_SIZE, best is if it is a multiple
// of URTP_DATAGRAM_SIZE that fits within a sensible
// TCP packet size
#ifdef TCP_MSS
# define TCP_BUFFER_LENGTH TCP_MSS
#else
# define TCP_BUFFER_LENGTH URTP_DATAGRAM_SIZE * 4
#endif

// A signal to indicate that an audio datagram is ready to send.
#define SIG_DATAGRAM_READY 0x01

// The maximum amount of time allowed to send a
// datagram of audio over TCP.
#define AUDIO_TCP_SEND_TIMEOUT_MS 1500

// If we've had consecutive socket errors on the
// audio streaming socket for this long, it's gone bad.
#define AUDIO_MAX_DURATION_SOCKET_ERRORS_MS 1000

// The audio send data task will run anyway this interval,
// necessary in order to terminate it in an orderly fashion.
#define AUDIO_SEND_DATA_RUN_ANYWAY_TIME_MS 1000

// The maximum length of an audio server URL (including
// terminator).
#define AUDIO_MAX_LEN_SERVER_URL 128

// The default audio setup data.
#define AUDIO_DEFAULT_STREAMING_ENABLED  false
#define AUDIO_DEFAULT_DURATION           -1
#define AUDIO_DEFAULT_FIXED_GAIN         -1
#define AUDIO_DEFAULT_COMMUNICATION_MODE COMMS_TCP
#define AUDIO_DEFAULT_SERVER_URL         "ciot.it-sgn.u-blox.com:5065"

/* ----------------------------------------------------------------
 * CALLBACK FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

static void datagramReadyCb(const char * datagram);
static void datagramOverflowStartCb();
static void datagramOverflowStopCb(int numOverflows);

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Audio comms parameters.
BACKUP_SRAM
static AudioLocal gAudioLocalPending;
static AudioLocal gAudioLocalActive;

// Thread required to run the I2S driver event queue.
static Thread *gpI2sTask = NULL;

// Function that forms the body of the gI2sTask.
static const Callback<void()> gI2STaskCallback(&I2S::i2s_bh_queue, &events::EventQueue::dispatch_forever);

// For monitoring progress.
static Ticker gSecondTicker;

// Audio buffer, enough for two blocks of stereo audio,
// where each sample takes up 64 bits (32 bits for L channel
// and 32 bits for R channel).
// Note: can't be in CCMRAM as DMA won't reach there.
static uint32_t gRawAudio[(SAMPLES_PER_BLOCK * 2) * 2];

// Datagram storage for URTP.
__attribute__ ((section ("CCMRAM")))
static char gDatagramStorage[URTP_DATAGRAM_STORE_SIZE];

// Buffer that holds one TCP packet.
static char gTcpBuffer[TCP_BUFFER_LENGTH];
static char *gpTcpBuffer = gTcpBuffer;

// Task to send data off to the audio streaming server.
static Thread *gpSendTask = NULL;

// The URTP codec.
static Urtp gUrtp(&datagramReadyCb, &datagramOverflowStartCb, &datagramOverflowStopCb);

// Flag to indicate that the audio comms channel is up.
static volatile bool gAudioCommsConnected = false;

// The microphone.
static I2S gMic(PB_15, PB_10, PB_9);

// The LWM2M object
static IocM2mAudio *gpM2mObject = NULL;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: URTP CODEC AND ITS CALLBACK FUNCTIONS
 * -------------------------------------------------------------- */

// Callback for when an audio datagram is ready for sending.
static void datagramReadyCb(const char *pDatagram)
{
    if (gpSendTask != NULL) {
        // Send the signal to the sending task
        gpSendTask->signal_set(SIG_DATAGRAM_READY);
    }
}

// Callback for when the audio datagram list starts to overflow.
static void datagramOverflowStartCb()
{
    event();
}

// Callback for when the audio datagram list stops overflowing.
static void datagramOverflowStopCb(int numOverflows)
{
    notEvent();
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: AUDIO CONNECTION
 * -------------------------------------------------------------- */

// Monitor on a 1 second tick.
// This is a ticker call-back, so nothing heavy please.
static void audioMonitor()
{
    // Monitor throughput
    if (getNumAudioBytesSent() > 0) {
        LOG(EVENT_THROUGHPUT_BITS_S, getNumAudioBytesSent() << 3);
        setNumAudioBytesSent(0);
        LOG(EVENT_NUM_DATAGRAMS_QUEUED, gUrtp.getUrtpDatagramsAvailable());
    }
}

// Get the address portion of a URL, leaving off the port number etc.
static void getAddressFromUrl(const char * pUrl, char * pAddressBuf, int lenBuf)
{
    const char * pPortPos;
    int lenUrl;

    if (lenBuf > 0) {
        // Check for the presence of a port number
        pPortPos = strchr(pUrl, ':');
        if (pPortPos != NULL) {
            // Length wanted is up to and including the ':'
            // (which will be overwritten with the terminator)
            if (lenBuf > pPortPos - pUrl + 1) {
                lenBuf = pPortPos - pUrl + 1;
            }
        } else {
            // No port number, take the whole thing
            // including the terminator
            lenUrl = strlen (pUrl);
            if (lenBuf > lenUrl + 1) {
                lenBuf = lenUrl + 1;
            }
        }
        memcpy (pAddressBuf, pUrl, lenBuf);
        *(pAddressBuf + lenBuf - 1) = 0;
    }
}

// Get the port number from the end of a URL.
static bool getPortFromUrl(const char * pUrl, int *port)
{
    bool success = false;
    const char * pPortPos = strchr(pUrl, ':');

    if (pPortPos != NULL) {
        *port = atoi(pPortPos + 1);
        success = true;
    }

    return success;
}

// Start the audio streaming connection.
// This will set up the pAudio structure.
// Note: here be multiple return statements.
static bool startAudioStreamingConnection(AudioLocal *pAudio)
{
    char *pBuf = new char[AUDIO_MAX_LEN_SERVER_URL];
    int port;
    nsapi_error_t nsapiError;
    const int setOption = 1;

    flash();
    LOG(EVENT_AUDIO_STREAMING_CONNECTION_START, 0);
    printf("Resolving IP address of the audio streaming server...\n");
    if (!isNetworkConnected()) {
        bad();
        LOG(EVENT_AUDIO_STREAMING_CONNECTION_START_FAILURE, 0);
        printf("Error, network is not ready.\n");
        return false;
    } else {
        getAddressFromUrl(pAudio->audioServerUrl.c_str(), pBuf, AUDIO_MAX_LEN_SERVER_URL);
        printf("Looking for server URL \"%s\"...\n", pBuf);
        LOG(EVENT_DNS_LOOKUP, 0);
        if (pGetNetworkInterface()->gethostbyname(pBuf, &pAudio->server) == 0) {
            printf("Found it at IP address %s.\n", pAudio->server.get_ip_address());
            if (getPortFromUrl(pAudio->audioServerUrl.c_str(), &port)) {
                pAudio->server.set_port(port);
                printf("Audio server port set to %d.\n", pAudio->server.get_port());
            } else {
                printf("WARNING: no port number was specified in the audio server URL (\"%s\").\n",
                       pAudio->audioServerUrl.c_str());
            }
        } else {
            bad();
            LOG(EVENT_DNS_LOOKUP_FAILURE, 0);
            LOG(EVENT_AUDIO_STREAMING_CONNECTION_START_FAILURE, 1);
            printf("Error, couldn't resolve IP address of audio streaming server.\n");
            return false;
        }
    }

    flash();
    printf("Opening socket to server for audio comms...\n");
    switch (pAudio->socketMode) {
        case COMMS_TCP:
            pAudio->sock.pTcpSock = new TCPSocket();
            LOG(EVENT_SOCKET_OPENING, 0);
            nsapiError = pAudio->sock.pTcpSock->open(pGetNetworkInterface());
            if (nsapiError != NSAPI_ERROR_OK) {
                bad();
                LOG(EVENT_SOCKET_OPENING_FAILURE, nsapiError);
                printf("Could not open TCP socket to audio streaming server (error %d).\n", nsapiError);
                return false;
            } else {
                LOG(EVENT_SOCKET_OPENED, 0);
                pAudio->sock.pTcpSock->set_timeout(1000);
                LOG(EVENT_TCP_CONNECTING, 0);
                printf("Connecting TCP...\n");
                nsapiError = pAudio->sock.pTcpSock->connect(pAudio->server);
                if (nsapiError != NSAPI_ERROR_OK) {
                    bad();
                    LOG(EVENT_TCP_CONNECT_FAILURE, nsapiError);
                    printf("Could not connect TCP socket (error %d).\n", nsapiError);
                    return false;
                } else {
                    LOG(EVENT_TCP_CONNECTED, 0);
                    printf("Setting TCP_NODELAY in TCP socket options...\n");
                    // Set TCP_NODELAY (1) in level IPPROTO_TCP (6) to 1
                    nsapiError = pAudio->sock.pTcpSock->setsockopt(6, 1, &setOption, sizeof(setOption));
                    if (nsapiError != NSAPI_ERROR_OK) {
                        bad();
                        LOG(EVENT_TCP_CONFIGURATION_FAILURE, nsapiError);
                        printf("Could not set TCP socket options (error %d).\n", nsapiError);
                        return false;
                    }
                    LOG(EVENT_TCP_CONFIGURED, 0);
                }
            }
            break;
        case COMMS_UDP:
            pAudio->sock.pUdpSock = new UDPSocket();
            nsapiError = pAudio->sock.pUdpSock->open(pGetNetworkInterface());
            LOG(EVENT_SOCKET_OPENING, 0);
            if (nsapiError != NSAPI_ERROR_OK) {
                bad();
                LOG(EVENT_SOCKET_OPENING_FAILURE, nsapiError);
                printf("Could not open UDP socket to audio streaming server (error %d).\n", nsapiError);
                return false;
            }
            LOG(EVENT_SOCKET_OPENED, 0);
            pAudio->sock.pUdpSock->set_timeout(1000);
            break;
        default:
            bad();
            printf("Unknown audio communications mode (%d).\n", pAudio->socketMode);
            return false;
            break;
    }

    gAudioCommsConnected = true;

    return true;
}

// Stop the audio streaming connection.
static void stopAudioStreamingConnection(AudioLocal *pAudio)
{
    flash();
    LOG(EVENT_AUDIO_STREAMING_CONNECTION_STOP, 0);
    printf("Closing audio server socket...\n");
    switch (pAudio->socketMode) {
        case COMMS_TCP:
            // No need to close() the socket,
            // the destructor does that.
            if (pAudio->sock.pTcpSock) {
                delete pAudio->sock.pTcpSock;
                pAudio->sock.pTcpSock = NULL;
            }
            break;
        case COMMS_UDP:
            // No need to close() the socket,
            // the destructor does that.
            if (pAudio->sock.pUdpSock) {
                delete pAudio->sock.pUdpSock;
                pAudio->sock.pUdpSock = NULL;
            }
            break;
        default:
            bad();
            printf("Unknown audio communications mode (%d).\n", pAudio->socketMode);
            break;
    }

    gAudioCommsConnected = false;
}

// Send a buffer of data over a TCP socket
static int tcpSend(TCPSocket * pSock, const char * pData, int size)
{
    int x = 0;
    int count = 0;
    Timer timer;

    timer.start();
    while ((count < size) && (timer.read_ms() < AUDIO_TCP_SEND_TIMEOUT_MS)) {
        x = pSock->send(pData + count, size - count);
        if (x > 0) {
            count += x;
        }
    }
    timer.stop();

    if (count < size) {
        LOG(EVENT_TCP_SEND_TIMEOUT, size - count);
    }

    if (x < 0) {
        count = x;
    }

    return count;
}

// The send function that forms the body of the send task.
// This task runs whenever there is an audio datagram ready
// to send.
static void sendAudioData(const AudioLocal * pAudioLocal)
{
    const char * pUrtpDatagram = NULL;
    Timer sendDurationTimer;
    Timer badSendDurationTimer;
    unsigned int duration;
    int retValue;
    bool okToDelete = false;

    while (gAudioCommsConnected) {
        // Wait for at least one datagram to be ready to send
        Thread::signal_wait(SIG_DATAGRAM_READY, AUDIO_SEND_DATA_RUN_ANYWAY_TIME_MS);

        while ((pUrtpDatagram = gUrtp.getUrtpDatagram()) != NULL) {
            okToDelete = false;
            sendDurationTimer.reset();
            sendDurationTimer.start();
            // Send the datagram
            if (gAudioCommsConnected) {
                //LOG(EVENT_SEND_START, (int) pUrtpDatagram);
                if (pAudioLocal->socketMode == COMMS_TCP) {
                    // For TCP, assemble the datagrams
                    // into a whole packet before sending
                    // for maximum efficiency
                    memcpy (gpTcpBuffer, pUrtpDatagram, URTP_DATAGRAM_SIZE);
                    gpTcpBuffer += URTP_DATAGRAM_SIZE;
                    retValue = URTP_DATAGRAM_SIZE;
                    if (gpTcpBuffer >= gTcpBuffer + sizeof(gTcpBuffer)) {
                        gpTcpBuffer = gTcpBuffer;
                        retValue = tcpSend(pAudioLocal->sock.pTcpSock, pUrtpDatagram, URTP_DATAGRAM_SIZE);
                    }
                } else {
                    retValue = pAudioLocal->sock.pUdpSock->sendto(pAudioLocal->server, pUrtpDatagram, URTP_DATAGRAM_SIZE);
                }
                if (retValue != URTP_DATAGRAM_SIZE) {
                    badSendDurationTimer.start();
                    LOG(EVENT_SEND_FAILURE, retValue);
                    bad();
                    incNumAudioSendFailures();
                } else {
                    incNumAudioBytesSent(retValue);
                    okToDelete = true;
                    badSendDurationTimer.stop();
                    badSendDurationTimer.reset();
                    toggleGreen();
                }
                //LOG(EVENT_SEND_STOP, (int) pUrtpDatagram);

                if (retValue < 0) {
                    // If the connection has gone, set a flag that will be picked up outside this function and
                    // cause us to start again
                    if (badSendDurationTimer.read_ms() > AUDIO_MAX_DURATION_SOCKET_ERRORS_MS) {
                        LOG(EVENT_SOCKET_ERRORS_FOR_TOO_LONG, badSendDurationTimer.read_ms());
                        badSendDurationTimer.stop();
                        badSendDurationTimer.reset();
                        bad();
                        gAudioCommsConnected = false;
                    }
                    if ((retValue == NSAPI_ERROR_NO_CONNECTION) ||
                        (retValue == NSAPI_ERROR_CONNECTION_LOST) ||
                        (retValue == NSAPI_ERROR_NO_SOCKET)) {
                        LOG(EVENT_SOCKET_BAD, retValue);
                        bad();
                        gAudioCommsConnected = false;
                    }
                }
            }

            sendDurationTimer.stop();
            duration = sendDurationTimer.read_us();
            incAverageAudioDatagramSendDuration(duration);
            incNumAudioDatagrams();

            if (duration > BLOCK_DURATION_MS * 1000) {
                // If this is UDP then it's serious, if it's TCP then
                // we can catch up.
                if (pAudioLocal->socketMode == COMMS_UDP) {
                    LOG(EVENT_SEND_DURATION_GREATER_THAN_BLOCK_DURATION, duration);
                }
                incNumAudioDatagramsSendTookTooLong();
            } else {
                //LOG(EVENT_SEND_DURATION, duration);
            }
            if (duration > getWorstCaseAudioDatagramSendDuration()) {
                setWorstCaseAudioDatagramSendDuration(duration);
                LOG(EVENT_NEW_PEAK_SEND_DURATION, duration);
            }

            if (okToDelete) {
                gUrtp.setUrtpDatagramAsRead(pUrtpDatagram);
            }
        }
    }
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: I2S INTERFACE
 * -------------------------------------------------------------- */

// Callback for I2S events.
//
// We get here when the DMA has either half-filled the gRawAudio
// buffer (so one 20 ms block) or completely filled it (two 20 ms
// blocks), or if an error has occurred.  We can use this as a
// double buffer.
static void i2sEventCallback (int arg)
{
    if (arg & I2S_EVENT_RX_HALF_COMPLETE) {
        //LOG(EVENT_I2S_DMA_RX_HALF_FULL, 0);
        gUrtp.codeAudioBlock(gRawAudio);
    } else if (arg & I2S_EVENT_RX_COMPLETE) {
        //LOG(EVENT_I2S_DMA_RX_FULL, 0);
        gUrtp.codeAudioBlock(gRawAudio + (sizeof (gRawAudio) / sizeof (gRawAudio[0])) / 2);
    } else {
        LOG(EVENT_I2S_DMA_UNKNOWN, arg);
        bad();
        printf("Unexpected event mask 0x%08x.\n", arg);
    }
}

// Initialise the I2S interface and begin reading from it.
//
// The ICS43434 microphone outputs 24 bit words in a 64 bit frame,
// with the LR pin dictating whether the word appears in the first
// 32 bits (LR = 0, left channel, WS low) or the second 32 bits
// (LR = 1, right channel, WS high).  Each data bit is valid on the
// rising edge of SCK and the MSB of the data word is clocked out on the
// second clock edge after WS changes, as follows:
//      ___                                 ______________________   ___
// WS      \____________...________..._____/                      ...   \______
//          0   1   2       23  24      31  32  33  34     55  56     63
// SCK  ___   _   _   _       _   _      _   _   _   _       _   _      _   _
//         \_/ \_/ \_/ \...\_/ \_/ ...\_/ \_/ \_/ \_/ \...\_/ \_/ ...\_/ \_/ \_
//
// SD   ________--- ---     --- --- ___________--- ---     --- ---_____________
//              --- --- ... --- ---            --- --- ... --- ---
//              23  22       1   0             23  22       1   0
//              Left channel data              Right channel data
//
// This is known as the Philips protocol (24-bit frame with CPOL = 0 to read
// the data on the rising edge).
static bool startI2s(I2S * pI2s)
{
    bool success = false;

    flash();
    LOG(EVENT_I2S_START, 0);
    printf("Starting I2S...\n");
    if ((pI2s->protocol(PHILIPS) == 0) &&
        (pI2s->mode(MASTER_RX, true) == 0) &&
        (pI2s->format(24, 32, 0) == 0) &&
        (pI2s->audio_frequency(SAMPLING_FREQUENCY) == 0)) {
        if (gpI2sTask == NULL) {
            gpI2sTask = new Thread();
        }
        if (gpI2sTask->start(gI2STaskCallback) == osOK) {
            if (pI2s->transfer((void *) NULL, 0,
                               (void *) gRawAudio, sizeof (gRawAudio),
                               event_callback_t(&i2sEventCallback),
                               I2S_EVENT_ALL) == 0) {
                success = true;
                printf("I2S started.\n");
            } else {
                bad();
                LOG(EVENT_I2S_START_FAILURE, 2);
                printf("Unable to start I2S transfer.\n");
            }
        } else {
            bad();
            LOG(EVENT_I2S_START_FAILURE, 1);
            printf("Unable to start I2S thread.\n");
        }
    } else {
        bad();
        LOG(EVENT_I2S_START_FAILURE, 0);
        printf("Unable to start I2S driver.\n");
    }

    return success;
}

// Stop the I2S interface.
static void stopI2s(I2S * pI2s)
{
    flash();
    LOG(EVENT_I2S_STOP, 0);
    printf("Stopping I2S...\n");
    pI2s->abort_all_transfers();
    if (gpI2sTask != NULL) {
       gpI2sTask->terminate();
       gpI2sTask->join();
       delete gpI2sTask;
       gpI2sTask = NULL;
    }
    printf("I2S stopped.\n");
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: AUDIO CONTROL
 * -------------------------------------------------------------- */

// Stop audio streaming.
static void stopStreaming(AudioLocal *pAudioLocal)
{
    stopI2s(&gMic);

    // Wait for any on-going transmissions to complete
    wait_ms(2000);

    flash();
    LOG(EVENT_AUDIO_STREAMING_STOP, 0);
    printf ("Stopping audio send task...\n");
    gpSendTask->terminate();
    gpSendTask->join();
    delete gpSendTask;
    gpSendTask = NULL;
    good();  // Make sure the green LED stays on at
             // the end as it will have been
             // toggling throughout
    printf ("Audio send task stopped.\n");

    stopAudioStreamingConnection(pAudioLocal);

    gSecondTicker.detach();

    printf("Audio streaming stopped.\n");
    pAudioLocal->streamingEnabled = false;
}

// Start audio streaming.
// Note: here be multiple return statements.
static bool startStreaming(AudioLocal *pAudioLocal)
{
    int retValue;

    // Start the per-second monitor tick and reset the diagnostics
    LOG(EVENT_AUDIO_STREAMING_START, 0);
    gSecondTicker.attach_us(callback(&audioMonitor), 1000000);
    resetDiagnostics();

    if (!startAudioStreamingConnection(pAudioLocal)) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 0);
        return false;
    }

    flash();
    printf ("Setting up URTP...\n");
    if (!gUrtp.init((void *) &gDatagramStorage, pAudioLocal->fixedGain)) {
        bad();
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 1);
        printf ("Unable to start URTP.\n");
        return false;
    }

    flash();
    printf ("Starting task to send audio data...\n");
    if (gpSendTask == NULL) {
        gpSendTask = new Thread();
    }
    retValue = gpSendTask->start(callback(sendAudioData, pAudioLocal));
    if (retValue != osOK) {
        bad();
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 2);
        printf ("Error starting task (%d).\n", retValue);
        return false;
    }

    if (!startI2s(&gMic)) {
        LOG(EVENT_AUDIO_STREAMING_START_FAILURE, 3);
        return false;
    }

    printf("Now streaming audio.\n");

    pAudioLocal->streamingEnabled = true;
    if (pAudioLocal->duration >= 0) {
        printf("Audio streaming will stop in %d second(s).\n", pAudioLocal->duration);
        pGetEventQueue()->call_in(pAudioLocal->duration * 1000, &stopStreaming, pAudioLocal);
    }

    return pAudioLocal->streamingEnabled;
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: HOOKS FOR AUDIO M2M C++ OBJECT
 * -------------------------------------------------------------- */

 // Callback that sets the configuration data via the IocM2mAudio
// object.
static void setAudioData(const IocM2mAudio::Audio *pM2mAudio)
{
    bool streamingWasEnabled = gAudioLocalPending.streamingEnabled;

    // Something has happened, tell Ready mode about it
    readyModeInstructionReceived();

    printf("Received new audio parameters:\n");
    printf("  streamingEnabled %d.\n", pM2mAudio->streamingEnabled);
    printf("  duration %f.\n", pM2mAudio->duration);
    printf("  fixedGain %f.\n", pM2mAudio->fixedGain);
    printf("  audioCommunicationsMode %lld.\n", pM2mAudio->audioCommunicationsMode);
    printf("  audioServerUrl \"%s\".\n", pM2mAudio->audioServerUrl.c_str());

    gAudioLocalPending.streamingEnabled = pM2mAudio->streamingEnabled;
    gAudioLocalPending.fixedGain = (int) pM2mAudio->fixedGain;
    gAudioLocalPending.duration = (int) pM2mAudio->duration;
    gAudioLocalPending.socketMode = pM2mAudio->audioCommunicationsMode;
    gAudioLocalPending.audioServerUrl = pM2mAudio->audioServerUrl;
    LOG(EVENT_SET_AUDIO_CONFIG_FIXED_GAIN, gAudioLocalPending.fixedGain);
    LOG(EVENT_SET_AUDIO_CONFIG_DURATION, gAudioLocalPending.duration);
    LOG(EVENT_SET_AUDIO_CONFIG_COMUNICATIONS_MODE, gAudioLocalPending.socketMode);
    if (pM2mAudio->streamingEnabled && !streamingWasEnabled) {
        LOG(EVENT_SET_AUDIO_CONFIG_STREAMING_ENABLED, 0);
        // Make a copy of the current audio settings so that
        // the streaming process cannot be affected by server writes
        // unless it is switched off and on again
        gAudioLocalActive = gAudioLocalPending;
        gAudioLocalPending.streamingEnabled = startStreaming(&gAudioLocalActive);
    } else if (!pM2mAudio->streamingEnabled && streamingWasEnabled) {
        LOG(EVENT_SET_AUDIO_CONFIG_STREAMING_DISABLED, 0);
        stopStreaming(&gAudioLocalActive);
        gAudioLocalPending.streamingEnabled = gAudioLocalActive.streamingEnabled;
        // Call this to update the diagnostics straight away as they will have been
        // modified during the streaming session
        cloudClientObjectUpdate();
    }
}

// Callback that retrieves the state of streamingEnabled
static bool getStreamingEnabled(bool *pStreamingEnabled)
{
    *pStreamingEnabled = gAudioLocalActive.streamingEnabled;

    return true;
}

// Convert a local audio data structure to the IocM2mAudio one.
static IocM2mAudio::Audio *pConvertAudioLocalToM2m (IocM2mAudio::Audio *pM2m, const AudioLocal *pLocal)
{
    pM2m->streamingEnabled = pLocal->streamingEnabled;
    pM2m->duration = (float) pLocal->duration;
    pM2m->fixedGain = (float) pLocal->fixedGain;
    pM2m->audioCommunicationsMode = pLocal->socketMode;
    pM2m->audioServerUrl = pLocal->audioServerUrl;

    return pM2m;
}

/* ----------------------------------------------------------------
 * PUBLIC: INITIALISATION
 * -------------------------------------------------------------- */

// Initialise audio.
IocM2mAudio *pInitAudio()
{
    IocM2mAudio::Audio *pTempStore = new IocM2mAudio::Audio;
    
    // Set up defaults
    gAudioLocalPending.streamingEnabled = AUDIO_DEFAULT_STREAMING_ENABLED;
    gAudioLocalPending.duration = AUDIO_DEFAULT_DURATION;
    gAudioLocalPending.fixedGain = AUDIO_DEFAULT_FIXED_GAIN;
    gAudioLocalPending.socketMode = AUDIO_DEFAULT_COMMUNICATION_MODE;
    gAudioLocalPending.audioServerUrl = AUDIO_DEFAULT_SERVER_URL;
    gAudioLocalPending.sock.pTcpSock = NULL;

    // Add the object to the global collection
    gpM2mObject = new IocM2mAudio(setAudioData,
                                 getStreamingEnabled,
                                 pConvertAudioLocalToM2m(pTempStore, &gAudioLocalPending),
                                 MBED_CONF_APP_OBJECT_DEBUG_ON);
    delete pTempStore;
    
    return gpM2mObject;
}

// Shut down audio.
void deinitAudio()
{
    if (gAudioLocalActive.streamingEnabled) {
        flash();
        printf("Stopping streaming...\n");
        stopStreaming(&gAudioLocalActive);
        gAudioLocalPending.streamingEnabled = gAudioLocalActive.streamingEnabled;
    }

    delete gpM2mObject;
    gpM2mObject = NULL;
}

// Determine if audio streaming is enabled.
bool isAudioStreamingEnabled()
{
    return gAudioLocalActive.streamingEnabled;
}

// Get the minimum number of URTP datagrams that are
// free.
int getUrtpDatagramsFreeMin()
{
    return gUrtp.getUrtpDatagramsFreeMin();
}

/* ----------------------------------------------------------------
 * PUBLIC: AUDIO M2M C++ OBJECT
 * -------------------------------------------------------------- */

// The consts of the definition of the object.
const M2MObjectHelper::DefObject IocM2mAudio::_defObject =
    {0, "32770", 5,
        -1, RESOURCE_NUMBER_STREAMING_ENABLED, "boolean", M2MResourceBase::BOOLEAN, true, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_DURATION, "duration", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_FIXED_GAIN, "level", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE, "mode", M2MResourceBase::INTEGER, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_AUDIO_SERVER_URL, "string", M2MResourceBase::STRING, false, M2MBase::GET_PUT_ALLOWED, NULL
    };

// Constructor.
IocM2mAudio::IocM2mAudio(Callback<void(const Audio *)> pSetCallback,
                         Callback<bool(bool *)> pGetStreamingEnabledCallback,
                         Audio *pInitialValues,
                         bool debugOn)
            :M2MObjectHelper(&_defObject,
                             value_updated_callback(this, &IocM2mAudio::objectUpdated),
                             NULL,
                             debugOn)
{
    _pSetCallback = pSetCallback;
    _pGetStreamingEnabledCallback = pGetStreamingEnabledCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Set the initial values
    MBED_ASSERT(setResourceValue(pInitialValues->streamingEnabled, RESOURCE_NUMBER_STREAMING_ENABLED));
    MBED_ASSERT(setResourceValue(pInitialValues->duration, RESOURCE_NUMBER_DURATION));
    MBED_ASSERT(setResourceValue(pInitialValues->fixedGain,  RESOURCE_NUMBER_FIXED_GAIN));
    MBED_ASSERT(setResourceValue(pInitialValues->audioCommunicationsMode, RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE));
    MBED_ASSERT(setResourceValue(pInitialValues->audioServerUrl, RESOURCE_NUMBER_AUDIO_SERVER_URL));

    // Update the observable resources
    updateObservableResources();

    printf("IocM2mAudio: object initialised.\n");
}

// Destructor.
IocM2mAudio::~IocM2mAudio()
{
}

// Callback when the object is updated by the server.
void IocM2mAudio::objectUpdated(const char *pResourceName)
{
    Audio audio;

    printf("IocM2mAudio: resource \"%s\" has been updated.\n", pResourceName);

    MBED_ASSERT(getResourceValue(&audio.streamingEnabled, RESOURCE_NUMBER_STREAMING_ENABLED));
    MBED_ASSERT(getResourceValue(&audio.duration, RESOURCE_NUMBER_DURATION));
    MBED_ASSERT(getResourceValue(&audio.fixedGain, RESOURCE_NUMBER_FIXED_GAIN));
    MBED_ASSERT(getResourceValue(&audio.audioCommunicationsMode, RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE));
    MBED_ASSERT(getResourceValue(&audio.audioServerUrl, RESOURCE_NUMBER_AUDIO_SERVER_URL));

    printf("IocM2mAudio: new audio parameters are:\n");
    printf("  streamingEnabled %d.\n", audio.streamingEnabled);
    printf("  duration %f (-1 == no limit).\n", audio.duration);
    printf("  fixedGain %f (-1 == use automatic gain).\n", audio.fixedGain);
    printf("  audioCommunicationsMode %lld (0 for UDP, 1 for TCP).\n", audio.audioCommunicationsMode);
    printf("  audioServerUrl \"%s\".\n", audio.audioServerUrl.c_str());

    if (_pSetCallback) {
        _pSetCallback(&audio);
    }
}

// Update the observable data for this object.
void IocM2mAudio::updateObservableResources()
{
    bool streamingEnabled;

    // Update the data
    if (_pGetStreamingEnabledCallback) {
        if (_pGetStreamingEnabledCallback(&streamingEnabled)) {
            // Set the values in the resources based on the new data
            MBED_ASSERT(setResourceValue(streamingEnabled, RESOURCE_NUMBER_STREAMING_ENABLED));
        }
    }
}

// End of file
