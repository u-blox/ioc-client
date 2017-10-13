//----------------------------------------------------------------------------
// The confidential and proprietary information contained in this file may
// only be used by a person authorised under and to the extent permitted
// by a subsisting licensing agreement from ARM Limited or its affiliates.
//
// (C) COPYRIGHT 2016 ARM Limited or its affiliates.
// ALL RIGHTS RESERVED
//
// This entire notice must be reproduced on all copies of this file
// and copies of this file may only be made by a person if such person is
// permitted to do so under the terms of a subsisting license agreement
// from ARM Limited or its affiliates.
//----------------------------------------------------------------------------

#include "mbed.h"
#include "factory_configurator_client.h"
#include "SDBlockDevice.h"
#include "UbloxPPPCellularInterface.h"
#include "cloud_client_dm.h"
#include "ioc_m2m.h"
#include "I2S.h"
#include "urtp.h"
#include "ioc_log.h"
#ifdef MBED_HEAP_STATS_ENABLED
#include "mbed_stats.h"
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// Software version
#define SOFTWARE_VERSION "0.0.0.0"

#ifndef MBED_CONF_APP_OBJECT_DEBUG_ON
// Get debug prints from LWM2M object stuff.
#  define MBED_CONF_APP_OBJECT_DEBUG_ON true
#endif

#ifndef MBED_CONF_APP_MODEM_DEBUG_ON
// Modem debug prints.
#  define MBED_CONF_APP_MODEM_DEBUG_ON false
#endif

#ifndef MBED_CONF_APP_CLOUD_CLIENT_RESET_STORAGE
// Whether to reset Mbed Cloud Client storage or not.
// If this is true then you really need to have
// MBED_CONF_APP_DEVELOPER_MODE defined and then a
// new object ID will be created when the Mbed Cloud
// Client registers with the server.
#  define MBED_CONF_APP_CLOUD_CLIENT_RESET_STORAGE false
#endif

// The baud rate to use with the modem.
#define MODEM_BAUD_RATE 230400

// The possible communications modes (for audio streaming).
#define COMMS_TCP 1
#define COMMS_UDP 0

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

// The configuration items for the LWM2M Device object.
#define DEVICE_OBJECT_DEVICE_TYPE      "ioc"
#define DEVICE_OBJECT_SERIAL_NUMBER    "0"
#define DEVICE_OBJECT_HARDWARE_VERSION "0"
#define DEVICE_OBJECT_SOFTWARE_VERSION SOFTWARE_VERSION
#define DEVICE_OBJECT_FIRMWARE_VERSION "0"
#define DEVICE_OBJECT_MEMORY_TOTAL     256
#define DEVICE_OBJECT_UTC_OFFSET       "UTC+0"
#define DEVICE_OBJECT_TIMEZONE         "+513030-0000731" // London

// The default audio setup data.
#define AUDIO_DEFAULT_STREAMING_ENABLED  false
#define AUDIO_DEFAULT_DURATION           -1.0
#define AUDIO_DEFAULT_FIXED_GAIN         -1.0
#define AUDIO_DEFAULT_COMMUNICATION_MODE COMMS_TCP
#define AUDIO_DEFAULT_SERVER_URL         "ciot.it-sgn.u-blox.com:8080"

// The default config data.
#define CONFIG_DEFAULT_INIT_WAKEUP_TICK_PERIOD    3600.0
#define CONFIG_DEFAULT_INIT_WAKEUP_COUNT          2
#define CONFIG_DEFAULT_NORMAL_WAKEUP_TICK_PERIOD  60.0
#define CONFIG_DEFAULT_NORMAL_WAKEUP_COUNT        60
#define CONFIG_DEFAULT_BATTERY_WAKEUP_TICK_PERIOD 600.0
#define CONFIG_DEFAULT_GNSS_ENABLE                true

// The static temperature object values.
#define TEMPERATURE_MIN_MEASURABLE_RANGE -10.0
#define TEMPERATURE_MAX_MEASURABLE_RANGE 120.0
#define TEMPERATURE_UNITS "cel"

// A signal to indicate that an audio datagram is ready to send.
#define SIG_DATAGRAM_READY 0x01

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// The identifiers for each LWM2M object.
//
// To add a new object:
// - add an entry for it here,
// - add it to IocM2mObjectPointerUnion,
// - add it to addObject() and deleteObject().
//
// Note: this enum is used to index into gObjectList.
typedef enum {
    IOC_M2M_POWER_CONTROL,
    IOC_M2M_LOCATION,
    IOC_M2M_TEMPERATURE,
    IOC_M2M_CONFIG,
    IOC_M2M_AUDIO,
    IOC_M2M_DIAGNOSTICS,
    MAX_NUM_IOC_M2M_OBJECTS
} IocM2mObjectId;

// Union of pointers to all the LWM2M objects.
typedef union {
    IocM2mPowerControl *pPowerControl;
    IocM2mLocation *pLocation;
    IocM2mTemperature *pTemperature;
    IocM2mConfig *pConfig;
    IocM2mAudio *pAudio;
    IocM2mDiagnostics *pDiagnostics;
    void* raw;
} IocM2mObjectPointerUnion;

