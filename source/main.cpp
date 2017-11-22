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
#include "low_power.h"
#include "factory_configurator_client.h"
#include "SDBlockDevice.h"
#include "FATFileSystem.h"
#include "UbloxPPPCellularInterface.h"
#include "cloud_client_dm.h"
#include "ioc_m2m.h"
#include "I2S.h"
#include "urtp.h"
#include "log.h"
#include "stm32f4xx_hal_iwdg.h"
#include "battery_gauge_bq27441.h"
#include "battery_charger_bq24295.h"
#include "gnss.h"
#include "compile_time.h"
#ifdef MBED_HEAP_STATS_ENABLED
#include "mbed_stats.h"
#endif

#if defined(MBED_CONF_MBED_TRACE_ENABLE) && MBED_CONF_MBED_TRACE_ENABLE
#include "mbed-trace/mbed_trace.h"
#include "mbed-trace-helper.h"
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

// The baud rate to use with the modem.
#define MODEM_BAUD_RATE 230400

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
#define DEVICE_OBJECT_UTC_OFFSET       "+00:00"
#define DEVICE_OBJECT_TIMEZONE         "+513030-0000731" // London

// The default audio setup data.
#define AUDIO_DEFAULT_STREAMING_ENABLED  false
#define AUDIO_DEFAULT_DURATION           -1
#define AUDIO_DEFAULT_FIXED_GAIN         -1
#define AUDIO_DEFAULT_COMMUNICATION_MODE COMMS_TCP
#define AUDIO_DEFAULT_SERVER_URL         "ciot.it-sgn.u-blox.com:5065"

// The default config data.
#define DEFAULT_POWER_ON_NOT_OFF                            false
#define CONFIG_DEFAULT_INIT_WAKE_UP_TICK_COUNTER_PERIOD     600
#define CONFIG_DEFAULT_INIT_WAKE_UP_TICK_COUNTER_MODULO     3
#define CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_PERIOD_1  60
#define CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_PERIOD_2  600
#define CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_MODULO    60
#define CONFIG_DEFAULT_GNSS_ENABLE                          true

// The static temperature object values.
#define TEMPERATURE_MIN_MEASURABLE_RANGE -10.0
#define TEMPERATURE_MAX_MEASURABLE_RANGE 120.0
#define TEMPERATURE_UNITS "cel"

// The default logging setup data.
#define LOGGING_DEFAULT_TO_FILE_ENABLED true
#define LOGGING_DEFAULT_UPLOAD_ENABLED  true
#define LOGGING_DEFAULT_SERVER_URL      "ciot.it-sgn.u-blox.com:5060"

// The period at which GNSS location is read (if it is active).
#define GNSS_UPDATE_PERIOD_MS (CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_PERIOD_1 / 2)

// Timeout for comms with the GNSS chip,
// should be less than GNSS_UPDATE_PERIOD_MS.
#define GNSS_COMMS_TIMEOUT_MS 1000

// The threshold for low battery warning.
#define LOW_BATTERY_WARNING_PERCENTAGE 20

// The period after which the "bad" status LED is tidied up.
#define BAD_OFF_PERIOD_MS 10000

// A signal to indicate that an audio datagram is ready to send.
#define SIG_DATAGRAM_READY 0x01

// Interval at which the watchdog should be fed.
#define WATCHDOG_WAKEUP_MS 32000

// Maximum sleep period.
// Note: 5 seconds offset from the watchdog seems a lot
// but, there are some delays at startup which need to be
// accounted for.
#define MAX_SLEEP_SECONDS ((WATCHDOG_WAKEUP_MS / 1000) - 5)

// The maximum size of a history marker.
// The history marker is a string stored in battery-backed SRAM
// which allows us to tell what the system was doing previously.
// On a power-on reset is will contain random garbage.
#define HISTORY_MARKER_MAX_SIZE 6

// History marker indicating that the system was in standby.
#define HISTORY_MARKER_STANDBY "stdby"

// History marker indicating that the system was off.
#define HISTORY_MARKER_OFF "off"

// History marker indicating that the system was running
// normally.
#define HISTORY_MARKER_NORMAL "norm"

// The interval at which we check for exit.
#define BUTTON_CHECK_INTERVAL_MS 1000

// The interval at which we check for LWM2M server
// registration during startup.
#define CLOUD_CLIENT_REGISTRATION_CHECK_INTERVAL_MS 1000

// The minimum Voltage limit that must be set in the battery
// charger chip to make USB operation reliable.
#define MIN_INPUT_VOLTAGE_LIMIT_MV  3880

// The partition on the SD card used by us.
#define IOC_PARTITION "ioc"

// The log file path on the partition.
// Note: I couldn't manage to open files using
// relative paths, hence this is the absolute
// path to the root of our partition.
#define LOG_FILE_PATH "/" IOC_PARTITION

// The log write interval.
#define LOG_WRITE_INTERVAL_MS 1000

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

// The identifiers for each LWM2M object.
//
// To add a new object:
// - create the object (over in ioc-m2m.h/ioc-m2m.cpp),
// - add an entry for it here,
// - add it to IocM2mObjectPointerUnion,
// - add it to addObject() and deleteObject(),
// - instantiate it in init().
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

// Structure defining the things we need to
// access in an LWM2M object.
typedef struct {
    IocM2mObjectPointerUnion object;
    Callback<void(void)> updateObservableResources;
} IocM2mObject;

// Union of socket types.
typedef union {
    TCPSocket *pTcpSock;
    UDPSocket *pUdpSock;
} SocketPointerUnion;

// Local version of config data.
typedef struct {
    time_t initWakeUpTickCounterPeriod;
    int64_t initWakeUpTickCounterModulo;
    time_t readyWakeUpTickCounterPeriod1;
    time_t readyWakeUpTickCounterPeriod2;
    int64_t readyWakeUpTickCounterModulo;
    bool gnssEnable;
} ConfigLocal;

// Local version of temperature data.
typedef struct {
    int32_t nowC;
    int32_t minC;
    int32_t maxC;
} TemperatureLocal;

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

// The local version of diagnostics data.
typedef struct {
    unsigned int worstCaseAudioDatagramSendDuration;
    uint64_t averageAudioDatagramSendDuration;
    uint64_t numAudioDatagrams;
    unsigned int numAudioSendFailures;
    unsigned int numAudioDatagramsSendTookTooLong;
    unsigned int numAudioBytesSent;
} DiagnosticsLocal;

// The local version of logging data.
typedef struct {
    bool loggingToFileEnabled;
    bool loggingUploadEnabled;
    String loggingServerUrl;
} LoggingLocal;

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

/* ----------------------------------------------------------------
 * STATIC FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

// Diagnostics.
static void good();
static void notBad();
static void bad();
static void toggleGreen();
static void event();
static void notEvent();
static void flash();
static void ledOff();
static void heapStats();

// URTP codec and its callback functions.
static void datagramReadyCb(const char * datagram);
static void datagramOverflowStartCb();
static void datagramOverflowStopCb(int numOverflows);

// Audio connection.
static void audioMonitor();
static void getAddressFromUrl(const char * pUrl, char * pAddressBuf, int lenBuf);
static bool getPortFromUrl(const char * pUrl, int *port);
static bool startAudioStreamingConnection(AudioLocal *pAudio);
static void stopAudioStreamingConnection(AudioLocal *pAudio);
static int tcpSend(TCPSocket * pSock, const char * pData, int size);
static void sendAudioData(const AudioLocal * pAudioLocal);

// I2S interface control.
static void i2sEventCallback (int arg);
static bool startI2s(I2S * pI2s);
static void stopI2s(I2S * pI2s);

// Audio control.
static bool startStreaming(AudioLocal *pAudioLocal);
static void stopStreaming(AudioLocal *pAudioLocal);

// GNSS control.
static bool isLeapYear(int year);
static bool gnssInit(GnssSerial * pGnss);
static unsigned int littleEndianUInt(char *pByte);
static bool gnssUpdate(IocM2mLocation::Location *location);

// LWM2M control.
static void addObject(IocM2mObjectId id, void * object);
static void deleteObject(IocM2mObjectId id);
static void setPowerControl(bool value);
static bool getLocationData(IocM2mLocation::Location *data);
static bool getTemperatureData(IocM2mTemperature::Temperature *data);
static void executeResetTemperatureMinMax();
static void setConfigData(const IocM2mConfig::Config *data);
static IocM2mConfig::Config *convertConfigLocalToM2m (IocM2mConfig::Config *pM2m, const ConfigLocal *pLocal);
static void setAudioData(const IocM2mAudio::Audio *m2mAudio);
static bool getStreamingEnabled(bool *streamingEnabled);
static IocM2mAudio::Audio *convertAudioLocalToM2m (IocM2mAudio::Audio *pM2m, const AudioLocal *pLocal);
static bool getDiagnosticsData(IocM2mDiagnostics::Diagnostics *data);
static void objectUpdate();
static void cloudClientRegisteredCallback();
static void cloudClientDeregisteredCallback();
static void cloudClientErrorCallback(int errorCode);

// Misc.
static void buttonCallback();
static void watchdogFeedCallback();

// Initialisation and deinitialisation.
static ResetReason getResetReason();
static void initPower();
static void deinitPower();
static bool initFileSystem();
static void deinitFileSystem();
static void printFccError();
static bool init();
static void deinit();

// Operating modes and sleep
static void setSleepLevelRegisteredSleep(time_t sleepDurationSeconds);
static void setSleepLevelDeregisteredSleep(time_t sleepDurationSeconds);
static void setSleepLevelOff();
static void initialisationModeWakeUpTickHandler();
static void initialisationMode();
static void readyModeInstructionReceived();
static void readyModeWakeUpTickHandler();
static void readyMode();

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// The SD card, which is instantiated in the Mbed Cloud Client
// in pal_plat_fileSystem.cpp.
extern SDBlockDevice sd;

// A file system (noting that Mbed Cloud Client has another
// file system entirely of its own named "sd").
FATFileSystem gFs(IOC_PARTITION, &sd);

// The reason we woke up.
BACKUP_SRAM
static ResetReason gResetReason;

// The low power driver.
static LowPower gLowPower;

// The network interface.
static UbloxPPPCellularInterface *gpCellular = NULL;

// The Mbed Cloud Client stuff.
static CloudClientDm *gpCloudClientDm = NULL;
static UpdateCallback *gpCloudClientGlobalUpdateCallback = NULL;

// Array of LWM2M objects for the control plane.
// NOTE: this array is intended to be indexed using the
// IocM2mObjectId enum.
static IocM2mObject gObjectList[MAX_NUM_IOC_M2M_OBJECTS];

// Storage for a marker in back-up SRAM to indicate
// what was happening before the system started.
BACKUP_SRAM
static char gHistoryMarker[HISTORY_MARKER_MAX_SIZE];

// Put the time that we went to sleep in back-up SRAM.
BACKUP_SRAM
static time_t gTimeEnterSleep;

// Put the time that we should wake from sleep in back-up SRAM.
BACKUP_SRAM
static time_t gTimeLeaveSleep;

// The wake-up tick counter
BACKUP_SRAM
static int gWakeUpTickCounter;

// Configuration data storage.
BACKUP_SRAM
static bool gPowerOnNotOff;
BACKUP_SRAM
static ConfigLocal gConfigLocal;

// Temperature data.
static TemperatureLocal gTemperatureLocal = {0};

// The event loop and event queue.
static Thread *gpEventThread = NULL;
static EventQueue gEventQueue (32 * EVENTS_EVENT_SIZE);

// Event ID for wake-up tick handler.
static int gWakeUpTickHandler = -1;

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

// Buffer that holds one TCP packet.
static char gTcpBuffer[TCP_BUFFER_LENGTH];
static char *gpTcpBuffer = gTcpBuffer;

// Task to send data off to the audio streaming server.
static Thread *gpSendTask = NULL;

// The URTP codec.
static Urtp gUrtp(&datagramReadyCb, &datagramOverflowStartCb, &datagramOverflowStopCb);

// Audio comms parameters.
BACKUP_SRAM
static AudioLocal gAudioLocalPending;
static AudioLocal gAudioLocalActive;

// Flag to indicate that the audio comms channel is up.
static volatile bool gAudioCommsConnected = false;

// The microphone.
static I2S gMic(PB_15, PB_10, PB_9);

// The user button.
static InterruptIn *gpUserButton = NULL;
static volatile bool gUserButtonPressed = false;

// For the watchdog.
// A prescaler value of 256 and a reload
// value of 0x0FFF gives a watchdog
// period of ~32 seconds (see STM32F4 manual
// section 21.3.3)
static IWDG_HandleTypeDef gWdt = {IWDG,
                                  {IWDG_PRESCALER_256, 0x0FFF}};

// Battery monitoring.
static I2C *gpI2C = NULL;
static BatteryGaugeBq27441 *gpBatteryGauge = NULL;
static BatteryChargerBq24295 *gpBatteryCharger = NULL;

// GNSS.
static GnssSerial *gpGnss = NULL;
static char gGnssBuffer[256];
static bool gPendingGnssStop = false;

/// For date conversion.
static const char daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const char daysInMonthLeapYear[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// LEDs for user feedback and diagnostics.
static DigitalOut gLedRed(LED1, 1);
static DigitalOut gLedGreen(LED2, 1);
static DigitalOut gLedBlue(LED3, 1);

// For diagnostics.
static DiagnosticsLocal gDiagnostics = {0};
static Ticker gSecondTicker;
static int gStartTime = 0;

// For logging.
static LoggingLocal gLoggingLocal = {0};
__attribute__ ((section ("CCMRAM")))
static char gLogBuffer[LOG_STORE_SIZE];

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: DIAGNOSTICS
 * -------------------------------------------------------------- */

