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
#include "urtp.h"
#include "ioc_log.h"
#ifdef MBED_HEAP_STATS_ENABLED
#include "mbed_stats.h"
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// Get debug prints from most things
#define DEBUG_ON true

// The baud rate to use with the modem
#define MODEM_BAUD_RATE 230400

// Whether to reset Mbed Cloud Client storage or not
// If this is true then you really need to have
// MBED_CONF_APP_DEVELOPER_MODE defined and then a
// new object ID will be created when the Mbed Cloud
// Client registers with the server
#define CLOUD_CLIENT_RESET_STORAGE false

// The default audio data
#define AUDIO_DEFAULT_STREAMING_ENABLED  false
#define AUDIO_DEFAULT_DURATION           -1.0
#define AUDIO_DEFAULT_FIXED_GAIN         -1.0
#define AUDIO_DEFAULT_COMMUNICATION_MODE 1
#define AUDIO_DEFAULT_SERVER_URL         "ciot.it-sgn.u-blox.com:8080"

// The default config data
#define CONFIG_DEFAULT_INIT_WAKEUP_TICK_PERIOD    3600.0
#define CONFIG_DEFAULT_INIT_WAKEUP_COUNT          2
#define CONFIG_DEFAULT_NORMAL_WAKEUP_TICK_PERIOD  60.0
#define CONFIG_DEFAULT_NORMAL_WAKEUP_COUNT        60
#define CONFIG_DEFAULT_BATTERY_WAKEUP_TICK_PERIOD 600.0
#define CONFIG_DEFAULT_GNSS_ENABLE                true

// The static temperature object parameters
#define TEMPERATURE_MIN_RANGE -10
#define TEMPERATURE_MAX_RANGE 120
#define TEMPERATURE_UNITS "cel"

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// Implementation of MbedCloudClientCallback
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
 * VARIABLES
 * -------------------------------------------------------------- */

// Datagram storage for URTP
__attribute__ ((section ("CCMRAM")))
static char datagramStorage[URTP_DATAGRAM_STORE_SIZE];

// The SD card, instantiated in the Mbed Cloud Client in
// in pal_plat_fileSystem.cpp
extern SDBlockDevice sd;

// The network interface
static UbloxPPPCellularInterface *gpCellular = NULL;

// LWM2M objects for the control plane
static CloudClientDm *gpCloudClientDm = NULL;
static IocM2mPowerControl *gpPowerControlObject = NULL;
static IocM2mLocation *gpLocationObject = NULL;
static IocM2mTemperature *gpTemperatureObject = NULL;
static IocM2mConfig *gpConfigObject = NULL;
static IocM2mAudio *gpAudioObject = NULL;
static IocM2mDiagnostics *gpDiagnosticsObject = NULL;
static UpdateCallback *gpCloudClientGlobalUpdateCallback = NULL;

// Data storage
static bool powerOnNotOff = false;
static IocM2mLocation::Location locationData;
static IocM2mTemperature::Temperature temperatureData;
static IocM2mConfig::Config configData;
static IocM2mAudio::Audio audioData;
static IocM2mDiagnostics::Diagnostics diagnosticsData;

// The user button
static InterruptIn *gpUserButton = NULL;

// LEDs for user feedback and diagnostics
static DigitalOut ledRed(LED1, 1);
static DigitalOut ledGreen(LED2, 1);
static DigitalOut ledBlue(LED3, 1);

static volatile bool gUserButtonPressed = false;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: DIAGNOSTICS
 * -------------------------------------------------------------- */

// Indicate good (green)
static void good() {
    ledGreen = 0;
    ledBlue = 1;
    ledRed = 1;
}

// Indicate bad (red)
static void bad() {
    ledRed = 0;
    ledGreen = 1;
    ledBlue = 1;
}

// Toggle green
static void toggleGreen() {
    ledGreen = !ledGreen;
}

