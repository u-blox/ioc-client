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
#include "log.h"

#include "ioc_cloud_client_dm.h"
#include "ioc_dynamics.h"
#include "ioc_temperature_battery.h"
#include "ioc_utils.h"

/* This file implements the establishment and control of all the
 * items on the I2C interface, specifically the battery charger,
 * and battery gauge, and includes the temperature monitoring LWM2M
 * object.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The static temperature object values.
#define TEMPERATURE_MIN_MEASURABLE_RANGE -10.0
#define TEMPERATURE_MAX_MEASURABLE_RANGE 120.0
#define TEMPERATURE_UNITS "cel"

// The minimum Voltage limit that must be set in the battery
// charger chip to make USB operation reliable.
#define MIN_INPUT_VOLTAGE_LIMIT_MV  3880

/* ----------------------------------------------------------------
 * STATIC FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

static bool getTemperatureData(IocM2mTemperature::Temperature *pData);
static void executeResetTemperatureMinMax();

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

static I2C *gpI2C = NULL;
static TemperatureLocal gTemperatureLocal = {0};
static IocM2mTemperature *gpM2mObject = NULL;
static BatteryGaugeBq27441 *gpBatteryGauge = NULL;
static BatteryChargerBq24295 *gpBatteryCharger = NULL;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: HOOKS FOR TEMPERATURE M2M C++ OBJECT
 * -------------------------------------------------------------- */

// Callback that retrieves temperature data for the IocM2mTemperature
// object.
static bool getTemperatureData(IocM2mTemperature::Temperature *pData)
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

        pData->temperature = (float) gTemperatureLocal.nowC;
        pData->minTemperature = (float) gTemperatureLocal.minC;
        pData->maxTemperature = (float) gTemperatureLocal.maxC;
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

/* ----------------------------------------------------------------
 * PUBLIC: INITIALISATION
 * -------------------------------------------------------------- */

// Initialise stuff on the I2C bus.
I2C *pInitI2C()
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
    if ((gpBatteryGauge == NULL) && (gpBatteryCharger == NULL)) {
        LOG(EVENT_I2C_STOP, 0);
        delete gpI2C;
        gpI2C = NULL;
    }

    return gpI2C;
}

// Init temperature stuff.
IocM2mTemperature *pInitTemperature()
{
    gpM2mObject = new IocM2mTemperature(getTemperatureData,
                                       executeResetTemperatureMinMax,
                                       TEMPERATURE_MIN_MEASURABLE_RANGE,
                                       TEMPERATURE_MAX_MEASURABLE_RANGE,
                                       TEMPERATURE_UNITS,
                                       MBED_CONF_APP_OBJECT_DEBUG_ON);

    return gpM2mObject;
}

// Shut down temperature stuff.
void deinitTemperature()
{
    delete gpM2mObject;
    gpM2mObject = NULL;
}

// Shut down the I2C stuff.
void deinitI2C()
{
    if (gpBatteryCharger != NULL) {
        flash();
        LOG(EVENT_BATTERY_CHARGER_BQ24295_STOP, 0);
        printf("Stopping battery charger...\n");
        delete gpBatteryCharger;
        gpBatteryCharger = NULL;
    }

    if (gpBatteryGauge != NULL) {
        flash();
        LOG(EVENT_BATTERY_GAUGE_BQ27441_STOP, 0);
        printf("Stopping battery gauge...\n");
        gpBatteryGauge->disableGauge();
        delete gpBatteryGauge;
        gpBatteryGauge = NULL;
    }

    if (gpI2C != NULL) {
        flash();
        LOG(EVENT_I2C_STOP, 0);
        printf("Stopping I2C...\n");
        delete gpI2C;
        gpI2C = NULL;
    }
}

/* ----------------------------------------------------------------
 * PUBLIC: BATTERY FUNCTIONS
 * -------------------------------------------------------------- */

// Determine whether a battery is detected or not.
bool isBatteryDetected()
{
    bool batteryDetected = false;
    
    if (gpBatteryGauge != NULL) {
        batteryDetected = gpBatteryGauge->isBatteryDetected();
    }
    
    return batteryDetected;
}

// Determine whether external power is present or not.
bool isExternalPowerPresent()
{
    bool externalPowerPresent = false;
    
    if (gpBatteryCharger != NULL) {
        externalPowerPresent = gpBatteryCharger->isExternalPowerPresent();
    }
    
    return externalPowerPresent;
}

