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

#define DEBUG_ON true

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Datagram storage for URTP
__attribute__ ((section ("CCMRAM")))
static char datagramStorage[URTP_DATAGRAM_STORE_SIZE];

extern SDBlockDevice sd;     // in pal_plat_fileSystem.cpp

IocM2mConfig::Config configData = {3600.0, // initWakeUpTickPeriod
                                    2, // initWakeUpCount
                                    60.0, // normalWakeUpTickPeriod
                                    60, // normalWakeUpCount
                                    600.0, // batteryWakeUpTickPeriod
                                    true}; // gnssEnable

IocM2mAudio::Audio audioData = {false, // streamingEnabled
                                 -1.0, // duration
                                 -1.0, // fixedGain
                                 1, // audioCommunicationsMode
                                 "ciot.it-sgn.u-blox.com:8080"}; //audioServerUrl

IocM2mDiagnostics::Diagnostics diagnosticsData = {-1};

// LEDs
static DigitalOut ledRed(LED1, 1);
static DigitalOut ledGreen(LED2, 1);
static DigitalOut ledBlue(LED3, 1);

volatile bool buttonPressed = false;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
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

static void cloudClientRegisteredCallback() {
    ledBlue = 0;
}

static void cloudClientDeregisteredCallback() {
    ledBlue = 1;
}

static void heapStats()
{
#ifdef MBED_HEAP_STATS_ENABLED
    mbed_stats_heap_t stats;
    mbed_stats_heap_get(&stats);

    printf("HEAP size:     %" PRIu32 ".\n", stats.current_size);
    printf("HEAP maxsize:  %" PRIu32 ".\n", stats.max_size);
#endif
}

static void setThing(bool value)
{
    printf("Thing set to %d.\n", value);
}

static bool getLocationData(IocM2mLocation::Location *data)
{
    data->latitudeDegrees = 1.00;
    data->longitudeDegrees = 2.00;
    data->radiusMetres = 5;
    data->altitudeMetres = 3;
    data->speedMPS = 0;
    data->timestampUnix = 0x7F123456;

    return true;
}

static bool getTemperatureData(IocM2mTemperature::Temperature *data)
{
    data->temperature = 32;
    data->minTemperature = 5;
    data->maxTemperature = 40;

    return true;
}

static void resetMinMax()
{
    printf("Received min/max temperature reset.\n");
}

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

static bool getDiagnosticsData(IocM2mDiagnostics::Diagnostics *data)
{
    data->upTime = 100;
    data->worstCaseSendDuration = 0.999;
    data->averageSendDuration = 0.015;
    data->minNumDatagramsFree = 50;
    data->numSendFailures = 2;
    data->percentageSendsTooLong = 25;

    return true;
}

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

static UpdateCallback *cloudClientGlobalUpdateCallback = new UpdateCallback();

// Something to attach to the button
static void buttonCallback()
{
    buttonPressed = true;
}

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

