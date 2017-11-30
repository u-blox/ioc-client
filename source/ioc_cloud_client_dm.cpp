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
#include "factory_configurator_client.h"
#include "log.h"

#include "ioc_cloud_client_dm.h"
#include "ioc_utils.h"
#include "ioc_power_control.h"
#include "ioc_temperature_battery.h"
#include "ioc_location.h"
#include "ioc_config.h"
#include "ioc_audio.h"
#include "ioc_diagnostics.h"

/* This file implements the Cloud Client functionality, bringing
 * together all of the application specific LWM2M objects and
 * populating the Device Management object built into the Cloud
 * Client.
 */
/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// The identifiers for each LWM2M object.
//
// To add a new object:
// - create the object (over in ioc-m2m.h/ioc-m2m.cpp),
// - add an entry for it here,
// - add it to IocM2mObjectPointerUnion,
// - add it to addObject() and removeObject(),
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
 * VARIABLES
 * -------------------------------------------------------------- */

// The Mbed Cloud Client stuff.
static CloudClientDm *gpCloudClientDm = NULL;
static UpdateCallback *gpCloudClientGlobalUpdateCallback = NULL;

// Array of LWM2M objects for the control plane.
// NOTE: this array is intended to be indexed using the
// IocM2mObjectId enum.
static IocM2mObject gObjectList[MAX_NUM_IOC_M2M_OBJECTS];

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: OBJECT HANDLING
 * -------------------------------------------------------------- */