// Return the battery voltage.
bool getBatteryVoltage(int32_t *pVoltageMV)
{
    bool success = false;
    
    if (gpBatteryGauge != NULL) {
        success = gpBatteryGauge->getVoltage(pVoltageMV);
    }
    
    return success;
}

// Return the battery current.
bool getBatteryCurrent(int32_t *pCurrentMA)
{
    bool success = false;
    
    if (gpBatteryGauge != NULL) {
        success = gpBatteryGauge->getCurrent(pCurrentMA);
    }
    
    return success;
}

// Return the battery level as a percentage.
bool getBatteryRemainingPercentage(int32_t *pBatteryLevelPercent)
{
    bool success = false;
    
    if (gpBatteryGauge != NULL) {
        success = gpBatteryGauge->getRemainingPercentage(pBatteryLevelPercent);
    }
    
    return success;
}

// Return a bitmap containing the charger faults, which
// may be tested against BatteryChargerBq24295:ChargerFault.
char getChargerFaults()
{
    char faults = 0;
    
    if (gpBatteryCharger != NULL) {
        faults = gpBatteryCharger->getChargerFaults();
    }
    
    return faults;
}

// Return the charger state.
BatteryChargerBq24295::ChargerState getChargerState()
{
    BatteryChargerBq24295::ChargerState chargerState = BatteryChargerBq24295::CHARGER_STATE_UNKNOWN;
    
    if (gpBatteryCharger != NULL) {
        chargerState = gpBatteryCharger->getChargerState();
    }
    
    return chargerState;
}

/* ----------------------------------------------------------------
 * PUBLIC: TEMPERATURE M2M C++ OBJECT
 * -------------------------------------------------------------- */

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const M2MObjectHelper::DefObject IocM2mTemperature::_defObject =
    {0, "3303", 7,
        -1, RESOURCE_NUMBER_TEMPERATURE, "temperature", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_MIN_TEMPERATURE, "temperature", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_MAX_TEMPERATURE, "temperature", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_RESET_MIN_MAX, "string", M2MResourceBase::STRING, false, M2MBase::POST_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_MIN_RANGE, "temperature", M2MResourceBase::FLOAT, false, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_MAX_RANGE, "temperature", M2MResourceBase::FLOAT, false, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_UNITS, "string", M2MResourceBase::STRING, false, M2MBase::GET_ALLOWED, NULL
    };

// Constructor.
IocM2mTemperature::IocM2mTemperature(Callback<bool(Temperature *)> getCallback,
                                     Callback<void(void)> resetMinMaxCallback,
                                     float minRange,
                                     float maxRange,
                                     String units,
                                     bool debugOn)
                  :M2MObjectHelper(&_defObject, NULL, NULL, debugOn)
{
    _getCallback = getCallback;
    _resetMinMaxCallback = resetMinMaxCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Set the fixed value resources here
    MBED_ASSERT(setResourceValue(minRange, RESOURCE_NUMBER_MIN_RANGE));
    MBED_ASSERT(setResourceValue(maxRange, RESOURCE_NUMBER_MAX_RANGE));
    MBED_ASSERT(setResourceValue(units, RESOURCE_NUMBER_UNITS));

    // Set the execute function
    if (resetMinMaxCallback) {
        MBED_ASSERT(setExecuteCallback(execute_callback(this, &IocM2mTemperature::executeFunction), RESOURCE_NUMBER_RESET_MIN_MAX));
    }

    // Update the observable resources
    updateObservableResources();

    printf("IocM2mTemperature: object initialised.\n");
}

// Destructor.
IocM2mTemperature::~IocM2mTemperature()
{
}

// Update the observable data for this object.
void IocM2mTemperature::updateObservableResources()
{
    Temperature data;

    // Update the data
    if (_getCallback) {
        if (_getCallback(&data)) {
            // Set the values in the resources based on the new data
            MBED_ASSERT(setResourceValue(data.temperature, RESOURCE_NUMBER_TEMPERATURE));
            MBED_ASSERT(setResourceValue(data.minTemperature, RESOURCE_NUMBER_MIN_TEMPERATURE));
            MBED_ASSERT(setResourceValue(data.maxTemperature, RESOURCE_NUMBER_MAX_TEMPERATURE));
        }
    }
}

// Executable function.
void IocM2mTemperature::executeFunction (void *parameter)
{
    printf("IocM2mTemperature: reset min/max received.\n");

    if (_resetMinMaxCallback) {
        _resetMinMaxCallback();
    }
}

// End of file