// Indicate good (green).
static void good() {
    gLedGreen = 0;
    gLedBlue = 1;
    gLedRed = 1;
}

// Switch bad (red) off again.
static void notBad() {
    gLedRed = 1;
}

// Indicate bad (red).
static void bad() {
    gLedRed = 0;
    gLedGreen = 1;
    gLedBlue = 1;
    if (gpEventThread) {
        gEventQueue.call_in(BAD_OFF_PERIOD_MS, notBad);
    }
}

// Toggle green.
static void toggleGreen() {
    gLedGreen = !gLedGreen;
}

// Set blue.
static void event() {
    gLedBlue = 0;
}

// Unset blue.
static void notEvent() {
    gLedBlue = 1;
}

// Flash blue.
static void flash() {
    gLedBlue = !gLedBlue;
    wait_ms(50);
    gLedBlue = !gLedBlue;
}

// All off.
static void ledOff() {
    gLedBlue = 1;
    gLedRed = 1;
    gLedGreen = 1;
}

// Print heap stats.
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

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: AUDIO CONNECTION
 * -------------------------------------------------------------- */

// Monitor on a 1 second tick.
// This is a ticker call-back, so nothing heavy please.
static void audioMonitor()
{
    // Monitor throughput
    if (gDiagnostics.numAudioBytesSent > 0) {
        LOG(EVENT_THROUGHPUT_BITS_S, gDiagnostics.numAudioBytesSent << 3);
        gDiagnostics.numAudioBytesSent = 0;
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
    if (!gpCellular || !gpCellular->is_connected()) {
        bad();
        LOG(EVENT_AUDIO_STREAMING_CONNECTION_START_FAILURE, 0);
        printf("Error, network is not ready.\n");
        return false;
    } else {
        getAddressFromUrl(pAudio->audioServerUrl.c_str(), pBuf, AUDIO_MAX_LEN_SERVER_URL);
        printf("Looking for server URL \"%s\"...\n", pBuf);
        LOG(EVENT_DNS_LOOKUP, 0);
        if (gpCellular->gethostbyname(pBuf, &pAudio->server) == 0) {
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
            nsapiError = pAudio->sock.pTcpSock->open(gpCellular);
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
            nsapiError = pAudio->sock.pUdpSock->open(gpCellular);
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
                    gDiagnostics.numAudioSendFailures++;
                } else {
                    gDiagnostics.numAudioBytesSent += retValue;
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
            gDiagnostics.averageAudioDatagramSendDuration += duration;
            gDiagnostics.numAudioDatagrams++;

            if (duration > BLOCK_DURATION_MS * 1000) {
                // If this is UDP then it's serious, if it's TCP then
                // we can catch up.
                if (pAudioLocal->socketMode == COMMS_UDP) {
                    LOG(EVENT_SEND_DURATION_GREATER_THAN_BLOCK_DURATION, duration);
                }
                gDiagnostics.numAudioDatagramsSendTookTooLong++;
            } else {
                //LOG(EVENT_SEND_DURATION, duration);
            }
            if (duration > gDiagnostics.worstCaseAudioDatagramSendDuration) {
                gDiagnostics.worstCaseAudioDatagramSendDuration = duration;
                LOG(EVENT_NEW_PEAK_SEND_DURATION, gDiagnostics.worstCaseAudioDatagramSendDuration);
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

// Start audio streaming.
// Note: here be multiple return statements.
static bool startStreaming(AudioLocal *pAudioLocal)
{
    int retValue;

    // Start the per-second monitor tick and reset the diagnostics
    LOG(EVENT_AUDIO_STREAMING_START, 0);
    gSecondTicker.attach_us(callback(&audioMonitor), 1000000);
    memset(&gDiagnostics, 0, sizeof (gDiagnostics));

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
    if ((pAudioLocal->duration >= 0) && gpEventThread) {
        printf("Audio streaming will stop in %d second(s).\n", pAudioLocal->duration);
        gEventQueue.call_in(pAudioLocal->duration * 1000, &stopStreaming, pAudioLocal);
    }

    return pAudioLocal->streamingEnabled;
}

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
    gLedGreen = 0;  // Make sure the green LED stays on at
                    // the end as it will have been
                    // toggling throughout
    printf ("Audio send task stopped.\n");

    stopAudioStreamingConnection(pAudioLocal);

    gSecondTicker.detach();

    printf("Audio streaming stopped.\n");
    pAudioLocal->streamingEnabled = false;
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: GNSS
 * -------------------------------------------------------------- */

/// Check if a year is a leap year.
static bool isLeapYear(int year) {
    bool leapYear = false;

    if (year % 400 == 0) {
        leapYear = true;
    } else if (year % 4 == 0) {
        leapYear = true;
    }

    return leapYear;
}

// Initialise the GNSS chip.
static bool gnssInit(GnssSerial * pGnss)
{
    bool success = false;
    Timer timer;
    int length;
    int returnCode;

    if (pGnss->init()) {
        // See ublox7-V14_ReceiverDescrProtSpec section 35.14.3 (CFG-PRT)
        // Switch off NMEA messages as they get in the way
        memset(gGnssBuffer, sizeof (gGnssBuffer), 0);
        gGnssBuffer[0] = 1; // The UART port
        gGnssBuffer[7] = 0x10; // Set Reserved1 bit for compatibility reasons
        gGnssBuffer[13] = 0x01; // UBX protocol only in
        gGnssBuffer[15] = 0x01; // UBX protocol only out
        // Send length is 20 bytes of payload + 6 bytes header + 2 bytes CRC
        if (gpGnss->sendUbx(0x06, 0x00, gGnssBuffer, 20) == 28) {
            timer.start();
            while (!success && (timer.read_ms() < GNSS_COMMS_TIMEOUT_MS)) {
                // Wait for the Ack
                returnCode = gpGnss->getMessage(gGnssBuffer, sizeof(gGnssBuffer));
                if ((returnCode != GnssSerial::WAIT) && (returnCode != GnssSerial::NOT_FOUND)) {
                    length = LENGTH(returnCode);
                    if (((PROTOCOL(returnCode) == GnssSerial::UBX) && (length >= 10))) {
                        // Ack is  0xb5-62-05-00-02-00-msgclass-msgid-crcA-crcB
                        // Nack is 0xb5-62-05-01-02-00-msgclass-msgid-crcA-crcB
                        // (see ublox7-V14_ReceiverDescrProtSpec section 33)
                        if ((gGnssBuffer[0] == 0xb5) &&
                            (gGnssBuffer[1] == 0x62) &&
                            (gGnssBuffer[2] == 0x05) &&
                            (gGnssBuffer[3] == 0x00) &&
                            (gGnssBuffer[4] == 0x02) &&
                            (gGnssBuffer[5] == 0x00) &&
                            (gGnssBuffer[6] == 0x06) &&
                            (gGnssBuffer[7] == 0x00)) {
                            success = true;
                        }
                    }
                }
            }
        }
    }

    return success;
}

/// Derive an unsigned int from a pointer to a
// little-endian unsigned int in memory.
static unsigned int littleEndianUInt(char *pByte) {
    unsigned int value;

    value = *pByte;
    value += ((unsigned int) *(pByte + 1)) << 8;
    value += ((unsigned int) *(pByte + 2)) << 16;
    value += ((unsigned int) *(pByte + 3)) << 24;

    return value;
}

// Callback to update GNSS.
static bool gnssUpdate(IocM2mLocation::Location *location)
{
    bool response = false;
    bool success = false;
    Timer timer;
    int length;
    int returnCode;
    int year;
    int months;
    int gpsTime = 0;

    if (gpGnss) {
        // See ublox7-V14_ReceiverDescrProtSpec section 39.7 (NAV-PVT)
        // Send length is 0 bytes of payload + 6 bytes header + 2 bytes CRC
        if (gpGnss->sendUbx(0x01, 0x07) == 8) {
            timer.start();
            while (!response && (timer.read_ms() < GNSS_COMMS_TIMEOUT_MS)) {
                returnCode = gpGnss->getMessage(gGnssBuffer, sizeof(gGnssBuffer));
                if ((returnCode != GnssSerial::WAIT) && (returnCode != GnssSerial::NOT_FOUND)) {
                    length = LENGTH(returnCode);
                    if ((PROTOCOL(returnCode) == GnssSerial::UBX) && (length >= 84)) {
                        response = true;
                        // Note in what follows that the offsets include 6 bytes of header,
                        // consisting of 0xb5-62-msgclass-msgid-length1-length2.

                        // The time/date is contained at byte offsets as follows:
                        //
                        // 10 - two bytes of year, little-endian (UTC)
                        // 12 - month, range 1..12 (UTC)
                        // 13 - day, range 1..31 (UTC)
                        // 14 - hour, range 0..23 (UTC)
                        // 15 - min, range 0..59 (UTC)
                        // 16 - sec, range 0..60 (UTC)
                        // 17 - validity (0x03 or higher means valid)
                        if ((gGnssBuffer[17] & 0x03) == 0x03) {
                            // Year 1999-2099, so need to adjust to get year since 1970
                            year = ((int) (gGnssBuffer[10])) + ((int) (gGnssBuffer[11]) << 8) - 1999 + 29;
                            // Month (1 to 12), so take away 1 to make it zero-based
                            months = gGnssBuffer[12] - 1;
                            months += year * 12;
                            // Work out the number of seconds due to the year/month count
                            for (int x = 0; x < months; x++) {
                                if (isLeapYear ((x / 12) + 1970)) {
                                    gpsTime += daysInMonthLeapYear[x % 12] * 3600 * 24;
                                } else {
                                    gpsTime += daysInMonth[x % 12] * 3600 * 24;
                                }
                            }
                            // Day (1 to 31)
                            gpsTime += ((int) gGnssBuffer[13] - 1) * 3600 * 24;
                            // Hour (0 to 23)
                            gpsTime += ((int) gGnssBuffer[14]) * 3600;
                            // Minute (0 to 59)
                            gpsTime += ((int) gGnssBuffer[15]) * 60;
                            // Second (0 to 60)
                            gpsTime += gGnssBuffer[16];

                            LOG(EVENT_GNSS_TIMESTAMP, gpsTime);
                            location->timestampUnix = gpsTime;
                            // Update system time
                            gStartTime += gpsTime - time(NULL);
                            set_time(gpsTime);
                            LOG(EVENT_CURRENT_TIME_UTC, time(NULL));
                        }

                        // The fix information is contained at byte offsets as follows:
                        //
                        // 26 - fix type, where 0x02 (2D) or 0x03 (3D) are good enough
                        // 27 - fix status flag, where bit 0 must be set for gnssFixOK
                        // 30 - 4 bytes of longitude, little-endian, in degrees * 10000000
                        // 34 - 4 bytes of latitude, little-endian, in degrees * 10000000
                        // 42 - 4 bytes of height above sea level, little-endian, millimetres
                        // 46 - 4 bytes of horizontal accuracy estimate, little-endian, millimetres
                        // 66 - 4 bytes of speed, little-endian, millimetres/second
                        if (((gGnssBuffer[26] == 0x03) || (gGnssBuffer[26] == 0x02)) &&
                            ((gGnssBuffer[27] & 0x01) == 0x01)) {
                            location->longitudeDegrees = ((float) littleEndianUInt(&(gGnssBuffer[30]))) / 10000000;
                            location->latitudeDegrees = ((float) littleEndianUInt(&(gGnssBuffer[34]))) / 10000000;
                            location->radiusMetres = ((float) littleEndianUInt(&(gGnssBuffer[46]))) / 1000;
                            location->speedMPS = ((float) littleEndianUInt(&(gGnssBuffer[66]))) / 1000;
                            LOG(EVENT_GNSS_LONGITUDE, littleEndianUInt(&(gGnssBuffer[30])));
                            LOG(EVENT_GNSS_LATITUDE, littleEndianUInt(&(gGnssBuffer[34])));
                            LOG(EVENT_GNSS_RADIUS, littleEndianUInt(&(gGnssBuffer[46])));
                            LOG(EVENT_GNSS_SPEED, littleEndianUInt(&(gGnssBuffer[66])));
                            if (gGnssBuffer[26] == 0x03) {
                                location->altitudeMetres = ((float) littleEndianUInt(&(gGnssBuffer[42]))) / 1000;
                                LOG(EVENT_GNSS_ALTITUDE, littleEndianUInt(&(gGnssBuffer[42])));
                            }
                            success = true;
                        }
                    }
                }
            }
        }
    }

    return success;
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
static void deleteObject(IocM2mObjectId id)
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

// Callback that sets the power switch via the IocM2mPowerControl
// object.
static void setPowerControl(bool value)
{
    LOG(EVENT_SET_POWER_CONTROL, value);
    printf("Power control set to %d.\n", value);

    // Something has happened, tell Ready mode about it
    readyModeInstructionReceived();

    gPowerOnNotOff = value;

    // TODO act on it
}

// Callback that retrieves location data for the IocM2mLocation
// object.
static bool getLocationData(IocM2mLocation::Location *data)
{
    return gnssUpdate(data);
}

// Callback that retrieves temperature data for the IocM2mTemperature
// object.
static bool getTemperatureData(IocM2mTemperature::Temperature *data)
{
    bool success = false;

    if (gpBatteryGauge) {
        gpBatteryGauge->getTemperature(&gTemperatureLocal.nowC);
        if (gTemperatureLocal.nowC < gTemperatureLocal.minC) {
            gTemperatureLocal.minC = gTemperatureLocal.nowC;
        }
        if (gTemperatureLocal.nowC > gTemperatureLocal.maxC) {
            gTemperatureLocal.maxC = gTemperatureLocal.nowC;
        }

        data->temperature = (float) gTemperatureLocal.nowC;
        data->minTemperature = (float) gTemperatureLocal.minC;
        data->maxTemperature = (float) gTemperatureLocal.maxC;
        success = true;
    }

    return success;
}

// Callback that executes a reset of the min/max temperature range
// via the IocM2mTemperature object.
static void executeResetTemperatureMinMax()
{
    // Something has happened, tell Ready mode about it
    readyModeInstructionReceived();

    LOG(EVENT_RESET_TEMPERATURE_MIN_MAX, 0);
    printf("Received min/max temperature reset.\n");

    gTemperatureLocal.minC = gTemperatureLocal.nowC;
    gTemperatureLocal.maxC = gTemperatureLocal.nowC;
}

// Callback that sets the configuration data via the IocM2mConfig
// object.
static void setConfigData(const IocM2mConfig::Config *data)
{
    // Something has happened, tell Ready mode about it
    readyModeInstructionReceived();

    printf("Received new config settings:\n");
    printf("  initWakeUpTickCounterPeriod %f.\n", data->initWakeUpTickCounterPeriod);
    printf("  initWakeUpTickCounterModulo %lld.\n", data->initWakeUpTickCounterModulo);
    printf("  readyWakeUpTickCounterPeriod1 %f.\n", data->readyWakeUpTickCounterPeriod1);
    printf("  readyWakeUpTickCounterPeriod2 %f.\n", data->readyWakeUpTickCounterPeriod2);
    printf("  readyWakeUpTickCounterModulo %lld.\n", data->readyWakeUpTickCounterModulo);
    printf("  GNSS enable %d.\n", data->gnssEnable);

    /// Handle GNSS configuration changes
    if (!gpGnss && data->gnssEnable) {
        gPendingGnssStop = false;
        LOG(EVENT_GNSS_START, 0);
        gpGnss = new GnssSerial();
        if (!gpGnss->init()) {
            LOG(EVENT_GNSS_START_FAILURE, 0);
            delete gpGnss;
            gpGnss = NULL;
        }
    } else if (gpGnss && !data->gnssEnable) {
        LOG(EVENT_GNSS_STOP_PENDING, 0);
        gPendingGnssStop = true;
    }

    // TODO act on the rest of it

    gConfigLocal.initWakeUpTickCounterPeriod = (time_t) data->initWakeUpTickCounterPeriod;
    gConfigLocal.initWakeUpTickCounterModulo = data->initWakeUpTickCounterModulo;
    gConfigLocal.readyWakeUpTickCounterPeriod1 = (time_t) data->readyWakeUpTickCounterPeriod1;
    gConfigLocal.readyWakeUpTickCounterPeriod2 = (time_t) data->readyWakeUpTickCounterPeriod2;
    gConfigLocal.readyWakeUpTickCounterModulo = data->readyWakeUpTickCounterModulo;
    gConfigLocal.gnssEnable = data->gnssEnable;
    LOG(EVENT_SET_INIT_WAKE_UP_TICK_COUNTER_PERIOD, gConfigLocal.initWakeUpTickCounterPeriod);
    LOG(EVENT_SET_INIT_WAKE_UP_TICK_COUNTER_MODULO, gConfigLocal.initWakeUpTickCounterModulo);
    LOG(EVENT_SET_READY_WAKE_UP_TICK_COUNTER_PERIOD1, gConfigLocal.readyWakeUpTickCounterPeriod1);
    LOG(EVENT_SET_READY_WAKE_UP_TICK_COUNTER_PERIOD2, gConfigLocal.readyWakeUpTickCounterPeriod2);
    LOG(EVENT_SET_READY_WAKE_UP_TICK_COUNTER_MODULO, gConfigLocal.readyWakeUpTickCounterModulo);
}

// Convert a local config data structure to the IocM2mConfig one.
static IocM2mConfig::Config *convertConfigLocalToM2m (IocM2mConfig::Config *pM2m, const ConfigLocal *pLocal)
{
    pM2m->initWakeUpTickCounterPeriod = (float) pLocal->initWakeUpTickCounterPeriod;
    pM2m->initWakeUpTickCounterModulo = pLocal->initWakeUpTickCounterModulo;
    pM2m->readyWakeUpTickCounterPeriod1 = (float) pLocal->readyWakeUpTickCounterPeriod1;
    pM2m->readyWakeUpTickCounterPeriod2 = (float) pLocal->readyWakeUpTickCounterPeriod2;
    pM2m->readyWakeUpTickCounterModulo = pLocal->readyWakeUpTickCounterModulo;
    pM2m->gnssEnable = pLocal->gnssEnable;

    return pM2m;
}

// Callback that sets the configuration data via the IocM2mAudio
// object.
static void setAudioData(const IocM2mAudio::Audio *m2mAudio)
{
    bool streamingWasEnabled = gAudioLocalPending.streamingEnabled;

    // Something has happened, tell Ready mode about it
    readyModeInstructionReceived();

    printf("Received new audio parameters:\n");
    printf("  streamingEnabled %d.\n", m2mAudio->streamingEnabled);
    printf("  duration %f.\n", m2mAudio->duration);
    printf("  fixedGain %f.\n", m2mAudio->fixedGain);
    printf("  audioCommunicationsMode %lld.\n", m2mAudio->audioCommunicationsMode);
    printf("  audioServerUrl \"%s\".\n", m2mAudio->audioServerUrl.c_str());

    gAudioLocalPending.streamingEnabled = m2mAudio->streamingEnabled;
    gAudioLocalPending.fixedGain = (int) m2mAudio->fixedGain;
    gAudioLocalPending.duration = (int) m2mAudio->duration;
    gAudioLocalPending.socketMode = m2mAudio->audioCommunicationsMode;
    gAudioLocalPending.audioServerUrl = m2mAudio->audioServerUrl;
    LOG(EVENT_SET_AUDIO_CONFIG_FIXED_GAIN, gAudioLocalPending.fixedGain);
    LOG(EVENT_SET_AUDIO_CONFIG_DURATION, gAudioLocalPending.duration);
    LOG(EVENT_SET_AUDIO_CONFIG_COMUNICATIONS_MODE, gAudioLocalPending.socketMode);
    if (m2mAudio->streamingEnabled && !streamingWasEnabled) {
        LOG(EVENT_SET_AUDIO_CONFIG_STREAMING_ENABLED, 0);
        // Make a copy of the current audio settings so that
        // the streaming process cannot be affected by server writes
        // unless it is switched off and on again
        gAudioLocalActive = gAudioLocalPending;
        gAudioLocalPending.streamingEnabled = startStreaming(&gAudioLocalActive);
    } else if (!m2mAudio->streamingEnabled && streamingWasEnabled) {
        LOG(EVENT_SET_AUDIO_CONFIG_STREAMING_DISABLED, 0);
        stopStreaming(&gAudioLocalActive);
        gAudioLocalPending.streamingEnabled = gAudioLocalActive.streamingEnabled;
        // Update the diagnostics straight away as they will have been
        // modified during the streaming session
        if (gObjectList[IOC_M2M_DIAGNOSTICS].updateObservableResources) {
            gObjectList[IOC_M2M_DIAGNOSTICS].updateObservableResources();
        }
    }
}

// Callback that retrieves the state of streamingEnabled
static bool getStreamingEnabled(bool *streamingEnabled)
{
    *streamingEnabled = gAudioLocalActive.streamingEnabled;

    return true;
}

// Convert a local audio data structure to the IocM2mAudio one.
static IocM2mAudio::Audio *convertAudioLocalToM2m (IocM2mAudio::Audio *pM2m, const AudioLocal *pLocal)
{
    pM2m->streamingEnabled = pLocal->streamingEnabled;
    pM2m->duration = (float) pLocal->duration;
    pM2m->fixedGain = (float) pLocal->fixedGain;
    pM2m->audioCommunicationsMode = pLocal->socketMode;
    pM2m->audioServerUrl = pLocal->audioServerUrl;

    return pM2m;
}

// Callback that gets diagnostic data for the IocM2mDiagnostics object.
static bool getDiagnosticsData(IocM2mDiagnostics::Diagnostics *data)
{
    if (gStartTime > 0) {
        data->upTime = time(NULL) - gStartTime;
    } else {
        data->upTime = 0;
    }
    data->resetReason = gResetReason;
    data->worstCaseSendDuration = (float) gDiagnostics.worstCaseAudioDatagramSendDuration / 1000000;
    data->averageSendDuration = (float) (gDiagnostics.averageAudioDatagramSendDuration /
                                         gDiagnostics.numAudioDatagrams) / 1000000;
    data->minNumDatagramsFree = gUrtp.getUrtpDatagramsFreeMin();
    data->numSendFailures = gDiagnostics.numAudioSendFailures;
    data->percentageSendsTooLong = (int64_t) gDiagnostics.numAudioDatagramsSendTookTooLong * 100 /
                                             gDiagnostics.numAudioDatagrams;

    return true;
}

// Callback to update the observable values in all of the LWM2M objects.
static void objectUpdate()
{
    int32_t voltageMV;
    int32_t currentMA;
    int32_t batteryLevelPercent;
    CloudClientDm::BatteryStatus batteryStatus = CloudClientDm::BATTERY_STATUS_UNKNOWN;
    char fault;

    // First do the observable resources for the Device object
    LOG(EVENT_LWM2M_OBJECT_UPDATE, 0);
    flash();
    if (gpCloudClientDm) {
        if (gpBatteryGauge && gpBatteryGauge->isBatteryDetected()) {
            if (gpBatteryGauge->getVoltage(&voltageMV)) {
                LOG(EVENT_BATTERY_VOLTAGE, voltageMV);
                gpCloudClientDm->setDeviceObjectVoltage(CloudClientDm::POWER_SOURCE_INTERNAL_BATTERY,
                                                        voltageMV);
            }
            if (gpBatteryGauge->getCurrent(&currentMA)) {
                LOG(EVENT_BATTERY_CURRENT, currentMA);
                gpCloudClientDm->setDeviceObjectCurrent(CloudClientDm::POWER_SOURCE_INTERNAL_BATTERY,
                                                        currentMA);
            }
            if (gpBatteryGauge->getRemainingPercentage(&batteryLevelPercent)) {
                LOG(EVENT_BATTERY_PERCENTAGE, batteryLevelPercent);
                gpCloudClientDm->setDeviceObjectBatteryLevel(batteryLevelPercent);
            }
        }
        if (gpBatteryCharger) {
            // Make sure we are lined up with the USB power state
            if (gpBatteryCharger->isExternalPowerPresent() &&
                !gpCloudClientDm->existsDeviceObjectPowerSource(CloudClientDm::POWER_SOURCE_USB)) {
                LOG(EVENT_EXTERNAL_POWER_ON, 0);
                gpCloudClientDm->addDeviceObjectPowerSource(CloudClientDm::POWER_SOURCE_USB);
            } else if (!gpBatteryCharger->isExternalPowerPresent() &&
                       gpCloudClientDm->existsDeviceObjectPowerSource(CloudClientDm::POWER_SOURCE_USB)) {
                LOG(EVENT_EXTERNAL_POWER_OFF, 0);
                gpCloudClientDm->deleteDeviceObjectPowerSource(CloudClientDm::POWER_SOURCE_USB);
            }

            fault = gpBatteryCharger->getChargerFaults();
            // Don't care about battery charger watchdog timer
            fault &= ~BatteryChargerBq24295::CHARGER_FAULT_WATCHDOG_EXPIRED;
            if (fault != BatteryChargerBq24295::CHARGER_FAULT_NONE) {
                batteryStatus = CloudClientDm::BATTERY_STATUS_FAULT;
                LOG(EVENT_BATTERY_STATUS_FAULT, fault);
            } else {
                if (batteryLevelPercent < LOW_BATTERY_WARNING_PERCENTAGE) {
                    batteryStatus = CloudClientDm::BATTERY_STATUS_LOW_BATTERY;
                    LOG(EVENT_BATTERY_STATUS_LOW_BATTERY, batteryLevelPercent);
                } else {
                    switch (gpBatteryCharger->getChargerState()) {
                        case BatteryChargerBq24295::CHARGER_STATE_DISABLED:
                            LOG(EVENT_BATTERY_STATUS_NORMAL, BatteryChargerBq24295::CHARGER_STATE_DISABLED);
                            batteryStatus = CloudClientDm::BATTERY_STATUS_NORMAL;
                            break;
                        case BatteryChargerBq24295::CHARGER_STATE_NO_EXTERNAL_POWER:
                            LOG(EVENT_BATTERY_STATUS_NORMAL, BatteryChargerBq24295::CHARGER_STATE_NO_EXTERNAL_POWER);
                            batteryStatus = CloudClientDm::BATTERY_STATUS_NORMAL;
                            break;
                        case BatteryChargerBq24295::CHARGER_STATE_NOT_CHARGING:
                            LOG(EVENT_BATTERY_STATUS_NORMAL, BatteryChargerBq24295::CHARGER_STATE_NOT_CHARGING);
                            batteryStatus = CloudClientDm::BATTERY_STATUS_NORMAL;
                            break;
                        case BatteryChargerBq24295::CHARGER_STATE_PRECHARGE:
                            LOG(EVENT_BATTERY_STATUS_CHARGING, BatteryChargerBq24295::CHARGER_STATE_PRECHARGE);
                            batteryStatus = CloudClientDm::BATTERY_STATUS_CHARGING;
                            break;
                        case BatteryChargerBq24295::CHARGER_STATE_FAST_CHARGE:
                            LOG(EVENT_BATTERY_STATUS_CHARGING, BatteryChargerBq24295::CHARGER_STATE_FAST_CHARGE);
                            batteryStatus = CloudClientDm::BATTERY_STATUS_CHARGING;
                            break;
                        case BatteryChargerBq24295::CHARGER_STATE_COMPLETE:
                            LOG(EVENT_BATTERY_STATUS_CHARGING_COMPLETE, BatteryChargerBq24295::CHARGER_STATE_COMPLETE);
                            batteryStatus = CloudClientDm::BATTERY_STATUS_CHARGING_COMPLETE;
                            break;
                        default:
                            LOG(EVENT_BATTERY_STATUS_UNKNOWN, 0);
                            batteryStatus = CloudClientDm::BATTERY_STATUS_UNKNOWN;
                            break;
                    }
                }
            }
        }
        gpCloudClientDm->setDeviceObjectBatteryStatus(batteryStatus);
    }

    // Check if there's been a request to switch off GNSS before
    // we go observing it.
    if (gPendingGnssStop) {
        if (gpGnss) {
            LOG(EVENT_GNSS_STOP, 0);
            delete gpGnss;
            gpGnss = NULL;
        }
        gPendingGnssStop = false;
    }

    // Now do all the other observable resources
    for (unsigned int x = 0; x < sizeof (gObjectList) /
                                 sizeof (gObjectList[0]); x++) {
        if (gObjectList[x].updateObservableResources) {
            gObjectList[x].updateObservableResources();
        }
    }
}

// Callback when mbed Cloud Client registers with the LWM2M server.
static void cloudClientRegisteredCallback()
{
    flash();
    good();
    LOG(EVENT_CLOUD_CLIENT_REGISTERED, 0);
    printf("Mbed Cloud Client is registered, press the user button to exit.\n");

    // The registration process may update system time so
    // read the start time now.
    if (time(NULL) > (signed int) __COMPILE_TIME_UNIX__) {
        gStartTime = time(NULL);
        LOG(EVENT_CURRENT_TIME_UTC, time(NULL));
    }
}

// Callback when mbed Cloud Client deregisters from the LWM2M server.
static void cloudClientDeregisteredCallback()
{
    flash();
    LOG(EVENT_CLOUD_CLIENT_DEREGISTERED, 0);
    printf("Mbed Cloud Client deregistered.\n");
}

// Callback when an error occurs in mbed Cloud Client.
static void cloudClientErrorCallback(int errorCode)
{
    flash();
    LOG(EVENT_CLOUD_CLIENT_ERROR, errorCode);
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: MISC
 * -------------------------------------------------------------- */

// Function to attach to the user button.
static void buttonCallback()
{
    gUserButtonPressed = true;
    LOG(EVENT_BUTTON_PRESSED, 0);
}

// Normally we feed the watchdog in task-context
// so that we can be sure that tasks are alive.
// However, sometimes it is not possible to do that
// (e.g. while waiting for cellular to register)
// and so in those circumstances this callback
// can be called on a ticker.
static void watchdogFeedCallback()
{
    HAL_IWDG_Refresh(&gWdt);
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: INITIALISATION AND DEINITIALISATION
 * -------------------------------------------------------------- */

// Find out what woke us up
static ResetReason getResetReason()
{
    ResetReason reason = RESET_REASON_UNKNOWN;

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST)) {
        reason = RESET_REASON_POWER_ON;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)) {
        reason = RESET_REASON_SOFTWARE;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) {
        reason = RESET_REASON_WATCHDOG;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST)) {
        reason = RESET_REASON_PIN;
    } else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)) {
        reason = RESET_REASON_LOW_POWER;
    }

    __HAL_RCC_CLEAR_RESET_FLAGS();

    return reason;
}

// Initialise power.
static void initPower()
{
    flash();
    LOG(EVENT_I2C_START, 0);
    gpI2C = new I2C(I2C_SDA_B, I2C_SCL_B);
    LOG(EVENT_BATTERY_CHARGER_BQ24295_START, 0);
    gpBatteryCharger = new BatteryChargerBq24295();
    if (gpBatteryCharger->init(gpI2C)) {
        if (!gpBatteryCharger->enableCharging() ||
            !gpBatteryCharger->setInputVoltageLimit(MIN_INPUT_VOLTAGE_LIMIT_MV) ||
            !gpBatteryCharger->setWatchdog(0)) {
            bad();
            LOG(EVENT_BATTERY_CHARGER_BQ24295_CONFIG_FAILURE, 0);
            printf ("WARNING: unable to completely configure battery charger.\n");
        }
    } else {
        bad();
        LOG(EVENT_BATTERY_CHARGER_BQ24295_START_FAILURE, 0);
        printf ("WARNING: unable to initialise battery charger.\n");
        delete gpBatteryCharger;
        gpBatteryCharger = NULL;
    }
    LOG(EVENT_BATTERY_GAUGE_BQ27441_START, 0);
    gpBatteryGauge = new BatteryGaugeBq27441();
    if (gpBatteryGauge->init(gpI2C)) {
        if (!gpBatteryGauge->disableBatteryDetect() ||
            !gpBatteryGauge->enableGauge()) {
            bad();
            LOG(EVENT_BATTERY_GAUGE_BQ27441_CONFIG_FAILURE, 0);
            printf ("WARNING: unable to completely configure battery gauge.\n");
        }
        // Reset the temperature min/max readings which are read from the gauge
        if (gpBatteryGauge->getTemperature(&gTemperatureLocal.nowC)) {
            gTemperatureLocal.minC = gTemperatureLocal.nowC;
            gTemperatureLocal.maxC = gTemperatureLocal.nowC;
        }
    } else {
        bad();
        LOG(EVENT_BATTERY_GAUGE_BQ27441_START_FAILURE, 0);
        printf ("WARNING: unable to initialise battery gauge (maybe the battery is not connected?).\n");
        delete gpBatteryGauge;
        gpBatteryGauge = NULL;
    }

    // Only need I2C for battery stuff, so if neither work just delete it
    if (!gpBatteryGauge && !gpBatteryCharger) {
        LOG(EVENT_I2C_STOP, 0);
        delete gpI2C;
        gpI2C = NULL;
    }
}

// Deinitialise power.
static void deinitPower()
{
    if (gpBatteryCharger) {
        flash();
        LOG(EVENT_BATTERY_CHARGER_BQ24295_STOP, 0);
        printf("Stopping battery charger...\n");
        delete gpBatteryCharger;
        gpBatteryCharger = NULL;
    }

    if (gpBatteryGauge) {
        flash();
        LOG(EVENT_BATTERY_GAUGE_BQ27441_STOP, 0);
        printf("Stopping battery gauge...\n");
        gpBatteryGauge->disableGauge();
        delete gpBatteryGauge;
        gpBatteryGauge = NULL;
    }

    if (gpI2C) {
        flash();
        LOG(EVENT_I2C_STOP, 0);
        printf("Stopping I2C...\n");
        delete gpI2C;
        gpI2C = NULL;
    }
}

// Initialise file system.
static bool initFileSystem()
{
    int x;

    // Set up logging defaults here
    gLoggingLocal.loggingToFileEnabled = LOGGING_DEFAULT_TO_FILE_ENABLED;
    gLoggingLocal.loggingUploadEnabled = LOGGING_DEFAULT_UPLOAD_ENABLED;
    gLoggingLocal.loggingServerUrl = LOGGING_DEFAULT_SERVER_URL;

    flash();
    LOG(EVENT_SD_CARD_START, 0);
    printf("Starting SD card...\n");
    x = sd.init();
    if (x == 0) {
        printf("Mounting file system...\n");
        gFs.mount(&sd);
    } else {
        bad();
        LOG(EVENT_SD_CARD_START_FAILURE, 0);
        printf("Error initialising SD card (%d).\n", x);
        return false;
    }
    printf("SD card started.\n");

    if (gLoggingLocal.loggingToFileEnabled) {
        flash();
        printf("Starting logging to file...\n");
        if (initLogFile(LOG_FILE_PATH)) {
            gEventQueue.call_every(LOG_WRITE_INTERVAL_MS, writeLog);
        } else {
            printf("WARNING: unable to initialise logging to file.\n");
        }
    }

    return true;
}

// Shutdown file system
static void deinitFileSystem()
{
    flash();
    LOG(EVENT_SD_CARD_STOP, 0);
    printf("Closing SD card and unmounting file system...\n");
    sd.deinit();
    gFs.unmount();
}

// Get the error messages from Cloud Client FCC
// and print them out for feedback to the mbed Cloud people.
static void printFccError()
{
    fcc_output_info_s *pFccError;
    fcc_warning_info_s *pFccWarning;

    pFccError = fcc_get_error_and_warning_data();
    if ((pFccError != NULL) && (pFccError->error_string_info != NULL)) {
        printf("FCC reported the follow error: \"%s\".\n", pFccError->error_string_info);
    }
    for (unsigned int z = 0; z < pFccError->size_of_warning_info_list; z++) {
        pFccWarning = pFccError->head_of_warning_list;
        if ((pFccWarning != NULL) && (pFccWarning->warning_info_string != NULL)) {
            printf("FCC reported the following warning: %d \"%s\".\n", z + 1, pFccWarning->warning_info_string);
        }
        pFccWarning = pFccWarning->next;
    }
}

// Initialise everything, bringing us to SleepLevel REGISTERED.
// If you add anything here, be sure to add the opposite
// to deinit().
// Note: here be multiple return statements.
static bool init()
{
    bool cloudClientConfigGood = false;
    bool dmObjectConfigGood = false;
    int x = 0;
    int y = 0;

    LOG(EVENT_WATCHDOG_START, 0);
    printf("Starting watchdog timer (%d seconds)...\n", WATCHDOG_WAKEUP_MS / 1000);
    if (HAL_IWDG_Init(&gWdt) != HAL_OK) {
        bad();
        LOG(EVENT_WATCHDOG_START_FAILURE, 0);
        printf("WARNING: unable to initialise watchdog, it is NOT running.\n");
    }

    // Set up defaults
    gAudioLocalPending.streamingEnabled = AUDIO_DEFAULT_STREAMING_ENABLED;
    gAudioLocalPending.duration = AUDIO_DEFAULT_DURATION;
    gAudioLocalPending.fixedGain = AUDIO_DEFAULT_FIXED_GAIN;
    gAudioLocalPending.socketMode = AUDIO_DEFAULT_COMMUNICATION_MODE;
    gAudioLocalPending.audioServerUrl = AUDIO_DEFAULT_SERVER_URL;
    gAudioLocalPending.sock.pTcpSock = NULL;

    flash();
    printf("Creating user button...\n");
    gpUserButton = new InterruptIn(SW0);
    gpUserButton->rise(&buttonCallback);

    if (CONFIG_DEFAULT_GNSS_ENABLE) {
        flash();
        LOG(EVENT_GNSS_START, 0);
        printf("Starting GNSS...\n");
        gpGnss = new GnssSerial();
        if (!gnssInit(gpGnss)) {
            bad();
            LOG(EVENT_GNSS_START_FAILURE, 0);
            printf ("WARNING: unable to initialise GNSS.\n");
            delete gpGnss;
            gpGnss = NULL;
        }
    }

    flash();
    LOG(EVENT_CLOUD_CLIENT_FILE_STORAGE_INIT, 0);
    printf("Initialising Mbed Cloud Client file storage...\n");
    fcc_status_e status = fcc_init();
    if(status != FCC_STATUS_SUCCESS) {
        bad();
        printFccError();
        LOG(EVENT_CLOUD_CLIENT_FILE_STORAGE_INIT_FAILURE, 0);
        printf("Error initialising Mbed Cloud Client file storage (%d).\n", status);
        return false;
    }
    printf("Mbed Cloud Client file storage initialised.\n");

    // I have seen device object configuration attempts fail due
    // to storage errors so, if one occurs, try a second time.
    for (y = 0; (y < 2) && !dmObjectConfigGood; y++) {
        // Strictly speaking, the process here should be to look for the
        // configuration files, which would have been set up at the factory
        // and if they are not correct to bomb out.  However, we're running
        // in developer mode and sometimes the Mbed Cloud Client code
        // seems unhappy with the credentials stored on SD card for some
        // reason.  Under these circumstances it is better to create new ones
        // and get on with it, otherwise the device is lost to us.  So we try
        // once and, if the credentials are bad, we reset the Mbed Cloud
        // Client storage and try again one more time.
        for (x = 0; (x < 2) && !cloudClientConfigGood; x++) {
            HAL_IWDG_Refresh(&gWdt);
#ifdef MBED_CONF_APP_DEVELOPER_MODE
            flash();
            LOG(EVENT_CLOUD_CLIENT_DEVELOPER_FLOW_START, 0);
            printf("Starting Mbed Cloud Client developer flow...\n");
            status = fcc_developer_flow();
            if (status == FCC_STATUS_KCM_FILE_EXIST_ERROR) {
                printf("Mbed Cloud Client developer credentials already exist.\n");
            } else if (status != FCC_STATUS_SUCCESS) {
                bad();
                printFccError();
                LOG(EVENT_CLOUD_CLIENT_DEVELOPER_FLOW_START_FAILURE, 0);
                printf("Failed to load Mbed Cloud Client developer credentials.\n");
                return false;
            }
#endif

            flash();
            LOG(EVENT_CLOUD_CLIENT_VERIFY_CONFIG_FILES, 0);
            printf("Checking Mbed Cloud Client configuration files...\n");
            status = fcc_verify_device_configured_4mbed_cloud();
            if (status == FCC_STATUS_SUCCESS) {
                cloudClientConfigGood = true;
            } else {
                printFccError();
                LOG(EVENT_CLOUD_CLIENT_VERIFY_CONFIG_FILES_FAILURE, 0);
                printf("Device not configured for Mbed Cloud Client.\n");

#ifdef MBED_CONF_APP_DEVELOPER_MODE
                // Use this function when you want to clear storage from all
                // the factory-tool generated data and user data.
                // After this operation device must be injected again by using
                // factory tool or developer certificate.
                flash();
                LOG(EVENT_CLOUD_CLIENT_RESET_STORAGE, 0);
                printf("Resetting Mbed Cloud Client storage to an empty state...\n");
                fcc_status_e deleteStatus = fcc_storage_delete();
                if (deleteStatus != FCC_STATUS_SUCCESS) {
                    bad();
                    printFccError();
                    LOG(EVENT_CLOUD_CLIENT_RESET_STORAGE_FAILURE, 0);
                    printf("Failed to delete Mbed Cloud Client storage - %d\n", deleteStatus);
                    return false;
                }
#endif
            }
        }

        // Note sure if this is required or not; it doesn't do any harm.
        srand(time(NULL));

        flash();
        LOG(EVENT_CLOUD_CLIENT_INIT_DM, 0);
        printf("Initialising Mbed Cloud Client DM...\n");
        gpCloudClientDm = new CloudClientDm(MBED_CONF_APP_OBJECT_DEBUG_ON,
                                            &cloudClientRegisteredCallback,
                                            &cloudClientDeregisteredCallback,
                                            &cloudClientErrorCallback);

        flash();
        printf("Configuring the LWM2M Device object...\n");
        LOG(EVENT_CLOUD_CLIENT_CONFIG_DM, 0);
        if (gpCloudClientDm->setDeviceObjectStaticDeviceType(DEVICE_OBJECT_DEVICE_TYPE) &&
            gpCloudClientDm->setDeviceObjectStaticSerialNumber(DEVICE_OBJECT_SERIAL_NUMBER) &&
            gpCloudClientDm->setDeviceObjectStaticHardwareVersion(DEVICE_OBJECT_HARDWARE_VERSION) &&
            gpCloudClientDm->setDeviceObjectSoftwareVersion(DEVICE_OBJECT_SOFTWARE_VERSION) &&
            gpCloudClientDm->setDeviceObjectFirmwareVersion(DEVICE_OBJECT_FIRMWARE_VERSION) &&
            gpCloudClientDm->addDeviceObjectPowerSource(CloudClientDm::POWER_SOURCE_INTERNAL_BATTERY) &&
            gpCloudClientDm->setDeviceObjectMemoryTotal(DEVICE_OBJECT_MEMORY_TOTAL) &&
            gpCloudClientDm->setDeviceObjectUtcOffset(DEVICE_OBJECT_UTC_OFFSET) &&
            gpCloudClientDm->setDeviceObjectTimezone(DEVICE_OBJECT_TIMEZONE)) {
            if (gpBatteryCharger && gpBatteryCharger->isExternalPowerPresent()) {
                dmObjectConfigGood = gpCloudClientDm->addDeviceObjectPowerSource(CloudClientDm::POWER_SOURCE_USB);
            } else {
                dmObjectConfigGood = true;
            }
        } else {
            cloudClientConfigGood = false;
            printf("Unable to configure the Device object.\n");
        }
    }

    if (!dmObjectConfigGood) {
        bad();
        LOG(EVENT_CLOUD_CLIENT_CONFIG_DM_FAILURE, 0);
        printf("Unable to configure the Device object after %d attempt(s).\n", y - 1);
        return false;
    }

    flash();
    LOG(EVENT_CREATE_LWM2M_OBJECTS, 0);
    printf("Creating all the other LWM2M objects...\n");
    // Create temporary storage for copying things around
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
    IocM2mConfig::Config *pTempStore1 = new IocM2mConfig::Config;
    addObject(IOC_M2M_CONFIG, new IocM2mConfig(setConfigData,
                                               convertConfigLocalToM2m(pTempStore1, &gConfigLocal),
                                               MBED_CONF_APP_OBJECT_DEBUG_ON));
    delete pTempStore1;
    IocM2mAudio::Audio *pTempStore2 = new IocM2mAudio::Audio;
    addObject(IOC_M2M_AUDIO, new IocM2mAudio(setAudioData,
                                             getStreamingEnabled,
                                             convertAudioLocalToM2m(pTempStore2, &gAudioLocalPending),
                                             MBED_CONF_APP_OBJECT_DEBUG_ON));
    delete pTempStore2;
    addObject(IOC_M2M_DIAGNOSTICS, new IocM2mDiagnostics(getDiagnosticsData,
                                                         MBED_CONF_APP_OBJECT_DEBUG_ON));

    flash();
    LOG(EVENT_CLOUD_CLIENT_START, 0);
    printf("Starting Mbed Cloud Client...\n");
    gpCloudClientGlobalUpdateCallback = new UpdateCallback();
    if (!gpCloudClientDm->start(gpCloudClientGlobalUpdateCallback)) {
        bad();
        LOG(EVENT_CLOUD_CLIENT_START_FAILURE, 0);
        printf("Error starting Mbed Cloud Client.\n");
        return false;
    }

    HAL_IWDG_Refresh(&gWdt);
    flash();
    LOG(EVENT_MODEM_START, 0);
    printf("Initialising modem...\n");
    gpCellular = new UbloxPPPCellularInterface(MDMTXD, MDMRXD, MODEM_BAUD_RATE,
                                               MBED_CONF_APP_MODEM_DEBUG_ON);
    if (!gpCellular->init()) {
        bad();
        LOG(EVENT_MODEM_START_FAILURE, 0);
        printf("Unable to initialise cellular.\n");
        return false;
    }

    // Run a ticker to feed the watchdog while we wait for registration
    gSecondTicker.attach_us(callback(&watchdogFeedCallback), 1000000);

    flash();
    LOG(EVENT_NETWORK_CONNECTING, 0);
    printf("Please wait up to 180 seconds to connect to the cellular packet network...\n");
    if (gpCellular->connect() != NSAPI_ERROR_OK) {
        gSecondTicker.detach();
        bad();
        LOG(EVENT_NETWORK_CONNECTION_FAILURE, 0);
        printf("Unable to connect to the cellular packet network.\n");
        return false;
    }
    gSecondTicker.detach();
    LOG(EVENT_NETWORK_CONNECTED, 0);

    flash();
    LOG(EVENT_CLOUD_CLIENT_CONNECTING, 0);
    printf("Connecting to LWM2M server...\n");
    if (!gpCloudClientDm->connect(gpCellular)) {
        bad();
        LOG(EVENT_CLOUD_CLIENT_CONNECT_FAILURE, 0);
        printf("Unable to connect to LWM2M server.\n");
        return false;
    } else {
        LOG(EVENT_CLOUD_CLIENT_CONNECTED, 0);
        printf("Connected to LWM2M server, please wait for registration to complete...\n");
        // !!! SUCCESS !!!
    }

    return true;
}

// Shut everything down.
// Anything that was set up in init() should be cleared here.
static void deinit()
{
    HAL_IWDG_Refresh(&gWdt);
    if (gAudioLocalActive.streamingEnabled) {
        flash();
        printf("Stopping streaming...\n");
        stopStreaming(&gAudioLocalActive);
        gAudioLocalPending.streamingEnabled = gAudioLocalActive.streamingEnabled;
    }

    if (gpCloudClientDm != NULL) {
        flash();
        LOG(EVENT_CLOUD_CLIENT_DISCONNECTING, 0);
        printf("Stopping Mbed Cloud Client...\n");
        gpCloudClientDm->stop();
        LOG(EVENT_CLOUD_CLIENT_DISCONNECTED, 0);
    }

    flash();
    LOG(EVENT_DELETE_LWM2M_OBJECTS, 0);
    printf("Deleting LWM2M objects...\n");
    for (unsigned int x = 0; x < sizeof (gObjectList) / sizeof (gObjectList[0]); x++) {
        deleteObject((IocM2mObjectId) x);
    }

    if (gpCloudClientDm != NULL) {
        flash();
        LOG(EVENT_CLOUD_CLIENT_DELETE, 0);
        printf("Deleting Mbed Cloud Client...\n");
        delete gpCloudClientDm;
        gpCloudClientDm = NULL;
        if (gpCloudClientGlobalUpdateCallback != NULL) {
            delete gpCloudClientGlobalUpdateCallback;
            gpCloudClientGlobalUpdateCallback = NULL;
        }
    }

    if (gpCellular != NULL) {
        HAL_IWDG_Refresh(&gWdt);
        flash();
        LOG(EVENT_NETWORK_DISCONNECTING, 0);
        printf("Disconnecting from the cellular packet network...\n");
        gpCellular->disconnect();
        flash();
        LOG(EVENT_NETWORK_DISCONNECTED, 0);
        LOG(EVENT_MODEM_STOP, 0);
        printf("Stopping modem...\n");
        gpCellular->deinit();
        delete gpCellular;
        gpCellular = NULL;
    }

    if (gpUserButton != NULL) {
        flash();
        printf("Removing user button...\n");
        delete gpUserButton;
        gpUserButton = NULL;
    }

    if (gpGnss) {
        flash();
        LOG(EVENT_GNSS_STOP, 0);
        printf ("Stopping GNSS...\n");
        delete gpGnss;
        gpGnss = NULL;
    }

    if (gStartTime > 0) {
        printf("Up for %ld second(s).\n", time(NULL) - gStartTime);
    }

    if (gDiagnostics.numAudioDatagrams > 0) {
        printf("Stats:\n");
        printf("Worst case time to perform a send: %u us.\n", gDiagnostics.worstCaseAudioDatagramSendDuration);
        printf("Average time to perform a send: %llu us.\n", gDiagnostics.averageAudioDatagramSendDuration /
                                                             gDiagnostics.numAudioDatagrams);
        printf("Minimum number of datagram(s) free %d.\n", gUrtp.getUrtpDatagramsFreeMin());
        printf("Number of send failure(s) %d.\n", gDiagnostics.numAudioSendFailures);
        printf("%d send(s) took longer than %d ms (%llu%% of the total).\n",
               gDiagnostics.numAudioDatagramsSendTookTooLong,
               BLOCK_DURATION_MS, (uint64_t) gDiagnostics.numAudioDatagramsSendTookTooLong * 100 /
                                             gDiagnostics.numAudioDatagrams);
    }
    printf("All stop.\n");
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: OPERATING MODES AND SLEEP
 * -------------------------------------------------------------- */

/* The dynamic behaviour of the Internet Of Chuffs client is as
 * follows:
 *
 * - there is a wakeUpTick, a wakeUpTickCounter, a
 *   wakeUpTickCounterModulo and a sleepLevel,
 *
 * - wakeUpTickCounter is incremented every wakeUpTick, modulo
 *   wakeUpTickCounterModulo,
 *
 * - the possible sleepLevels are:
 *
 *   REGISTERED:         peripherals are up (though they may be quiescent),
 *                       GNSS may be on, modem is on, MCU is clocked
 *                       normally, the IOC Client is registered with the
 *                       LWM2M Mbed Cloud Server; successful init() brings
 *                       the IOC Client into this sleepLevel,
 *   REGISTERED_SLEEP:   as "REGISTERED" except MCU is in clock-stop so no
 *                       timers are running, the MCU will wake up from RTC
 *                       interrupt,
 *   DEREGISTERED_SLEEP: peripherals are in lowest power state, GNSS and
 *                       modem are off, MCU is in deep sleep (so RAM is off
 *                       as well) but will awake from RTC interrupt, the IOC
 *                       Client is deregistered from the LWM2M Mbed Cloud
 *                       Server; deinit() puts the IOC Client into this
 *                       sleepLevel and init() must be run on return,
 *   OFF:                as "DEREGISTERED_SLEEP" except that a power-cycle
 *                       is required to wake the IOC Client up; note that,
 *                       since the watchdog is active, OFF isn't truly
 *                       off, the MCU will wake up at the watchdog interval
 *                       and put itself immediately back to sleep again.
 *
 * - the IOC client wakes up, either from sleepLevel or from
 *   power-cycle, performs some operation and, when the operation
 *   finishes, the IOC client is returned to sleepLevel,
 *
 * - there are two modes of behaviour, Initialisation and Ready:
 *
 *   - in Initialisation mode the IOC client is trying to register
 *     with the LWM2M server in the Mbed Cloud,
 *   - in Ready mode the IOC client reports-in to the Mbed Cloud
 *     on a regular basis and awaits instructions from it.
 *
 * - In summary, the life-cycle is as follows, implemented using
 *   the mechanisms above:
 *
 *   1. enter Initialisation mode for a time,
 *   2. if Initialisation mode is successfully completed, go to
 *      Ready mode for a period of time,
 *   3. if an instruction is received in Ready mode, reset
 *      the Ready mode timer,
 *   4. if Initialisation mode is not completed in time or the Ready
 *      mode timer expires, return to OFF,
 *   5. the timers will be set such that, if there is no external
 *      power, the IOC client stays awake for about 60 minutes and
 *      no longer.
 *
 * In detail, Initialisation mode dynamic behaviour is as follows:
 *
 * - the variables are set up on entry:
 *   - wakeUpTick period:       10 minutes [initWakeUpTickCounterPeriod],
 *   - sleepLevel:              DEREGISTERED_SLEEP,
 *   - wakeUpTickCounterModulo: 3 [initWakeUpTickCounterModulo],
 * - on wake-up, run init(),
 * - if init() is completed, move immediately to Ready mode,
 * - at each tick:
 *   - if wakeUpTickCounterModulo has been reached AND
 *     there is NO external power, go to sleepLevel OFF,
 *   - otherwise, run init().
 *
 * In detail, Ready mode dynamic behaviour is as follows:
 *
 * - the variables are set up on entry:
 *   - wakeUpTick period:       1 minute [readyWakeUpTickCounterPeriod1],
 *   - sleepLevel:              REGISTERED_SLEEP,
 *   - wakeUpTickCounterModulo: 60 [readyWakeUpTickCounterModulo],
 * - at each tick, report-in to the Mbed Cloud LWM2M server,
 * - if an instruction is received from the Mbed Cloud LWM2M
 *   server then:
 *   - act upon it,
 *   - if there is external power, reset the wakeUpTickCounter,
 * - if no instruction is received, or the instruction
 *   has been completed, go to sleepLevel until the next
 *   tick,
 * - if wakeUpTickCounterModulo is reached:
 *   - if audio streaming is in progress, keep wakeUpTick
 *     at 1 minute, otherwise:
 *     - if there is external power, set wakeUpTick to 10
 *       minutes [readyWakeUpTickCounterPeriod2],
 *     - if there is no external power, go to sleepLevel OFF.
 */

// Go to MCU sleep but remain registered, for the given time.
static void setSleepLevelRegisteredSleep(time_t sleepDurationSeconds)
{
    time_t sleepTimeLeft;
    gTimeEnterSleep = time(NULL);
    gTimeLeaveSleep = gTimeEnterSleep + sleepDurationSeconds;

    LOG(EVENT_SLEEP_LEVEL_REGISTERED, sleepDurationSeconds);
    printf("Going to REGISTERED_SLEEP for %d second(s), until %s",
           (int) (sleepDurationSeconds), ctime(&gTimeLeaveSleep));

    // Need to wake-up at the watchdog interval to feed it
    while ((sleepTimeLeft = (gTimeLeaveSleep - time(NULL))) > 0) {
        if (sleepTimeLeft > MAX_SLEEP_SECONDS) {
            sleepTimeLeft = MAX_SLEEP_SECONDS;
        }

        HAL_IWDG_Refresh(&gWdt);
        LOG(EVENT_ENTER_STOP, sleepTimeLeft);
        gLowPower.enterStop(sleepTimeLeft);
        deinitLog();  // So that we have a complete record up to this point
    }

    printf("Awake from REGISTERED_SLEEP after %d second(s).\n", (int) (time(NULL) - gTimeEnterSleep));
}

// Go to deregistered sleep for the given time.
static void setSleepLevelDeregisteredSleep(time_t sleepDurationSeconds)
{
    gTimeEnterSleep = time(NULL);
    gTimeLeaveSleep = gTimeEnterSleep + sleepDurationSeconds;

    LOG(EVENT_SLEEP_LEVEL_DEREGISTERED, sleepDurationSeconds);
    printf("Going to DEREGISTERED_SLEEP for %d second(s), until %s",
           (int) (sleepDurationSeconds), ctime(&gTimeLeaveSleep));

    // Need to wake-up at the watchdog interval to feed it
    if (sleepDurationSeconds > MAX_SLEEP_SECONDS) {
        sleepDurationSeconds = MAX_SLEEP_SECONDS;
    }
    memcpy(gHistoryMarker, HISTORY_MARKER_STANDBY, sizeof (gHistoryMarker));
    HAL_IWDG_Refresh(&gWdt);
    LOG(EVENT_ENTER_STANDBY, sleepDurationSeconds * 1000);
    deinitLog();  // So that we have a complete record
    gLowPower.enterStandby(sleepDurationSeconds * 1000);
    // The wake-up process is handled on entry to main()
}

// Go to OFF sleep state, from which only a power
// cycle will awaken us (or the watchdog, but we'll
// immediately go back to sleep again)
static void setSleepLevelOff()
{
    LOG(EVENT_SLEEP_LEVEL_OFF, 0);
    memcpy(gHistoryMarker, HISTORY_MARKER_OFF, sizeof (gHistoryMarker));
    HAL_IWDG_Refresh(&gWdt);
    LOG(EVENT_ENTER_STANDBY, MAX_SLEEP_SECONDS * 1000);
    deinitLog();  // So that we have a complete record
    gLowPower.enterStandby(MAX_SLEEP_SECONDS * 1000);
}

// The Initialisation mode wake-up tick handler.
static void initialisationModeWakeUpTickHandler()
{
    gWakeUpTickCounter++;
    LOG(EVENT_INITIALISATION_MODE_WAKE_UP_TICK, gWakeUpTickCounter);
    if (gWakeUpTickCounter >= gConfigLocal.initWakeUpTickCounterModulo) {
        gWakeUpTickCounter = 0;
        if (gpBatteryCharger && !gpBatteryCharger->isExternalPowerPresent()) {
            // If there is no external power and we've got here, it's been
            // far too long so just give up
            setSleepLevelOff();
        } else {
            // Otherwise, enter standby with a short
            // timer, which will reset us to start trying
            // again
            LOG(EVENT_ENTER_STANDBY, 100);
            deinitLog();  // So that we have a complete record
            gLowPower.enterStandby(100);
        }
    }
}

// Perform Initialisation mode.
static void initialisationMode()
{
    bool success = false;
    time_t timeStarted;

    // Add the Initialisation mode wake-up handler
    LOG(EVENT_INITIALISATION_MODE_START, 0);
    gWakeUpTickHandler = gEventQueue.call_every(gConfigLocal.initWakeUpTickCounterPeriod * 1000, initialisationModeWakeUpTickHandler);

    // Initialise everything.  There are three possible outcomes:
    //
    // - initFileSystem() && init() succeeds, in which case this function returns,
    // - initFileSystem() && init() fails, in which case we go to deregistered sleep for
    //   the remainder of the wake up tick period,
    // - initFileSystem() && init() takes longer than the wake up tick period, in which case
    //   the initWakeUpTickHandler will kick in and restart things
    //   in the appropriate way.
    while (!success) {
        timeStarted = time(NULL);
        success = initFileSystem() && init();
        if (!success) {
            setSleepLevelDeregisteredSleep(gConfigLocal.initWakeUpTickCounterPeriod - (time(NULL) - timeStarted));
        }
    }

    // The cloud client registration event is asynchronous
    // to the above, so need to wait for it now
    while (!gpCloudClientDm->isConnected()) {
        HAL_IWDG_Refresh(&gWdt);
        wait_ms(CLOUD_CLIENT_REGISTRATION_CHECK_INTERVAL_MS);
    }

    // Having done all of that, it's now safe to begin
    // uploading any log files that might be lying around
    // from previous runs to a logging server
    if (gLoggingLocal.loggingUploadEnabled) {
        beginLogFileUpload(&gFs, gpCellular, gLoggingLocal.loggingServerUrl.c_str());
    }

    // Remove the Initialisation mode wake-up handler
    gEventQueue.cancel(gWakeUpTickHandler);
    gWakeUpTickHandler = -1;
}

// Deal with the fact that an instruction
// has been received from the server.
static void readyModeInstructionReceived()
{
    LOG(EVENT_READY_MODE_INSTRUCTION_RECEIVED, 0);
    if (gpBatteryCharger && gpBatteryCharger->isExternalPowerPresent()) {
        // If there is external power, reset the tick counter
        // so that we stay awake
        LOG(EVENT_READY_MODE_WAKE_UP_TICK_COUNTER_RESET, 0);
        gWakeUpTickCounter = 0;
    }
}

// The Ready mode wake-up tick handler.
static void readyModeWakeUpTickHandler()
{
    gWakeUpTickCounter++;
    LOG(EVENT_READY_MODE_WAKE_UP_TICK, gWakeUpTickCounter);
    if (gWakeUpTickCounter >= gConfigLocal.readyWakeUpTickCounterModulo) {
        gWakeUpTickCounter = 0;
        if (gAudioLocalActive.streamingEnabled) {
            // If we're streaming, make sure we stay awake
            gEventQueue.cancel(gWakeUpTickHandler);
            gWakeUpTickHandler = gEventQueue.call_every(gConfigLocal.readyWakeUpTickCounterPeriod1 * 1000, readyModeWakeUpTickHandler);
        } else {
            if (gpBatteryCharger && !gpBatteryCharger->isExternalPowerPresent()) {
                // If there is no external power we've been awake for long enough
                setSleepLevelOff();
            } else {
                // Otherwise, just switch to the long repeat period as obviously
                // nothing much is happening
                gEventQueue.cancel(gWakeUpTickHandler);
                gWakeUpTickHandler = gEventQueue.call_every(gConfigLocal.readyWakeUpTickCounterPeriod2 * 1000, readyModeWakeUpTickHandler);
            }
        }
    }

    // Update the objects that the server can observe
    objectUpdate();

    // TODO call Mbed Cloud Client keep alive once we've
    // understood how to drive Mbed Cloud Client in that way
}

// Perform Ready mode.
static void readyMode()
{
    // Switch to the Ready mode wake-up handler and zero the tick count
    LOG(EVENT_READY_MODE_START, 0);
    gWakeUpTickCounter = 0;
    gWakeUpTickHandler = gEventQueue.call_every(gConfigLocal.readyWakeUpTickCounterPeriod1 * 1000, readyModeWakeUpTickHandler);

    for (int x = 0; !gUserButtonPressed; x++) {
        HAL_IWDG_Refresh(&gWdt);
        // TODO should go to sleep here but we can't until we find out
        // how to drive Mbed Cloud Client in that way
        wait_ms(BUTTON_CHECK_INTERVAL_MS);
    }

    // Cancel the Ready mode wake-up handler
    gEventQueue.cancel(gWakeUpTickHandler);
}

/* ----------------------------------------------------------------
 * MAIN
 * -------------------------------------------------------------- */

int main()
{
    time_t sleepTimeLeft;

    flash();

    gResetReason = getResetReason();

    // If this is a power on reset, do a system
    // reset to get us out of our debug-mode
    // entanglement with the debug chip on the
    // mbed board and allow power saving
    if (gResetReason == RESET_REASON_POWER_ON) {
        NVIC_SystemReset();
    }

    flash();
    printf("Starting logging...\n");
    initLog(gLogBuffer);

    LOG(EVENT_SYSTEM_START, gResetReason);
    LOG(EVENT_BUILD_TIME_UNIX_FORMAT, __COMPILE_TIME_UNIX__);

    // Bring up the battery charger and battery gauge
    initPower();

    // If we should be off, and there is no external
    // power to keep us going, go straight back to sleep
    if ((memcmp (gHistoryMarker, HISTORY_MARKER_OFF, sizeof (HISTORY_MARKER_OFF)) == 0) &&
        gpBatteryCharger && !gpBatteryCharger->isExternalPowerPresent()) {
        HAL_IWDG_Refresh(&gWdt);
        LOG(EVENT_ENTER_STANDBY, MAX_SLEEP_SECONDS * 1000);
        deinitLog();  // So that we have a complete record
        gLowPower.enterStandby(MAX_SLEEP_SECONDS * 1000);
    }

    // If we've been in standby and the RTC is running,
    // check if it's actually time to wake up yet
    if ((memcmp (gHistoryMarker, HISTORY_MARKER_STANDBY, sizeof (HISTORY_MARKER_STANDBY)) == 0) &&
        (time(NULL) != 0)) {
        sleepTimeLeft = gTimeLeaveSleep - time(NULL);
        if (sleepTimeLeft > 0) {
            // Not time to wake up yet, feed the watchdog and go
            // back to sleep
            HAL_IWDG_Refresh(&gWdt);
            if (sleepTimeLeft > MAX_SLEEP_SECONDS) {
                sleepTimeLeft = MAX_SLEEP_SECONDS;
            }
            LOG(EVENT_ENTER_STANDBY, sleepTimeLeft);
            deinitLog();  // So that we have a complete record
            gLowPower.enterStandby(sleepTimeLeft);
        }
        printf("Awake from DEREGISTERED_SLEEP after %d second(s).\n",
               (int) (time(NULL) - gTimeEnterSleep));
    }

    // If we were not running normally, this must have been a power-on reset,
    // so zero the wake-up tick counter and set up configuration defaults
    // Note: can't check for the gResetReason being RESET_REASON_POWER_ON because
    // that's not the case under the debugger.
    if ((memcmp (gHistoryMarker, HISTORY_MARKER_NORMAL, sizeof (HISTORY_MARKER_NORMAL)) != 0)) {
        gWakeUpTickCounter = 0;
        gPowerOnNotOff = DEFAULT_POWER_ON_NOT_OFF;
        gConfigLocal.initWakeUpTickCounterPeriod = CONFIG_DEFAULT_INIT_WAKE_UP_TICK_COUNTER_PERIOD;
        gConfigLocal.initWakeUpTickCounterModulo = CONFIG_DEFAULT_INIT_WAKE_UP_TICK_COUNTER_MODULO;
        gConfigLocal.readyWakeUpTickCounterPeriod1 = CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_PERIOD_1;
        gConfigLocal.readyWakeUpTickCounterPeriod2 = CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_PERIOD_2;
        gConfigLocal.readyWakeUpTickCounterModulo = CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_MODULO;
        gConfigLocal.gnssEnable = CONFIG_DEFAULT_GNSS_ENABLE;
    }

#if defined(MBED_CONF_MBED_TRACE_ENABLE) && MBED_CONF_MBED_TRACE_ENABLE
    // NOTE: the mutex causes output to stop under heavy load, hence
    // it is commented out here.
    // Create mutex for tracing to avoid broken lines in logs
    //MBED_ASSERT(mbed_trace_helper_create_mutex());

    // Initialize mbed trace
    mbed_trace_init();
    //mbed_trace_mutex_wait_function_set(mbed_trace_helper_mutex_wait);
    //mbed_trace_mutex_release_function_set(mbed_trace_helper_mutex_release);
#endif

    printf("\n********** START **********\n");

    // We are running, so set the history marker to normal
    memcpy(gHistoryMarker, HISTORY_MARKER_NORMAL, sizeof (gHistoryMarker));

    heapStats();

    // Start the event queue in the event thread
    gpEventThread = new Thread();
    gpEventThread->start(callback(&gEventQueue, &EventQueue::dispatch_forever));

    // Run through the Initialisation and Ready
    // modes.  Exit is via various forms of sleep
    // or reset, or most naturally via the user
    // button switching everything off, in which
    // case readyMode() will return.
    initialisationMode();
    readyMode();

    // Shut everything down
    deinit();
    deinitPower();
    ledOff();

    // Stop the event queue
    if (gpEventThread) {
        gpEventThread->terminate();
        gpEventThread->join();
        delete gpEventThread;
        gpEventThread = NULL;
    }

    heapStats();

    HAL_IWDG_Refresh(&gWdt);
    flash();
    printf("Printing the log...\n");
    // Run a ticker to feed the watchdog while we print out the log
    gSecondTicker.attach_us(callback(&watchdogFeedCallback), 1000000);
    printLog();
    gSecondTicker.detach();
    printf("Stopping logging...\n");
    deinitLog();
    deinitFileSystem();

    LOG(EVENT_SYSTEM_STOP, 0);
    printf("********** STOP **********\n");

    setSleepLevelOff();
}

// End of file