// Structure defining the things we need
// to access in an LWM2M object
typedef struct {
    IocM2mObjectPointerUnion object;
    Callback<void(void)> updateObservableResources;
} IocM2mObject;

// Implementation of MbedCloudClientCallback,
// a catch-all should the callback get missed out on
// an object that includes a writable resources.
class UpdateCallback : public MbedCloudClientCallback {
public:
    UpdateCallback() {
    }

    virtual ~UpdateCallback() {
    }

    // Implementation of MbedCloudClientCallback
    virtual void value_updated(M2MBase *base, M2MBase::BaseType type) {
        printf("UNHANDLED  PUT request, name: \"%s\", path: \"%s\","
               " type: \"%d\" (0 for object, 1 for resource), type: \"%s\".\n",
               base->name(), base->uri_path(), type, base->resource_type());
    }
};

// Union of socket types.
typedef union {
    TCPSocket *pTcpSock;
    UDPSocket *pUdpSock;
} SocketPointerUnion;

// A struct to hold the audio communications channel parameters.
typedef struct {
    int socketMode; // Either COMMS_TCP or COMMS_UDP
    SocketPointerUnion sock;
    SocketAddress server;
} AudioCommsParams;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// The SD card, which is instantiated in the Mbed Cloud Client
// in pal_plat_fileSystem.cpp.
extern SDBlockDevice sd;

// The network interface.
static UbloxPPPCellularInterface *gpCellular = NULL;

// The Mbed Cloud Client stuff.
static CloudClientDm *gpCloudClientDm = NULL;
static UpdateCallback *gpCloudClientGlobalUpdateCallback = NULL;

// Array of LWM2M objects for the control plane.
// NOTE: this array is intended to be indexed using the
// IocM2mObjectId enum.
static IocM2mObject gObjectList[MAX_NUM_IOC_M2M_OBJECTS];

// Data storage.
static bool gPowerOnNotOff = false;
static IocM2mLocation::Location gLocationData;
static IocM2mTemperature::Temperature gTemperatureData;
static IocM2mConfig::Config gConfigData;
static IocM2mAudio::Audio gAudioData;
// Note: don't need storage here for IocM2mDiagnostics
// since the values needed include floats and we want
// to keep things nice and simple, hence use integers,
// see lower down in the variables section.

// The event loop and event queue.
static Thread gEventThread;
static EventQueue gEventQueue (32 * EVENTS_EVENT_SIZE);

// Event ID for the LWM2M object update event.
static int gObjectUpdateEvent = -1;

// Thread required to run the I2S driver event queue.
static Thread *gpI2sTask = NULL;

// Function that forms the body of the gI2sTask.
static const Callback<void()> gI2STaskCallback(&I2S::i2s_bh_queue, &events::EventQueue::dispatch_forever);

// Audio buffer, enough for two blocks of stereo audio,
// where each sample takes up 64 bits (32 bits for L channel
// and 32 bits for R channel).
// Note: can't be in CCMRAM as DMA won't reach there.
static uint32_t gRawAudio[(SAMPLES_PER_BLOCK * 2) * 2];

// Datagram storage for URTP.
__attribute__ ((section ("CCMRAM")))
static char gDatagramStorage[URTP_DATAGRAM_STORE_SIZE];

// Task to send data off to the audio streaming server.
static Thread *gpSendTask = NULL;

// Audio comms parameters.
static AudioCommsParams gAudioCommsParams = {0};

// Flag to indicate that the audio comms channel is up.
static volatile bool gAudioCommsConnected = false;

// The microphone.
static I2S gMic(PB_15, PB_10, PB_9);

// The user button.
static InterruptIn *gpUserButton = NULL;
static volatile bool gUserButtonPressed = false;

// LEDs for user feedback and diagnostics.
static DigitalOut gLedRed(LED1, 1);
static DigitalOut gLedGreen(LED2, 1);
static DigitalOut gLedBlue(LED3, 1);

// For diagnostics.
static uint64_t gWorstCaseAudioDatagramSendDuration = 0;
static uint64_t gAverageAudioDatagramSendDuration = 0;
static uint64_t gNumAudioDatagrams = 0;
static unsigned int gNumAudioSendFailures = 0;
static unsigned int gNumAudioDatagramsSendTookTooLong = 0;
static unsigned int gNumAudioBytesSent = 0;
static Ticker gAudioSecondTicker;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: DIAGNOSTICS
 * -------------------------------------------------------------- */

// Indicate good (green)
static void good() {
    gLedGreen = 0;
    gLedBlue = 1;
    gLedRed = 1;
}

// Indicate bad (red)
static void bad() {
    gLedRed = 0;
    gLedGreen = 1;
    gLedBlue = 1;
}

// Toggle green
static void toggleGreen() {
    gLedGreen = !gLedGreen;
}

// Set blue
static void event() {
    gLedBlue = 0;
}

// Unset blue
static void notEvent() {
    gLedBlue = 1;
}