// All off
static void ledOff() {
    ledBlue = 1;
    ledRed = 1;
    ledGreen = 1;
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
 * STATIC FUNCTIONS: LWM2M
 * -------------------------------------------------------------- */

// Callback when mbed Cloud Client registers with the LWM2M server.
static void cloudClientRegisteredCallback() {
    ledBlue = 0;
}

// Callback when mbed Cloud Client deregisters from the LWM2M server.
static void cloudClientDeregisteredCallback() {
    ledBlue = 1;
}

// Callback that sets the power switch for the IocM2mPowerControl
// object.
static void setPowerControl(bool value)
{
    printf("Power control set to %d.\n", value);

    powerOnNotOff = value;
}

// Callback that retrieves location data for the IocM2mLocation
// object.
static bool getLocationData(IocM2mLocation::Location *data)
{
    *data = locationData;

    return true;
}

// Callback that retrieves temperature data for the IocM2mTemperature
// object.
static bool getTemperatureData(IocM2mTemperature::Temperature *data)
{
    *data = temperatureData;

    return true;
}

// Callback that executes a reset of the min/max temperature range
// for the IocM2mTemperature object.
static void executeResetTemperatureMinMax()
{
    printf("Received min/max temperature reset.\n");

    temperatureData.maxTemperature = temperatureData.temperature;
    temperatureData.minTemperature = temperatureData.temperature;
}

// Callback that sets the configuration data for the IocM2mConfig
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

    configData = *data;
}

// Callback that sets the configuration data for the IocM2mAudio
// object.
static void setAudioData(IocM2mAudio::Audio *data)
{
    printf("Received new audio parameters:\n");
    printf("  streamingEnabled %d.\n", data->streamingEnabled);
    printf("  duration %f.\n", data->duration);
    printf("  fixedGain %f.\n", data->fixedGain);
    printf("  audioCommunicationsMode %lld.\n", data->audioCommunicationsMode);
    printf("  audioServerUrl \"%s\".\n", data->audioServerUrl.c_str());

    audioData = *data;
}