int main() {
    InterruptIn userButton(SW0);
    int x;

    good();
    printf("\nMaking sure the compiler links datagramStorage (0x%08x).\n", (int) datagramStorage);

    // Attach a function to the user button
    userButton.rise(&buttonCallback);

    printf("Starting SD card...\n");
    x = sd.init();
    if(x != 0) {
        bad();
        printf("Error initialising SD card (%d).\n", x);
        return -1;
    }
    printf("SD card started.\n");

    printf("Initialising file storage...\n");
    fcc_status_e status = fcc_init();
    if(status != FCC_STATUS_SUCCESS) {
        bad();
        printf("Error initialising file storage (%d).\n", status);
        return -1;
    }
    printf("File storage initialised.\n");

    // Resets storage to an empty state.
    // Use this function when you want to clear storage from all the factory-tool generated data and user data.
    // After this operation device must be injected again by using factory tool or developer certificate.
#ifdef RESET_STORAGE
//#if 1
    printf("Reseting storage to an empty state...\n");
    fcc_status_e deleteStatus = fcc_storage_delete();
    if (deleteStatus != FCC_STATUS_SUCCESS) {
        printf("Failed to delete storage - %d\n", deleteStatus);
    }
#endif

#ifdef MBED_CONF_APP_DEVELOPER_MODE
    printf("Starting developer flow...\n");
    status = fcc_developer_flow();
    if (status == FCC_STATUS_KCM_FILE_EXIST_ERROR) {
        printf("Developer credentials already exist.\n");
    } else if (status != FCC_STATUS_SUCCESS) {
        bad();
        printf("Failed to load developer credentials.\n");
        return -1;
    }    
#endif

    printf("Checking configuration...\n");
    status = fcc_verify_device_configured_4mbed_cloud();
    if (status != FCC_STATUS_SUCCESS) {
        bad();
        printf("Device not configured for mbed Cloud.\n");
        return -1;
    }

    // Note sure if this is required or not
    srand(time(NULL));

    printf("Initialising cellular...\n");
    UbloxPPPCellularInterface *cellular = new UbloxPPPCellularInterface(MDMTXD, MDMRXD, 230400, false);
    if (cellular->init()) {
        printf("Please wait up to 180 seconds to connect to the packet network...\n");
        if (cellular->connect() != NSAPI_ERROR_OK) {
            bad();
            printf("Unable to connect to the cellular packet network.\n");
            return -1;
        }
    } else {
        bad();
        printf("Unable to initialise cellular.\n");
        return -1;
    }

    heapStats();
    printf("Initialising cloud client...\n");
    CloudClientDm *cloudClientDm = new CloudClientDm(true,
                                                     &cloudClientRegisteredCallback,
                                                     &cloudClientDeregisteredCallback);
    printf("Configuring Device object...\n");
    // TODO add resources to device object

    printf("Creating all the other objects...\n");
    IocM2mPowerControl *powerControlObject = new IocM2mPowerControl(setThing, true, DEBUG_ON);
    cloudClientDm->addObject(powerControlObject->getObject());
    IocM2mLocation *locationObject = new IocM2mLocation(getLocationData, DEBUG_ON);
    cloudClientDm->addObject(locationObject->getObject());
    IocM2mTemperature *temperatureObject = new IocM2mTemperature(getTemperatureData, resetMinMax, -10, +120, "cel", DEBUG_ON);
    cloudClientDm->addObject(temperatureObject->getObject());
    IocM2mConfig *configObject = new IocM2mConfig(setConfigData, &configData, DEBUG_ON);
    cloudClientDm->addObject(configObject->getObject());
    IocM2mAudio *audioObject = new IocM2mAudio(setAudioData, &audioData, DEBUG_ON);
    cloudClientDm->addObject(audioObject->getObject());
    IocM2mDiagnostics *diagnosticsObject = new IocM2mDiagnostics(getDiagnosticsData, DEBUG_ON);
    cloudClientDm->addObject(diagnosticsObject->getObject());

    printf("Starting cloud client...\n");
    if (cloudClientDm->start(cloudClientGlobalUpdateCallback)) {
        printf("Connecting to LWM2M server...\n");
        if (cloudClientDm->connect(cellular)) {
            printf("Connected to LWM2M server, registration should occur soon; press the user button to exit.\n");
            for (x = 0; !buttonPressed; x++) {
                wait_ms(1000);
                toggleGreen();
            }
        } else {
            bad();
            printf("Unable to connect to LWM2M server.\n");
            return -1;
        }
    } else {
        bad();
        printf("Error starting cloud client.\n");
        return -1;
    }

    printf("Stopping cloud client...\n");
    cloudClientDm->stop();
    printf("Deleting objects...\n");
    delete powerControlObject;
    delete locationObject;
    delete temperatureObject;
    delete configObject;
    delete audioObject;
    delete diagnosticsObject;
    printf("Deleting cloud client...\n");
    delete cloudClientDm;
    heapStats();

    printf("Disconnecting network...\n");
    cellular->disconnect();
    printf("Stopping modem...\n");
    cellular->deinit();
    delete cellular;
    printf("All stop.\n");
    ledOff();
}

// End of file
