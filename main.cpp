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

#ifndef MBED_CONF_APP_CLOUD_CLIENT_RESET_STORAGE
// Whether to reset Mbed Cloud Client storage or not.
// If this is true then you really need to have
// MBED_CONF_APP_DEVELOPER_MODE defined and then a
// new object ID will be created when the Mbed Cloud
// Client registers with the server.
#  define MBED_CONF_APP_CLOUD_CLIENT_RESET_STORAGE false
#endif

// The default audio data.
#define AUDIO_DEFAULT_STREAMING_ENABLED  false
#define AUDIO_DEFAULT_DURATION           -1.0
#define AUDIO_DEFAULT_FIXED_GAIN         -1.0
#define AUDIO_DEFAULT_COMMUNICATION_MODE 1
#define AUDIO_DEFAULT_SERVER_URL         "ciot.it-sgn.u-blox.com:8080"

// The default config data.
#define CONFIG_DEFAULT_INIT_WAKEUP_TICK_PERIOD    3600.0
#define CONFIG_DEFAULT_INIT_WAKEUP_COUNT          2
#define CONFIG_DEFAULT_NORMAL_WAKEUP_TICK_PERIOD  60.0
#define CONFIG_DEFAULT_NORMAL_WAKEUP_COUNT        60
#define CONFIG_DEFAULT_BATTERY_WAKEUP_TICK_PERIOD 600.0
#define CONFIG_DEFAULT_GNSS_ENABLE                true

// The static temperature object parameters.
#define TEMPERATURE_MIN_RANGE -10
#define TEMPERATURE_MAX_RANGE 120
#define TEMPERATURE_UNITS "cel"

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// Identifier for each LWM2M object.
//
// To add a new object:
// - add an entry for it here,
// - add it to IocM2mObjectPointerUnion,
// - add it to addObject() and deleteObject().
//
// Note: this enum is used to index into the gObjectList.
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
} IocM2mObjectPointerUnion;

// Structure defining the things we need
// to access in an LWM2M object
typedef struct {
    IocM2mObjectPointerUnion object;
    Callback<void(void)> updateObservableResources;
} IocM2mObject;

// Implementation of MbedCloudClientCallback.
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

// Datagram storage for URTP.
__attribute__ ((section ("CCMRAM")))
static char datagramStorage[URTP_DATAGRAM_STORE_SIZE];

// The SD card, instantiated in the Mbed Cloud Client in
// in pal_plat_fileSystem.cpp.
extern SDBlockDevice sd;

// The event loop, queue and ticker.
Thread gEventThread;
EventQueue gEventQueue (32 * EVENTS_EVENT_SIZE);

// The network interface.
static UbloxPPPCellularInterface *gpCellular = NULL;

// The Mbed Cloud Client stuff.
static CloudClientDm *gpCloudClientDm = NULL;
static UpdateCallback *gpCloudClientGlobalUpdateCallback = NULL;

// LWM2M objects for the control plane.
// NOTE: this array is accessed using the IocM2mObjectId enum.
static IocM2mObject gObjectList[MAX_NUM_IOC_M2M_OBJECTS] = {NULL};

// Event ID for the LWM2M object update event.
static int gObjectUpdateEvent = -1;

// Data storage.
static bool powerOnNotOff = false;
static IocM2mLocation::Location locationData;
static IocM2mTemperature::Temperature temperatureData;
static IocM2mConfig::Config configData;
static IocM2mAudio::Audio audioData;
static IocM2mDiagnostics::Diagnostics diagnosticsData;

// The user button.
static InterruptIn *gpUserButton = NULL;

// LEDs for user feedback and diagnostics.
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

// Add a LWM2M object to the relevant lists
static void addObject(IocM2mObjectId id, void * object)
{
    MBED_ASSERT (gpCloudClientDm);

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
// Note: this does not remove the object from the Mbed Cloud Client,
// the Mbed Cloud Client clears itself up at the end.
void deleteObject(IocM2mObjectId id)
{
    if ((void *) &(gObjectList[id].object) != NULL) {
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

        if ((void *) &(gObjectList[id].object) == NULL) {
            gObjectList[id].updateObservableResources = NULL;
        }
    }
}

// Callback when mbed Cloud Client registers with the LWM2M server.
static void cloudClientRegisteredCallback() {
    printf("Press the user button to exit.\n");
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

// Callback to update the observable values of all the LWM2M objects
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

    // Note sure if this is required or not
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
    printf("Configuring LWM2M Device object...\n");
    // TODO add resources to the Device object

    printf("Creating all the other LWM2M objects...\n");
    addObject(IOC_M2M_POWER_CONTROL, new IocM2mPowerControl(setPowerControl, true,
                                                            MBED_CONF_APP_OBJECT_DEBUG_ON));
    addObject(IOC_M2M_LOCATION, new IocM2mLocation(getLocationData,
                                                   MBED_CONF_APP_OBJECT_DEBUG_ON));
    addObject(IOC_M2M_TEMPERATURE, new IocM2mTemperature(getTemperatureData,
                                                         executeResetTemperatureMinMax,
                                                         TEMPERATURE_MIN_RANGE,
                                                         TEMPERATURE_MAX_RANGE,
                                                         TEMPERATURE_UNITS,
                                                         MBED_CONF_APP_OBJECT_DEBUG_ON));
    addObject(IOC_M2M_CONFIG, new IocM2mConfig(setConfigData, &configData,
                                               MBED_CONF_APP_OBJECT_DEBUG_ON));
    addObject(IOC_M2M_AUDIO, new IocM2mAudio(setAudioData, &audioData,
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

    printf("All stop.\n");
}

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

int main() {
    printf("\nMaking sure the compiler links datagramStorage (0x%08x).\n", (int) datagramStorage);

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
}

// End of file