// All off
static void ledOff() {
    gLedBlue = 1;
    gLedRed = 1;
    gLedGreen = 1;
}

// Print heap stats
static void heapStats()
{
#ifdef MBED_HEAP_STATS_ENABLED
    mbed_stats_heap_t stats;
    mbed_stats_heap_get(&stats);

    printf("HEAP size:     %" PRIu32 ".\n", stats.current_size);
    printf("HEAP maxsize:  %" PRIu32 ".\n", stats.max_size);
#endif
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: URTP CODEC AND ITS CALLBACK FUNCTIONS
 * -------------------------------------------------------------- */

// Callback for when an audio datagram is ready for sending.
static void datagramReadyCb(const char * datagram)
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

// The URTP codec.
static Urtp gUrtp(&datagramReadyCb, &datagramOverflowStartCb, &datagramOverflowStopCb);

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: AUDIO CONNECTION
 * -------------------------------------------------------------- */

// Monitor on a 1 second tick.
// This is a ticker call-back, so nothing heavy please.
static void audioMonitor()
{
    // Monitor throughput
    if (gNumAudioBytesSent > 0) {
        LOG(EVENT_THROUGHPUT_BITS_S, gNumAudioBytesSent << 3);
        gNumAudioBytesSent = 0;
        LOG(EVENT_NUM_DATAGRAMS_QUEUED, gUrtp.getUrtpDatagramsAvailable());
    }
}

// Get the address portion of a URL, leaving off the port number etc.
void getAddressFromUrl(const char * pUrl, char * pAddressBuf, int lenBuf)
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
bool getPortFromUrl(const char * pUrl, int *port)
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
// This will set up the gAudioCommsParams structure.
// Note: here be multiple return statements.
static bool startAudioStreamingConnection(IocM2mAudio::Audio *pAudio)
{
    char *pBuf = new char[AUDIO_MAX_LEN_SERVER_URL];
    int port;
    nsapi_error_t nsapiError;
    const int setOption = 1;

    printf("Resolving IP address of the audio streaming server...\n");
    if (!gpCellular || !gpCellular->is_connected()) {
        bad();
        printf("Error, network is not ready.\n");
        return false;
    } else {
        getAddressFromUrl(pAudio->audioServerUrl.c_str(), pBuf, AUDIO_MAX_LEN_SERVER_URL);
        printf("Looking for server URL \"%s\"...\n", pBuf);
        if (gpCellular->gethostbyname(pBuf, &gAudioCommsParams.server) == 0) {
            printf("Found it at IP address %s.\n", gAudioCommsParams.server.get_ip_address());
            if (getPortFromUrl(pAudio->audioServerUrl.c_str(), &port)) {
                gAudioCommsParams.server.set_port(port);
                printf("Audio server port set to %d.\n", gAudioCommsParams.server.get_port());
            } else {
                printf("WARNING: no port number was specified in the audio server URL (\"%s\").\n",
                       pAudio->audioServerUrl.c_str());
            }
        } else {
            bad();
            printf("Error, couldn't resolve IP address of audio streaming server.\n");
            return false;
        }
    }

    printf("Opening socket to server for audio comms...\n");
    switch (pAudio->audioCommunicationsMode) {
        case COMMS_TCP:
            gAudioCommsParams.socketMode = COMMS_TCP;
            gAudioCommsParams.sock.pTcpSock = new TCPSocket();
            nsapiError = gAudioCommsParams.sock.pTcpSock->open(gpCellular);
            if (nsapiError != NSAPI_ERROR_OK) {
                bad();
                printf("Could not open TCP socket to audio streaming server (error %d).\n", nsapiError);
                return false;
            } else {
                printf("Connecting TCP...\n");
                nsapiError = gAudioCommsParams.sock.pTcpSock->connect(gAudioCommsParams.server);
                if (nsapiError != NSAPI_ERROR_OK) {
                    bad();
                    printf("Could not connect TCP socket (error %d).\n", nsapiError);
                    return false;
                } else {
                    printf("Setting TCP_NODELAY in TCP socket options...\n");
                    // Set TCP_NODELAY (1) in level IPPROTO_TCP (6) to 1
                    nsapiError = gAudioCommsParams.sock.pTcpSock->setsockopt(6, 1, &setOption, sizeof(setOption));
                    if (nsapiError != NSAPI_ERROR_OK) {
                        bad();
                        printf("Could not set TCP socket options (error %d).\n", nsapiError);
                        return false;
                    }
                }
            }
            break;
        case COMMS_UDP:
            gAudioCommsParams.socketMode = COMMS_UDP;
            gAudioCommsParams.sock.pUdpSock = new UDPSocket();
            nsapiError = gAudioCommsParams.sock.pUdpSock->open(gpCellular);
            if (nsapiError != NSAPI_ERROR_OK) {
                bad();
                printf("Could not open UDP socket to audio streaming server (error %d).\n", nsapiError);
                return false;
            }
            break;
        default:
            bad();
            printf("Unknown audio communications mode (%lld).\n", pAudio->audioCommunicationsMode);
            return false;
            break;
    }

    gAudioCommsConnected = true;

    return true;
}

// Stop the audio streaming connection.
static void stopAudioStreamingConnection()
{
    printf("Closing audio server socket...\n");
    switch (gAudioCommsParams.socketMode) {
        case COMMS_TCP:
            gAudioCommsParams.sock.pTcpSock->close();
            delete gAudioCommsParams.sock.pTcpSock;
            gAudioCommsParams.sock.pTcpSock = NULL;
            break;
        case COMMS_UDP:
            gAudioCommsParams.sock.pUdpSock->close();
            delete gAudioCommsParams.sock.pUdpSock;
            gAudioCommsParams.sock.pUdpSock = NULL;
            break;
        default:
            bad();
            printf("Unknown audio communications mode (%d).\n", gAudioCommsParams.socketMode);
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
static void sendAudioData(const AudioCommsParams * pAudioCommsParams)
{
    const char * urtpDatagram = NULL;
    Timer sendDurationTimer;
    Timer badSendDurationTimer;
    unsigned int duration;
    int retValue;
    bool okToDelete = false;

    while (gAudioCommsConnected) {
        // Wait for at least one datagram to be ready to send
        Thread::signal_wait(SIG_DATAGRAM_READY, AUDIO_SEND_DATA_RUN_ANYWAY_TIME_MS);

        while ((urtpDatagram = gUrtp.getUrtpDatagram()) != NULL) {
            okToDelete = false;
            sendDurationTimer.reset();
            sendDurationTimer.start();
            // Send the datagram
            if (gAudioCommsConnected) {
                //LOG(EVENT_SEND_START, (int) urtpDatagram);
                if (pAudioCommsParams->socketMode == COMMS_TCP) {
                    retValue = tcpSend(pAudioCommsParams->sock.pTcpSock, urtpDatagram, URTP_DATAGRAM_SIZE);
                } else {
                    retValue = pAudioCommsParams->sock.pUdpSock->sendto(pAudioCommsParams->server, urtpDatagram, URTP_DATAGRAM_SIZE);
                }
                if (retValue != URTP_DATAGRAM_SIZE) {
                    badSendDurationTimer.start();
                    LOG(EVENT_SEND_FAILURE, retValue);
                    bad();
                    gNumAudioSendFailures++;
                } else {
                    gNumAudioBytesSent += retValue;
                    okToDelete = true;
                    badSendDurationTimer.stop();
                    badSendDurationTimer.reset();
                    toggleGreen();
                }
                //LOG(EVENT_SEND_STOP, (int) urtpDatagram);

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
            gAverageAudioDatagramSendDuration += duration;
            gNumAudioDatagrams++;

            if (duration > BLOCK_DURATION_MS * 1000) {
                // If this is UDP then it's serious, if it's TCP then
                // we can catch up.
                if (pAudioCommsParams->socketMode == COMMS_UDP) {
                    LOG(EVENT_SEND_DURATION_GREATER_THAN_BLOCK_DURATION, duration);
                }
                gNumAudioDatagramsSendTookTooLong++;
            } else {
                //LOG(EVENT_SEND_DURATION, duration);
            }
            if (duration > gWorstCaseAudioDatagramSendDuration) {
                gWorstCaseAudioDatagramSendDuration = duration;
                LOG(EVENT_NEW_PEAK_SEND_DURATION, gWorstCaseAudioDatagramSendDuration);
            }

            if (okToDelete) {
                gUrtp.setUrtpDatagramAsRead(urtpDatagram);
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
                printf("I2S started...\n");
            } else {
                bad();
                printf("Unable to start I2S transfer.\n");
            }
        }
    } else {
        bad();
        printf("Unable to start I2S thread.\n");
    }

    return success;
}

// Stop the I2S interface.
static void stopI2s(I2S * pI2s)
{
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

// Start audio streaming.
// Note: here be multiple return statements.
bool startStreaming()
{
    int retValue;

    // Start the per-second monitor tick
    gAudioSecondTicker.attach_us(callback(&audioMonitor), 1000000);

    if (!startAudioStreamingConnection(&gAudioData)) {
        return false;
    }

    printf ("Setting up URTP...\n");
    if (!gUrtp.init((void *) &gDatagramStorage)) {
        bad();
        printf ("Unable to start URTP.\n");
        return false;
    }

    printf ("Starting task to send audio data...\n");
    if (gpSendTask == NULL) {
        gpSendTask = new Thread();
    }
    retValue = gpSendTask->start(callback(sendAudioData, &gAudioCommsParams));
    if (retValue != osOK) {
        bad();
        printf ("Error starting task (%d).\n", retValue);
        return false;
    }

    if (!startI2s(&gMic)) {
        return false;
    }

    printf("Now streaming audio.\n");

    return true;
}

// Stop audio streaming.
void stopStreaming()
{
    stopI2s(&gMic);

    // Wait for any on-going transmissions to complete
    wait_ms(2000);

    printf ("Stopping audio send task...\n");
    gpSendTask->terminate();
    gpSendTask->join();
    delete gpSendTask;
    gpSendTask = NULL;
    printf ("Audio send task stopped.\n");

    stopAudioStreamingConnection();

    gAudioSecondTicker.detach();

    printf("Audio streaming stopped.\n");
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: LWM2M
 * -------------------------------------------------------------- */

// Add an LWM2M object to the relevant lists
static void addObject(IocM2mObjectId id, void * object)
{
    printf ("Adding object with ID %d.\n", id);

    MBED_ASSERT (gpCloudClientDm);
    MBED_ASSERT (gObjectList[id].object.raw == NULL);
    MBED_ASSERT (!gObjectList[id].updateObservableResources);

    switch (id) {
        case IOC_M2M_POWER_CONTROL:
            gObjectList[id].object.pPowerControl = (IocM2mPowerControl *) object;
            gpCloudClientDm->addObject(gObjectList[id].object.pPowerControl->getObject());
            gObjectList[id].updateObservableResources = Callback<void(void)>(gObjectList[id].object.pPowerControl,
                                                                             &IocM2mPowerControl::updateObservableResources);
            break;
        case IOC_M2M_LOCATION:
            gObjectList[id].object.pLocation = (IocM2mLocation *) object;
            gpCloudClientDm->addObject(gObjectList[id].object.pLocation->getObject());
            gObjectList[id].updateObservableResources = Callback<void(void)>(gObjectList[id].object.pLocation,
                                                                             &IocM2mLocation::updateObservableResources);
            break;
        case IOC_M2M_TEMPERATURE:
            gObjectList[id].object.pTemperature = (IocM2mTemperature *) object;
            gpCloudClientDm->addObject(gObjectList[id].object.pTemperature->getObject());
            gObjectList[id].updateObservableResources = Callback<void(void)>(gObjectList[id].object.pTemperature,
                                                                             &IocM2mTemperature::updateObservableResources);
            break;
        case IOC_M2M_CONFIG:
            gObjectList[id].object.pConfig = (IocM2mConfig *) object;
            gpCloudClientDm->addObject(gObjectList[id].object.pConfig->getObject());
            gObjectList[id].updateObservableResources = Callback<void(void)>(gObjectList[id].object.pConfig,
                                                                             &IocM2mConfig::updateObservableResources);
            break;
        case IOC_M2M_AUDIO:
            gObjectList[id].object.pAudio = (IocM2mAudio *) object;
            gpCloudClientDm->addObject(gObjectList[id].object.pAudio->getObject());
            gObjectList[id].updateObservableResources = Callback<void(void)>(gObjectList[id].object.pAudio,
                                                                             &IocM2mAudio::updateObservableResources);
            break;
        case IOC_M2M_DIAGNOSTICS:
            gObjectList[id].object.pDiagnostics = (IocM2mDiagnostics *) object;
            gpCloudClientDm->addObject(gObjectList[id].object.pDiagnostics->getObject());
            gObjectList[id].updateObservableResources = Callback<void(void)>(gObjectList[id].object.pDiagnostics,
                                                                             &IocM2mDiagnostics::updateObservableResources);
            break;
        default:
            printf("Unknown object ID (%d).\n", id);
            break;
    }
}

// Delete an object.
// IMPORTANT: this does not remove the object from the Mbed Cloud Client,
// the Mbed Cloud Client clears itself up at the end.
void deleteObject(IocM2mObjectId id)
{
    if (gObjectList[id].object.raw != NULL) {
        switch (id) {
            case IOC_M2M_POWER_CONTROL:
                delete gObjectList[id].object.pPowerControl;
                gObjectList[id].object.pPowerControl = NULL;
                break;
            case IOC_M2M_LOCATION:
                delete gObjectList[id].object.pLocation;
                gObjectList[id].object.pLocation = NULL;
                break;
            case IOC_M2M_TEMPERATURE:
                delete gObjectList[id].object.pTemperature;
                gObjectList[id].object.pTemperature = NULL;
                break;
            case IOC_M2M_CONFIG:
                delete gObjectList[id].object.pConfig;
                gObjectList[id].object.pConfig = NULL;
                break;
            case IOC_M2M_AUDIO:
                delete gObjectList[id].object.pAudio;
                gObjectList[id].object.pAudio = NULL;
                break;
            case IOC_M2M_DIAGNOSTICS:
                delete gObjectList[id].object.pDiagnostics;
                gObjectList[id].object.pDiagnostics = NULL;
                break;
            default:
                printf("Unknown object ID (%d).\n", id);
                break;
        }

        if (gObjectList[id].object.raw == NULL) {
            gObjectList[id].updateObservableResources = NULL;
        }
    }
}

// Callback when mbed Cloud Client registers with the LWM2M server.
static void cloudClientRegisteredCallback() {
    printf("Cloud Client registered, press the user button to exit.\n");
}

// Callback when mbed Cloud Client deregisters from the LWM2M server.
static void cloudClientDeregisteredCallback() {
    printf("Cloud Client deregistered.\n");
}

// Callback that sets the power switch via the IocM2mPowerControl
// object.
static void setPowerControl(bool value)
{
    printf("Power control set to %d.\n", value);

    gPowerOnNotOff = value;

    // TODO act on it
}

// Callback that retrieves location data for the IocM2mLocation
// object.
static bool getLocationData(IocM2mLocation::Location *data)
{
    // TODO get it

    *data = gLocationData;

    return true;
}

// Callback that retrieves temperature data for the IocM2mTemperature
// object.
static bool getTemperatureData(IocM2mTemperature::Temperature *data)
{
    // TODO get it

    *data = gTemperatureData;

    return true;
}

// Callback that executes a reset of the min/max temperature range
// via the IocM2mTemperature object.
static void executeResetTemperatureMinMax()
{
    printf("Received min/max temperature reset.\n");

    gTemperatureData.maxTemperature = gTemperatureData.temperature;
    gTemperatureData.minTemperature = gTemperatureData.temperature;
}

// Callback that sets the configuration data via the IocM2mConfig
// object.
static void setConfigData(IocM2mConfig::Config *data)
{
    printf("Received new config settings:\n");
    printf("  initWakeUpTickPeriod %f.\n", data->initWakeUpTickPeriod);
    printf("  initWakeUpCount %lld.\n", data->initWakeUpCount);
    printf("  normalWakeUpTickPeriod %f.\n", data->normalWakeUpTickPeriod);
    printf("  normalWakeUpCount %lld.\n", data->normalWakeUpCount);
    printf("  batteryWakeUpTickPeriod %f.\n", data->batteryWakeUpTickPeriod);
    printf("  GNSS enable %d.\n", data->gnssEnable);

    gConfigData = *data;

    // TODO act on it
}

// Callback that sets the configuration data via the IocM2mAudio
// object.
static void setAudioData(IocM2mAudio::Audio *data)
{
    printf("Received new audio parameters:\n");
    printf("  streamingEnabled %d.\n", data->streamingEnabled);
    printf("  duration %f.\n", data->duration);
    printf("  fixedGain %f.\n", data->fixedGain);
    printf("  audioCommunicationsMode %lld.\n", data->audioCommunicationsMode);
    printf("  audioServerUrl \"%s\".\n", data->audioServerUrl.c_str());

    gAudioData = *data;

    // TODO act on it
}

// Callback that gets diagnostic data for the IocM2mDiagnostics object.
static bool getDiagnosticsData(IocM2mDiagnostics::Diagnostics *data)
{
    data->upTime = 0; // TODO
    data->worstCaseSendDuration = (float) gWorstCaseAudioDatagramSendDuration;
    data->averageSendDuration = (float) (gAverageAudioDatagramSendDuration /
                                         gNumAudioDatagrams);
    data->minNumDatagramsFree = gUrtp.getUrtpDatagramsFreeMin();
    data->numSendFailures = gNumAudioSendFailures;
    data->percentageSendsTooLong = (int64_t) gNumAudioDatagramsSendTookTooLong * 100 /
                                   gNumAudioDatagrams;

    return true;
}

// Callback to update the observable values in all of the LWM2M objects.
static void objectUpdate()
{
    for (unsigned int x = 0; x < sizeof (gObjectList) /
                                 sizeof (gObjectList[0]); x++) {
        if (gObjectList[x].updateObservableResources) {
            gObjectList[x].updateObservableResources();
        }
    }
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: MISC
 * -------------------------------------------------------------- */

// Function to attach to the user button.
static void buttonCallback()
{
    gUserButtonPressed = true;
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: INITIALISATION AND DEINITIALISATION
 * -------------------------------------------------------------- */

// Initialise everything.  If you add anything here, be sure to
// add the opposite to deinit().
// Note: here be multiple return statements.
static bool init(bool resetStorage)
{
    int x = 0;

    printf("Starting logging...\n");
    initLog();
    LOG(EVENT_LOG_START, 0);

    printf("Setting up data storage...\n");
    memset(&gLocationData, 0, sizeof (gLocationData));
    memset(&gTemperatureData, 0, sizeof (gTemperatureData));

    gConfigData.initWakeUpTickPeriod = CONFIG_DEFAULT_INIT_WAKEUP_TICK_PERIOD;
    gConfigData.initWakeUpCount = CONFIG_DEFAULT_INIT_WAKEUP_COUNT;
    gConfigData.normalWakeUpTickPeriod = CONFIG_DEFAULT_NORMAL_WAKEUP_TICK_PERIOD;
    gConfigData.normalWakeUpCount = CONFIG_DEFAULT_NORMAL_WAKEUP_COUNT;
    gConfigData.batteryWakeUpTickPeriod = CONFIG_DEFAULT_BATTERY_WAKEUP_TICK_PERIOD;
    gConfigData.gnssEnable = CONFIG_DEFAULT_GNSS_ENABLE;

    gAudioData.streamingEnabled = AUDIO_DEFAULT_STREAMING_ENABLED;
    gAudioData.duration = AUDIO_DEFAULT_DURATION;
    gAudioData.fixedGain = AUDIO_DEFAULT_FIXED_GAIN;
    gAudioData.audioCommunicationsMode = AUDIO_DEFAULT_COMMUNICATION_MODE;
    gAudioData.audioServerUrl = AUDIO_DEFAULT_SERVER_URL;

    printf("Creating user button...\n");
    gpUserButton = new InterruptIn(SW0);
    gpUserButton->rise(&buttonCallback);

    printf("Starting SD card...\n");
    x = sd.init();
    if (x != 0) {
        bad();
        printf("Error initialising SD card (%d).\n", x);
        return false;
    }
    printf("SD card started.\n");

    printf("Initialising Mbed Cloud Client file storage...\n");
    fcc_status_e status = fcc_init();
    if(status != FCC_STATUS_SUCCESS) {
        bad();
        printf("Error initialising Mbed Cloud Client file storage (%d).\n", status);
        return false;
    }
    printf("Mbed Cloud Client file storage initialised.\n");

    if (resetStorage) {
        // Use this function when you want to clear storage from all
        // the factory-tool generated data and user data.
        // After this operation device must be injected again by using
        // factory tool or developer certificate.
        printf("Resetting Mbed Cloud Client storage to an empty state...\n");
        fcc_status_e deleteStatus = fcc_storage_delete();
        if (deleteStatus != FCC_STATUS_SUCCESS) {
            bad();
            printf("Failed to delete Mbed Cloud Client storage - %d\n", deleteStatus);
            return false;
        }
    }

#ifdef MBED_CONF_APP_DEVELOPER_MODE
    printf("Starting Mbed Cloud Client developer flow...\n");
    status = fcc_developer_flow();
    if (status == FCC_STATUS_KCM_FILE_EXIST_ERROR) {
        printf("Mbed Cloud Client developer credentials already exist.\n");
    } else if (status != FCC_STATUS_SUCCESS) {
        bad();
        printf("Failed to load Mbed Cloud Clientdeveloper credentials.\n");
        return false;
    }    
#endif

    printf("Checking Mbed Cloud Client configuration files...\n");
    status = fcc_verify_device_configured_4mbed_cloud();
    if (status != FCC_STATUS_SUCCESS) {
        bad();
        printf("Device not configured for Mbed Cloud Client.\n");
#ifdef MBED_CONF_APP_DEVELOPER_MODE
        printf("  You might want to clear mbed Cloud Client file storage and try again.\n");
#endif
        return false;
    }

    // Note sure if this is required or not; it doesn't do any harm.
    srand(time(NULL));

    printf("Initialising cellular...\n");
    gpCellular = new UbloxPPPCellularInterface(MDMTXD, MDMRXD, MODEM_BAUD_RATE,
                                               MBED_CONF_APP_MODEM_DEBUG_ON);
    if (!gpCellular->init()) {
        bad();
        printf("Unable to initialise cellular.\n");
        return false;
    } else {
        printf("Please wait up to 180 seconds to connect to the packet network...\n");
        if (gpCellular->connect() != NSAPI_ERROR_OK) {
            bad();
            printf("Unable to connect to the cellular packet network.\n");
            return false;
        }
    }

    printf("Initialising Mbed Cloud Client...\n");
    gpCloudClientDm = new CloudClientDm(MBED_CONF_APP_OBJECT_DEBUG_ON,
                                        &cloudClientRegisteredCallback,
                                        &cloudClientDeregisteredCallback);

    printf("Configuring the LWM2M Device object...\n");
    if (!gpCloudClientDm->setDeviceObjectStaticDeviceType(DEVICE_OBJECT_DEVICE_TYPE) ||
        !gpCloudClientDm->setDeviceObjectStaticSerialNumber(DEVICE_OBJECT_SERIAL_NUMBER) ||
        !gpCloudClientDm->setDeviceObjectStaticHardwareVersion(DEVICE_OBJECT_HARDWARE_VERSION) ||
        !gpCloudClientDm->setDeviceObjectStaticSoftwareVersion(DEVICE_OBJECT_SOFTWARE_VERSION) ||
        !gpCloudClientDm->setDeviceObjectFirmwareVersion(DEVICE_OBJECT_FIRMWARE_VERSION) ||
        !gpCloudClientDm->addDeviceObjectPowerSource(CloudClientDm::POWER_SOURCE_INTERNAL_BATTERY) ||
        !gpCloudClientDm->setDeviceObjectMemoryTotal(DEVICE_OBJECT_MEMORY_TOTAL) ||
        !gpCloudClientDm->setDeviceObjectUtcOffset(DEVICE_OBJECT_UTC_OFFSET) ||
        !gpCloudClientDm->setDeviceObjectTimezone(DEVICE_OBJECT_TIMEZONE)) {
        bad();
        printf("Unable to configure the Device object.\n");
        return false;
    }

    printf("Creating all the other LWM2M objects...\n");
    for (unsigned int x = 0; x < sizeof (gObjectList) / sizeof(gObjectList[0]); x++) {
        gObjectList[x].object.raw = NULL;
        gObjectList[x].updateObservableResources = NULL;
    }
    addObject(IOC_M2M_POWER_CONTROL, new IocM2mPowerControl(setPowerControl, true,
                                                            MBED_CONF_APP_OBJECT_DEBUG_ON));
    addObject(IOC_M2M_LOCATION, new IocM2mLocation(getLocationData,
                                                   MBED_CONF_APP_OBJECT_DEBUG_ON));
    addObject(IOC_M2M_TEMPERATURE, new IocM2mTemperature(getTemperatureData,
                                                         executeResetTemperatureMinMax,
                                                         TEMPERATURE_MIN_MEASURABLE_RANGE,
                                                         TEMPERATURE_MAX_MEASURABLE_RANGE,
                                                         TEMPERATURE_UNITS,
                                                         MBED_CONF_APP_OBJECT_DEBUG_ON));
    addObject(IOC_M2M_CONFIG, new IocM2mConfig(setConfigData, &gConfigData,
                                               MBED_CONF_APP_OBJECT_DEBUG_ON));
    addObject(IOC_M2M_AUDIO, new IocM2mAudio(setAudioData, &gAudioData,
                                             MBED_CONF_APP_OBJECT_DEBUG_ON));
    addObject(IOC_M2M_DIAGNOSTICS, new IocM2mDiagnostics(getDiagnosticsData,
                                                         MBED_CONF_APP_OBJECT_DEBUG_ON));

    printf("Starting Mbed Cloud Client...\n");
    gpCloudClientGlobalUpdateCallback = new UpdateCallback();
    if (!gpCloudClientDm->start(gpCloudClientGlobalUpdateCallback)) {
        bad();
        printf("Error starting Mbed Cloud Client.\n");
        return false;
    } else {
        printf("Connecting to LWM2M server...\n");
        if (!gpCloudClientDm->connect(gpCellular)) {
            bad();
            printf("Unable to connect to LWM2M server.\n");
            return false;
        } else {
            printf("Connected to LWM2M server, registration should occur soon.\n");
            // !!! SUCCESS !!!
        }
    }

    return true;
}

// Shut everything down.
// Anything that was set up in init() should be cleared here.
static void deinit()
{
    if (gpCloudClientDm != NULL) {
        printf("Stopping Mbed Cloud Client...\n");
        gpCloudClientDm->stop();
    }
    printf("Deleting LWM2M objects...\n");
    for (unsigned int x = 0; x < sizeof (gObjectList) / sizeof (gObjectList[0]); x++) {
        deleteObject((IocM2mObjectId) x);
    }

    if (gpCloudClientDm != NULL) {
        printf("Deleting Mbed Cloud Client...\n");
        delete gpCloudClientDm;
        gpCloudClientDm = NULL;
        if (gpCloudClientGlobalUpdateCallback != NULL) {
            delete gpCloudClientGlobalUpdateCallback;
            gpCloudClientGlobalUpdateCallback = NULL;
        }
    }

    if (gpCellular != NULL) {
        printf("Disconnecting from the cellular packet network...\n");
        gpCellular->disconnect();
        printf("Stopping cellular modem...\n");
        gpCellular->deinit();
        delete gpCellular;
        gpCellular = NULL;
    }

    printf("Closing SD card...\n");
    sd.deinit();

    printf("Removing user button...\n");
    if (gpUserButton != NULL) {
        delete gpUserButton;
        gpUserButton = NULL;
    }

    printf("Printing the log...\n");
    LOG(EVENT_LOG_STOP, 0);
    printLog();

    if (gNumAudioDatagrams > 0) {
        printf("Stats:\n");
        printf("Worst case time to perform a send: %llu us.\n", gWorstCaseAudioDatagramSendDuration);
        printf("Average time to perform a send: %llu us.\n", gAverageAudioDatagramSendDuration);
        printf("Minimum number of datagram(s) free %d.\n", gUrtp.getUrtpDatagramsFreeMin());
        printf("Number of send failure(s) %d,\n", gNumAudioSendFailures);
        printf("%d send(s) took longer than %d ms (%llu%% of the total).\n", gNumAudioDatagramsSendTookTooLong,
               BLOCK_DURATION_MS, (uint64_t) gNumAudioDatagramsSendTookTooLong * 100 / gNumAudioDatagrams);
    }
    printf("All stop.\n");
}

/* ----------------------------------------------------------------
 * MAIN
 * -------------------------------------------------------------- */

int main() {
    printf("\n********** START **********\n");

    good();
    heapStats();

    // Initialise everything
    if (init(MBED_CONF_APP_CLOUD_CLIENT_RESET_STORAGE)) {

        printf("Starting event queue in context %p...\n", Thread::gettid());
        gEventThread.start(callback(&gEventQueue, &EventQueue::dispatch_forever));
        gObjectUpdateEvent = gEventQueue.call_every(CONFIG_DEFAULT_INIT_WAKEUP_TICK_PERIOD * 1000, objectUpdate);

        for (int x = 0; !gUserButtonPressed; x++) {
            wait_ms(1000);
            toggleGreen();
        }

        // Shut everything down
        deinit();
    }

    heapStats();
    ledOff();
    printf("********** STOP **********\n");
}

// End of file