// Callback that gets diagnostic data for the IocM2mDiagnostics object.
static bool getDiagnosticsData(IocM2mDiagnostics::Diagnostics *data)
{
    *data = diagnosticsData;

    return true;
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
    printf("Setting up data storage...\n");
    memset(&locationData, 0, sizeof (locationData));
    memset(&temperatureData, 0, sizeof (temperatureData));

    configData.initWakeUpTickPeriod = CONFIG_DEFAULT_INIT_WAKEUP_TICK_PERIOD;
    configData.initWakeUpCount = CONFIG_DEFAULT_INIT_WAKEUP_COUNT;
    configData.normalWakeUpTickPeriod = CONFIG_DEFAULT_NORMAL_WAKEUP_TICK_PERIOD;
    configData.normalWakeUpCount = CONFIG_DEFAULT_NORMAL_WAKEUP_COUNT;
    configData.batteryWakeUpTickPeriod = CONFIG_DEFAULT_BATTERY_WAKEUP_TICK_PERIOD;
    configData.gnssEnable = CONFIG_DEFAULT_GNSS_ENABLE;

    audioData.streamingEnabled = AUDIO_DEFAULT_STREAMING_ENABLED;
    audioData.duration = AUDIO_DEFAULT_DURATION;
    audioData.fixedGain = AUDIO_DEFAULT_FIXED_GAIN;
    audioData.audioCommunicationsMode = AUDIO_DEFAULT_COMMUNICATION_MODE;
    audioData.audioServerUrl = AUDIO_DEFAULT_SERVER_URL;

    memset(&diagnosticsData, 0, sizeof (diagnosticsData));

    printf("Creating user button...\n");
    gpUserButton = new InterruptIn(SW0);
    gpUserButton->rise(&buttonCallback);

    printf("Starting SD card...\n");
    int sdError = sd.init();
    if(sdError != 0) {
        bad();
        printf("Error initialising SD card (%d).\n", sdError);
        return false;
    }
    printf("SD card started.\n");

    printf("Initialising file storage...\n");
    fcc_status_e status = fcc_init();
    if(status != FCC_STATUS_SUCCESS) {
        bad();
        printf("Error initialising file storage (%d).\n", status);
        return false;
    }
    printf("File storage initialised.\n");

    // Use this function when you want to clear storage from all
    // the factory-tool generated data and user data.
    // After this operation device must be injected again by using
    // factory tool or developer certificate.
    if (resetStorage) {
        printf("Reseting storage to an empty state...\n");
        fcc_status_e deleteStatus = fcc_storage_delete();
        if (deleteStatus != FCC_STATUS_SUCCESS) {
            printf("Failed to delete storage - %d\n", deleteStatus);
            return false;
        }
    }

#ifdef MBED_CONF_APP_DEVELOPER_MODE
    printf("Starting developer flow...\n");
    status = fcc_developer_flow();
    if (status == FCC_STATUS_KCM_FILE_EXIST_ERROR) {
        printf("Developer credentials already exist.\n");
    } else if (status != FCC_STATUS_SUCCESS) {
        bad();
        printf("Failed to load developer credentials.\n");
        return false;
    }    
#endif

    printf("Checking configuration...\n");
    status = fcc_verify_device_configured_4mbed_cloud();
    if (status != FCC_STATUS_SUCCESS) {
        bad();
        printf("Device not configured for mbed Cloud.\n");
#ifdef MBED_CONF_APP_DEVELOPER_MODE
        printf("  You might want to clear mbed Cloud Client file storage and try again.\n");
#endif
        return false;
    }

    // Note sure if this is required or not
    srand(time(NULL));

    printf("Initialising cellular...\n");
    UbloxPPPCellularInterface *gpCellular = new UbloxPPPCellularInterface(MDMTXD,
                                                                          MDMRXD,
                                                                          MODEM_BAUD_RATE,
                                                                          false);
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
    gpCloudClientDm = new CloudClientDm(DEBUG_ON, &cloudClientRegisteredCallback,
                                                  &cloudClientDeregisteredCallback);
    printf("Configuring Device object...\n");
    // TODO add resources to device object

    printf("Creating all the other objects...\n");
    gpPowerControlObject = new IocM2mPowerControl(setPowerControl, true, DEBUG_ON);
    gpCloudClientDm->addObject(gpPowerControlObject->getObject());
    gpLocationObject = new IocM2mLocation(getLocationData, DEBUG_ON);
    gpCloudClientDm->addObject(gpLocationObject->getObject());
    gpTemperatureObject = new IocM2mTemperature(getTemperatureData,
                                                executeResetTemperatureMinMax,
                                                TEMPERATURE_MIN_RANGE,
                                                TEMPERATURE_MAX_RANGE,
                                                TEMPERATURE_UNITS, DEBUG_ON);
    gpCloudClientDm->addObject(gpTemperatureObject->getObject());
    gpConfigObject = new IocM2mConfig(setConfigData, &configData, DEBUG_ON);
    gpCloudClientDm->addObject(gpConfigObject->getObject());
    gpAudioObject = new IocM2mAudio(setAudioData, &audioData, DEBUG_ON);
    gpCloudClientDm->addObject(gpAudioObject->getObject());
    gpDiagnosticsObject = new IocM2mDiagnostics(getDiagnosticsData, DEBUG_ON);
    gpCloudClientDm->addObject(gpDiagnosticsObject->getObject());

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
    if (gpCloudClientDm) {
        printf("Stopping cloud client...\n");
        gpCloudClientDm->stop();
    }
    printf("Deleting objects...\n");
    if (gpPowerControlObject) {
        delete gpPowerControlObject;
        gpPowerControlObject = NULL;
    }
    if (gpLocationObject) {
        delete gpLocationObject;
        gpLocationObject = NULL;
    }
    if (gpTemperatureObject) {
        delete gpTemperatureObject;
        gpTemperatureObject = NULL;
    }
    if (gpConfigObject) {
        delete gpConfigObject;
        gpConfigObject = NULL;
    }
    if (gpAudioObject) {
        delete gpAudioObject;
        gpAudioObject = NULL;
    }
    if (gpDiagnosticsObject) {
        delete gpDiagnosticsObject;
        gpDiagnosticsObject = NULL;
    }

    if (gpCloudClientDm) {
        printf("Deleting cloud client...\n");
        delete gpCloudClientDm;
        gpCloudClientDm = NULL;
    }

    if (gpCellular) {
        printf("Disconnecting network...\n");
        gpCellular->disconnect();
        printf("Stopping modem...\n");
        gpCellular->deinit();
        delete gpCellular;
        gpCellular = NULL;
    }

    printf("Closing SD card...\n");
    sd.deinit();

    printf("Removing user button...\n");
    if (gpUserButton) {
        delete gpUserButton;
        gpUserButton = NULL;
    }

    printf("All stop.\n");
    ledOff();
}

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

int main() {
    good();
    printf("\nMaking sure the compiler links datagramStorage (0x%08x).\n", (int) datagramStorage);

    heapStats();
    if (init(CLOUD_CLIENT_RESET_STORAGE)) {

        printf("Press the user button to exit.\n");
        for (int x = 0; !gUserButtonPressed; x++) {
            wait_ms(1000);
            toggleGreen();
        }
        deinit();
    }
    heapStats();
}

// End of file
