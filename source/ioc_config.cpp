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
#include "low_power.h"
#include "log.h"

#include "ioc_cloud_client_dm.h"
#include "ioc_config.h"
#include "ioc_utils.h"
#include "ioc_dynamics.h"
#include "ioc_location.h"

/* This file implements the LWM2M configuration object.
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

// Configuration data storage.
BACKUP_SRAM
static ConfigLocal gConfigLocal;
static IocM2mConfig *gpM2mObject = NULL;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: HOOKS FOR CONFIG M2M C++ OBJECT
 * -------------------------------------------------------------- */

// Callback that sets the configuration data via the IocM2mConfig
// object.
static void setConfigData(const IocM2mConfig::Config *pData)
{
    // Something has happened, tell Ready mode about it
    readyModeInstructionReceived();

    printf("Received new config settings:\n");
    printf("  initWakeUpTickCounterPeriod %f.\n", pData->initWakeUpTickCounterPeriod);
    printf("  initWakeUpTickCounterModulo %lld.\n", pData->initWakeUpTickCounterModulo);
    printf("  readyWakeUpTickCounterPeriod1 %f.\n", pData->readyWakeUpTickCounterPeriod1);
    printf("  readyWakeUpTickCounterPeriod2 %f.\n", pData->readyWakeUpTickCounterPeriod2);
    printf("  readyWakeUpTickCounterModulo %lld.\n", pData->readyWakeUpTickCounterModulo);
    printf("  GNSS enable %d.\n", pData->gnssEnable);

    /// Handle GNSS configuration changes
    if (!isGnssOn() && pData->gnssEnable) {
        startGnss();
    } else if (isGnssOn() && !pData->gnssEnable) {
        setPendingGnssStop(true);
    }

    // TODO act on the rest of it

    gConfigLocal.initWakeUpTickCounterPeriod = (time_t) pData->initWakeUpTickCounterPeriod;
    gConfigLocal.initWakeUpTickCounterModulo = pData->initWakeUpTickCounterModulo;
    gConfigLocal.readyWakeUpTickCounterPeriod1 = (time_t) pData->readyWakeUpTickCounterPeriod1;
    gConfigLocal.readyWakeUpTickCounterPeriod2 = (time_t) pData->readyWakeUpTickCounterPeriod2;
    gConfigLocal.readyWakeUpTickCounterModulo = pData->readyWakeUpTickCounterModulo;
    gConfigLocal.gnssEnable = pData->gnssEnable;
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

/* ----------------------------------------------------------------
 * PUBLIC: INITIALISATION
 * -------------------------------------------------------------- */

// Reset the configuration object.
void resetConfig()
{
    gConfigLocal.initWakeUpTickCounterPeriod = CONFIG_DEFAULT_INIT_WAKE_UP_TICK_COUNTER_PERIOD;
    gConfigLocal.initWakeUpTickCounterModulo = CONFIG_DEFAULT_INIT_WAKE_UP_TICK_COUNTER_MODULO;
    gConfigLocal.readyWakeUpTickCounterPeriod1 = CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_PERIOD_1;
    gConfigLocal.readyWakeUpTickCounterPeriod2 = CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_PERIOD_2;
    gConfigLocal.readyWakeUpTickCounterModulo = CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_MODULO;
    gConfigLocal.gnssEnable = CONFIG_DEFAULT_GNSS_ENABLE;
}

// Initialise the configuration object.
IocM2mConfig *pInitConfig()
{
    IocM2mConfig::Config *pTempStore = new IocM2mConfig::Config;
    
    gpM2mObject = new IocM2mConfig(setConfigData,
                                   convertConfigLocalToM2m(pTempStore, &gConfigLocal),
                                   MBED_CONF_APP_OBJECT_DEBUG_ON);
    delete pTempStore;
    
    return gpM2mObject;
}

// Shut down configuration object.
void deinitConfig()
{
    delete gpM2mObject;
    gpM2mObject = NULL;
}

// Get the value of initWakeUpTickCounterPeriod.
time_t getInitWakeUpTickCounterPeriod()
{
    return gConfigLocal.initWakeUpTickCounterPeriod;
}

// Get the value of initWakeUpTickCounterPeriod.
int64_t getInitWakeUpTickCounterModulo()
{
    return gConfigLocal.initWakeUpTickCounterModulo;
}

// Get the value of readyWakeUpTickCounterPeriod1.
time_t getReadyWakeUpTickCounterPeriod1()
{
    return gConfigLocal.readyWakeUpTickCounterPeriod1;
}

// Get the value of readyWakeUpTickCounterPeriod2.
time_t getReadyWakeUpTickCounterPeriod2()
{
    return gConfigLocal.readyWakeUpTickCounterPeriod2;
}

// Get the value of readyWakeUpTickCounterModulo.
int64_t getReadyWakeUpTickCounterModulo()
{
    return gConfigLocal.readyWakeUpTickCounterModulo;
}

// Return whether GNSS is configured on or off.
bool configIsGnssEnabled()
{
    return gConfigLocal.gnssEnable;
}

/* ----------------------------------------------------------------
 * PUBLIC: CONFIG M2M C++ OBJECT
 * -------------------------------------------------------------- */

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const M2MObjectHelper::DefObject IocM2mConfig::_defObject =
    {0, "32769", 6,
        RESOURCE_INSTANCE_INIT_WAKE_UP, RESOURCE_NUMBER_INIT_WAKE_UP_TICK_COUNTER_PERIOD, "seconds", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, NULL,
        RESOURCE_INSTANCE_INIT_WAKE_UP, RESOURCE_NUMBER_INIT_WAKE_UP_TICK_COUNTER_MODULO, "modulo", M2MResourceBase::INTEGER, false, M2MBase::GET_PUT_ALLOWED, NULL,
        RESOURCE_INSTANCE_READY_WAKE_UP_TICK_COUNTER_PERIOD_1, RESOURCE_NUMBER_READY_WAKE_UP_TICK_COUNTER_PERIOD_1, "seconds", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, NULL,
        RESOURCE_INSTANCE_READY_WAKE_UP_TICK_COUNTER_PERIOD_2, RESOURCE_NUMBER_READY_WAKE_UP_TICK_COUNTER_PERIOD_2, "seconds", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, NULL,
        RESOURCE_INSTANCE_READY_WAKE_UP_TICK_COUNTER_MODULO, RESOURCE_NUMBER_READY_WAKE_UP_TICK_COUNTER_MODULO, "modulo", M2MResourceBase::INTEGER, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_GNSS_ENABLE, "boolean", M2MResourceBase::BOOLEAN, false, M2MBase::GET_PUT_ALLOWED, NULL
    };

// Constructor.
IocM2mConfig::IocM2mConfig(Callback<void(const Config *)> setCallback,
                           Config *initialValues,
                           bool debugOn)
             :M2MObjectHelper(&_defObject,
                              value_updated_callback(this, &IocM2mConfig::objectUpdated),
                              NULL,
                              debugOn)
{
    _setCallback = setCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Set the initial values
    MBED_ASSERT(setResourceValue(initialValues->initWakeUpTickCounterPeriod,
                                 RESOURCE_NUMBER_INIT_WAKE_UP_TICK_COUNTER_PERIOD, RESOURCE_INSTANCE_INIT_WAKE_UP));
    MBED_ASSERT(setResourceValue(initialValues->initWakeUpTickCounterModulo,
                                 RESOURCE_NUMBER_INIT_WAKE_UP_TICK_COUNTER_MODULO, RESOURCE_INSTANCE_INIT_WAKE_UP));
    MBED_ASSERT(setResourceValue(initialValues->readyWakeUpTickCounterPeriod1,
                                 RESOURCE_NUMBER_READY_WAKE_UP_TICK_COUNTER_PERIOD_1, RESOURCE_INSTANCE_READY_WAKE_UP_TICK_COUNTER_PERIOD_1));
    MBED_ASSERT(setResourceValue(initialValues->readyWakeUpTickCounterPeriod2,
                                 RESOURCE_NUMBER_READY_WAKE_UP_TICK_COUNTER_PERIOD_2, RESOURCE_INSTANCE_READY_WAKE_UP_TICK_COUNTER_PERIOD_2));
    MBED_ASSERT(setResourceValue(initialValues->readyWakeUpTickCounterModulo,
                                 RESOURCE_NUMBER_READY_WAKE_UP_TICK_COUNTER_MODULO, RESOURCE_INSTANCE_READY_WAKE_UP_TICK_COUNTER_MODULO));
    MBED_ASSERT(setResourceValue(initialValues->gnssEnable, RESOURCE_NUMBER_GNSS_ENABLE));

    printf("IocM2mConfig: object initialised.\n");
}

// Destructor.
IocM2mConfig::~IocM2mConfig()
{
}

// Callback when the object is updated by the server.
void IocM2mConfig::objectUpdated(const char *resourceName)
{
    Config config;

    printf("IocM2mConfig: resource \"%s\" has been updated.\n", resourceName);

    MBED_ASSERT(getResourceValue(&config.initWakeUpTickCounterPeriod,
                                 RESOURCE_NUMBER_INIT_WAKE_UP_TICK_COUNTER_PERIOD, RESOURCE_INSTANCE_INIT_WAKE_UP));
    MBED_ASSERT(getResourceValue(&config.initWakeUpTickCounterModulo,
                                 RESOURCE_NUMBER_INIT_WAKE_UP_TICK_COUNTER_MODULO, RESOURCE_INSTANCE_INIT_WAKE_UP));
    MBED_ASSERT(getResourceValue(&config.readyWakeUpTickCounterPeriod1,
                                 RESOURCE_NUMBER_READY_WAKE_UP_TICK_COUNTER_PERIOD_1, RESOURCE_INSTANCE_READY_WAKE_UP_TICK_COUNTER_PERIOD_1));
    MBED_ASSERT(getResourceValue(&config.readyWakeUpTickCounterPeriod2,
                                 RESOURCE_NUMBER_READY_WAKE_UP_TICK_COUNTER_PERIOD_2, RESOURCE_INSTANCE_READY_WAKE_UP_TICK_COUNTER_PERIOD_2));
    MBED_ASSERT(getResourceValue(&config.readyWakeUpTickCounterModulo,
                                 RESOURCE_NUMBER_READY_WAKE_UP_TICK_COUNTER_MODULO, RESOURCE_INSTANCE_READY_WAKE_UP_TICK_COUNTER_MODULO));
    MBED_ASSERT(getResourceValue(&config.gnssEnable,
                                 RESOURCE_NUMBER_GNSS_ENABLE));

    printf("IocM2mConfig: new config is:\n");
    printf("  initWakeUpTickCounterPeriod %f.\n", config.initWakeUpTickCounterPeriod);
    printf("  initWakeUpTickCounterModulo %lld.\n", config.initWakeUpTickCounterModulo);
    printf("  readyWakeUpTickCounterPeriod1 %f.\n", config.readyWakeUpTickCounterPeriod1);
    printf("  readyWakeUpTickCounterPeriod2 %f.\n", config.readyWakeUpTickCounterPeriod2);
    printf("  readyWakeUpTickCounterModulo %lld.\n", config.readyWakeUpTickCounterModulo);
    printf("  GNSS enable %d.\n", config.gnssEnable);

    if (_setCallback) {
        _setCallback(&config);
    }
}

// End of file