// Add an LWM2M object to the relevant lists
static void addObject(IocM2mObjectId id, void *pObject)
{
    printf ("Adding object with ID %d.\n", id);

    MBED_ASSERT (gpCloudClientDm);
    MBED_ASSERT (gObjectList[id].object.raw == NULL);
    MBED_ASSERT (!gObjectList[id].updateObservableResources);

    switch (id) {
        case IOC_M2M_POWER_CONTROL:
            gObjectList[id].object.pPowerControl = (IocM2mPowerControl *) pObject;
            gpCloudClientDm->addObject(gObjectList[id].object.pPowerControl->getObject());
            gObjectList[id].updateObservableResources = Callback<void(void)>(gObjectList[id].object.pPowerControl,
                                                                             &IocM2mPowerControl::updateObservableResources);
            break;
        case IOC_M2M_LOCATION:
            gObjectList[id].object.pLocation = (IocM2mLocation *) pObject;
            gpCloudClientDm->addObject(gObjectList[id].object.pLocation->getObject());
            gObjectList[id].updateObservableResources = Callback<void(void)>(gObjectList[id].object.pLocation,
                                                                             &IocM2mLocation::updateObservableResources);
            break;
        case IOC_M2M_TEMPERATURE:
            gObjectList[id].object.pTemperature = (IocM2mTemperature *) pObject;
            gpCloudClientDm->addObject(gObjectList[id].object.pTemperature->getObject());
            gObjectList[id].updateObservableResources = Callback<void(void)>(gObjectList[id].object.pTemperature,
                                                                             &IocM2mTemperature::updateObservableResources);
            break;
        case IOC_M2M_CONFIG:
            gObjectList[id].object.pConfig = (IocM2mConfig *) pObject;
            gpCloudClientDm->addObject(gObjectList[id].object.pConfig->getObject());
            gObjectList[id].updateObservableResources = Callback<void(void)>(gObjectList[id].object.pConfig,
                                                                             &IocM2mConfig::updateObservableResources);
            break;
        case IOC_M2M_AUDIO:
            gObjectList[id].object.pAudio = (IocM2mAudio *) pObject;
            gpCloudClientDm->addObject(gObjectList[id].object.pAudio->getObject());
            gObjectList[id].updateObservableResources = Callback<void(void)>(gObjectList[id].object.pAudio,
                                                                             &IocM2mAudio::updateObservableResources);
            break;
        case IOC_M2M_DIAGNOSTICS:
            gObjectList[id].object.pDiagnostics = (IocM2mDiagnostics *) pObject;
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
static void removeObject(IocM2mObjectId id)
{
    if (gObjectList[id].object.raw != NULL) {
        switch (id) {
            case IOC_M2M_POWER_CONTROL:
                gObjectList[id].object.pPowerControl = NULL;
                break;
            case IOC_M2M_LOCATION:
                gObjectList[id].object.pLocation = NULL;
                break;
            case IOC_M2M_TEMPERATURE:
                gObjectList[id].object.pTemperature = NULL;
                break;
            case IOC_M2M_CONFIG:
                gObjectList[id].object.pConfig = NULL;
                break;
            case IOC_M2M_AUDIO:
                gObjectList[id].object.pAudio = NULL;
                break;
            case IOC_M2M_DIAGNOSTICS:
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

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: CLOUD CLIENT CALLBACKS
 * -------------------------------------------------------------- */

// Callback when mbed Cloud Client registers with the LWM2M server.
static void cloudClientRegisteredCallback()
{
    flash();
    good();
    LOG(EVENT_CLOUD_CLIENT_REGISTERED, 0);
    printf("Mbed Cloud Client is registered, press the user button to exit.\n");
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

/* ----------------------------------------------------------------
 * PUBLIC
 * -------------------------------------------------------------- */

// Initialise the Mbed Cloud Client and its Device Management object.
// Note: here be multiple return statements.
// Note: the process of connecting to the server is kept separate as
// having cellular running while doing this Cloud Client initialisation
// puts too much stuff on the stack.
CloudClientDm *pInitCloudClientDm()
{
    bool cloudClientConfigGood = false;
    bool dmObjectConfigGood = false;
    int x = 0;
    int y = 0;

    flash();
    LOG(EVENT_CLOUD_CLIENT_FILE_STORAGE_INIT, 0);
    printf("Initialising Mbed Cloud Client file storage...\n");
    fcc_status_e status = fcc_init();
    if(status != FCC_STATUS_SUCCESS) {
        bad();
        printFccError();
        LOG(EVENT_CLOUD_CLIENT_FILE_STORAGE_INIT_FAILURE, 0);
        printf("Error initialising Mbed Cloud Client file storage (%d).\n", status);
        return NULL;
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
            feedWatchdog();
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
                return NULL;
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
                    return NULL;
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
            if (isExternalPowerPresent()) {
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
        delete gpCloudClientDm;
        gpCloudClientDm = NULL;
        bad();
        LOG(EVENT_CLOUD_CLIENT_CONFIG_DM_FAILURE, 0);
        printf("Unable to configure the Device object after %d attempt(s).\n", y - 1);
        return gpCloudClientDm;
    }

    flash();
    LOG(EVENT_CREATE_LWM2M_OBJECTS, 0);
    printf("Creating all the other LWM2M objects...\n");
    for (unsigned int x = 0; x < sizeof (gObjectList) / sizeof(gObjectList[0]); x++) {
        gObjectList[x].object.raw = NULL;
        gObjectList[x].updateObservableResources = NULL;
    }
    addObject(IOC_M2M_POWER_CONTROL, (void *) pInitPowerControl());
    addObject(IOC_M2M_LOCATION, (void *) pInitLocation());
    addObject(IOC_M2M_TEMPERATURE, (void *) pInitTemperature());
    addObject(IOC_M2M_CONFIG, (void *) pInitConfig());
    addObject(IOC_M2M_AUDIO, (void *) pInitAudio());
    addObject(IOC_M2M_DIAGNOSTICS, (void *) pInitDiagnostics());
    
    if (configIsGnssEnabled()) {
        startGnss();
    }

    flash();
    LOG(EVENT_CLOUD_CLIENT_START, 0);
    printf("Starting Mbed Cloud Client...\n");
    gpCloudClientGlobalUpdateCallback = new UpdateCallback();
    if (!gpCloudClientDm->start(gpCloudClientGlobalUpdateCallback)) {
        delete gpCloudClientDm;
        gpCloudClientDm = NULL;
        bad();
        LOG(EVENT_CLOUD_CLIENT_START_FAILURE, 0);
        printf("Error starting Mbed Cloud Client.\n");
        return gpCloudClientDm;
    }

    // !!! SUCCESS !!!

    return gpCloudClientDm;
}

// Connect the Mbed Cloud Client to the LWM2M server.
bool connectCloudClientDm(NetworkInterface *pNetworkInterface)
{
    bool success = false;

    if (gpCloudClientDm != NULL) {
        flash();
        LOG(EVENT_CLOUD_CLIENT_CONNECTING, 0);
        printf("Connecting to LWM2M server...\n");
        if (!gpCloudClientDm->connect(pNetworkInterface)) {
            bad();
            LOG(EVENT_CLOUD_CLIENT_CONNECT_FAILURE, 0);
            printf("Unable to connect to LWM2M server.\n");
        } else {
            success = true;
            LOG(EVENT_CLOUD_CLIENT_CONNECTED, 0);
            printf("Connected to LWM2M server, please wait for registration to complete...\n");
        }
    }

    return success;
}

// Shut down the Mbed Cloud Client.
void deinitCloudClientDm()
{
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

    // Stop audio first, so that all our diagnostics
    // will be complete when we stop
    deinitAudio();

    deinitPowerControl();
    deinitLocation();
    deinitTemperature();
    deinitConfig();
    deinitDiagnostics();

    for (unsigned int x = 0; x < sizeof (gObjectList) / sizeof(gObjectList[0]); x++) {
        removeObject((IocM2mObjectId) x);
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
}

// Return whether the Cloud Client has connected.
bool isCloudClientConnected()
{
    bool isConnected = false;
    
    if (gpCloudClientDm != NULL) {
        isConnected = gpCloudClientDm->isConnected();
    }
    
    return isConnected;
}

// Callback to update the observable values in all of the LWM2M objects.
void cloudClientObjectUpdate()
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
        if (isBatteryDetected()) {
            if (getBatteryVoltage(&voltageMV)) {
                LOG(EVENT_BATTERY_VOLTAGE, voltageMV);
                gpCloudClientDm->setDeviceObjectVoltage(CloudClientDm::POWER_SOURCE_INTERNAL_BATTERY,
                                                        voltageMV);
            }
            if (getBatteryCurrent(&currentMA)) {
                LOG(EVENT_BATTERY_CURRENT, currentMA);
                gpCloudClientDm->setDeviceObjectCurrent(CloudClientDm::POWER_SOURCE_INTERNAL_BATTERY,
                                                        currentMA);
            }
            if (getBatteryRemainingPercentage(&batteryLevelPercent)) {
                LOG(EVENT_BATTERY_PERCENTAGE, batteryLevelPercent);
                gpCloudClientDm->setDeviceObjectBatteryLevel(batteryLevelPercent);
            }
        }
        // Make sure we are lined up with the USB power state
        if (isExternalPowerPresent() &&
            !gpCloudClientDm->existsDeviceObjectPowerSource(CloudClientDm::POWER_SOURCE_USB)) {
            LOG(EVENT_EXTERNAL_POWER_ON, 0);
            gpCloudClientDm->addDeviceObjectPowerSource(CloudClientDm::POWER_SOURCE_USB);
        } else if (!isExternalPowerPresent() &&
                   gpCloudClientDm->existsDeviceObjectPowerSource(CloudClientDm::POWER_SOURCE_USB)) {
            LOG(EVENT_EXTERNAL_POWER_OFF, 0);
            gpCloudClientDm->deleteDeviceObjectPowerSource(CloudClientDm::POWER_SOURCE_USB);
        }

        fault = getChargerFaults();
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
                switch (getChargerState()) {
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
        gpCloudClientDm->setDeviceObjectBatteryStatus(batteryStatus);
    }

    // Check if there's been a request to switch off GNSS before
    // we go observing it.
    if (getPendingGnssStop()) {
        stopGnss();
        setPendingGnssStop(false);
    }

    // Now do all the other observable resources
    for (unsigned int x = 0; x < sizeof (gObjectList) /
                                 sizeof (gObjectList[0]); x++) {
        if (gObjectList[x].updateObservableResources) {
            gObjectList[x].updateObservableResources();
        }
    }
}

// End of file
