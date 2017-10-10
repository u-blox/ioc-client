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
#include "ioc_control.h"

#define printfLog(format, ...) debug_if(_debugOn, format, ## __VA_ARGS__)

/**********************************************************************
 * POWER CONTROL OBJECT
 **********************************************************************/

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const M2MObjectHelper::DefObject IocCtrlPowerControl::_defObject =
    {0, "3312", 1,
        -1, RESOURCE_NUMBER_POWER_SWITCH, "on/off", M2MResourceBase::BOOLEAN, false, M2MBase::GET_PUT_ALLOWED, NULL
    };

// Constructor.
IocCtrlPowerControl::IocCtrlPowerControl(bool debugOn,
                                         Callback<void(bool)> setCallback,
                                         bool initialValue)
                    :M2MObjectHelper(debugOn, &_defObject,
                                     value_updated_callback(this, &IocCtrlPowerControl::objectUpdated))
{
    _setCallback = setCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Set the initial value
    MBED_ASSERT(setResourceValue(initialValue, RESOURCE_NUMBER_POWER_SWITCH));

    printfLog("IocCtrlPowerControl: object initialised.\n");
}

// Destructor.
IocCtrlPowerControl::~IocCtrlPowerControl()
{
}

// Callback when the object is updated.
void IocCtrlPowerControl::objectUpdated(const char *resourceName)
{
    bool onNotOff;

    printfLog("IocCtrlPowerControl: resource \"%s\" has been updated.\n", resourceName);

    MBED_ASSERT(getResourceValue(&onNotOff, RESOURCE_NUMBER_POWER_SWITCH));

    printfLog("IocCtrlPowerControl: new value is %d.\n", onNotOff);

    if (_setCallback) {
        _setCallback(onNotOff);
    }
}

/**********************************************************************
 * LOCATION OBJECT
 **********************************************************************/

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const M2MObjectHelper::DefObject IocCtrlLocation::_defObject =
    {0, "6", 6,
        -1, RESOURCE_NUMBER_LATITUDE, "latitude", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_DEGREES,
        -1, RESOURCE_NUMBER_LONGITUDE, "longitude", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_DEGREES,
        -1, RESOURCE_NUMBER_RADIUS, "radius", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_METRES,
        -1, RESOURCE_NUMBER_ALTITUDE, "altitude", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_METRES,
        -1, RESOURCE_NUMBER_SPEED, "speed", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_SPEED,
        -1, RESOURCE_NUMBER_TIMESTAMP, "timestamp", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
    };

// Constructor.
IocCtrlLocation::IocCtrlLocation(bool debugOn,
                                 Callback<bool(Location *)> getCallback)
                :M2MObjectHelper(debugOn, &_defObject)
{
    _getCallback = getCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Update the values held in the resources
    updateObservableResources();

    printfLog("IocCtrlLocation: object initialised.\n");
}

// Destructor.
IocCtrlLocation::~IocCtrlLocation()
{
}

// Update the observable data for this object.
void IocCtrlLocation::updateObservableResources()
{
    Location data;

    // Update the data
    if (_getCallback) {
        if (_getCallback(&data)) {
            // Set the values in the resources based on the new data
            MBED_ASSERT(setResourceValue(data.latitudeDegrees, RESOURCE_NUMBER_LATITUDE));
            MBED_ASSERT(setResourceValue(data.longitudeDegrees, RESOURCE_NUMBER_LONGITUDE));
            MBED_ASSERT(setResourceValue(data.radiusMetres, RESOURCE_NUMBER_RADIUS));
            MBED_ASSERT(setResourceValue(data.altitudeMetres, RESOURCE_NUMBER_ALTITUDE));
            MBED_ASSERT(setResourceValue(data.speedMPS, RESOURCE_NUMBER_SPEED));
            MBED_ASSERT(setResourceValue(data.timestampUnix, RESOURCE_NUMBER_TIMESTAMP));
        }
    }
}

/**********************************************************************
 * TEMPERATURE OBJECT
 **********************************************************************/

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const M2MObjectHelper::DefObject IocCtrlTemperature::_defObject =
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
IocCtrlTemperature::IocCtrlTemperature(bool debugOn,
                                       Callback<bool(Temperature *)> getCallback,
                                       Callback<void(void)> resetMinMaxCallback,
                                       float minRange,
                                       float maxRange,
                                       String units)
                   :M2MObjectHelper(debugOn, &_defObject)
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
        MBED_ASSERT(setExecuteCallback(execute_callback(this, &IocCtrlTemperature::executeFunction), RESOURCE_NUMBER_RESET_MIN_MAX));
    }

    // Update the observable resources
    updateObservableResources();

    printfLog("IocCtrlTemperature: object initialised.\n");
}

// Destructor.
IocCtrlTemperature::~IocCtrlTemperature()
{
}

// Update the observable data for this object.
void IocCtrlTemperature::updateObservableResources()
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

/** Executable function.
 */
void IocCtrlTemperature::executeFunction (void *parameter)
{
    printfLog("IocCtrlTemperature: reset min/max received.\n");

    if (_resetMinMaxCallback) {
        _resetMinMaxCallback();
    }
}

/**********************************************************************
 * CONFIG OBJECT
 **********************************************************************/

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const M2MObjectHelper::DefObject IocCtrlConfig::_defObject =
    {0, "32769", 6,
        RESOURCE_INSTANCE_INIT_WAKE_UP, RESOURCE_NUMBER_INIT_WAKE_UP_TICK_PERIOD, "seconds", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, NULL,
        RESOURCE_INSTANCE_INIT_WAKE_UP, RESOURCE_NUMBER_INIT_WAKE_UP_COUNT, "counter", M2MResourceBase::INTEGER, false, M2MBase::GET_PUT_ALLOWED, NULL,
        RESOURCE_INSTANCE_NORMAL_WAKE_UP, RESOURCE_NUMBER_NORMAL_WAKE_UP_TICK_PERIOD, "seconds", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, NULL,
        RESOURCE_INSTANCE_NORMAL_WAKE_UP, RESOURCE_NUMBER_NORMAL_WAKE_UP_COUNT, "counter", M2MResourceBase::INTEGER, false, M2MBase::GET_PUT_ALLOWED, NULL,
        RESOURCE_INSTANCE_BATTERY_WAKE_UP, RESOURCE_NUMBER_BATTERY_WAKE_UP_TICK_PERIOD, "seconds", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_GNSS_ENABLE, "boolean", M2MResourceBase::BOOLEAN, false, M2MBase::GET_PUT_ALLOWED, NULL
    };

// Constructor.
IocCtrlConfig::IocCtrlConfig(bool debugOn,
                             Callback<void(Config *)> setCallback,
                             Config *initialValues)
              :M2MObjectHelper(debugOn, &_defObject,
                               value_updated_callback(this, &IocCtrlConfig::objectUpdated))
{
    _setCallback = setCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Set the initial values
    MBED_ASSERT(setResourceValue(initialValues->initWakeUpTickPeriod,
                                 RESOURCE_NUMBER_INIT_WAKE_UP_TICK_PERIOD, RESOURCE_INSTANCE_INIT_WAKE_UP));
    MBED_ASSERT(setResourceValue(initialValues->initWakeUpCount,
                                 RESOURCE_NUMBER_INIT_WAKE_UP_COUNT, RESOURCE_INSTANCE_INIT_WAKE_UP));
    MBED_ASSERT(setResourceValue(initialValues->normalWakeUpTickPeriod,
                                 RESOURCE_NUMBER_NORMAL_WAKE_UP_TICK_PERIOD, RESOURCE_INSTANCE_NORMAL_WAKE_UP));
    MBED_ASSERT(setResourceValue(initialValues->normalWakeUpCount,
                                 RESOURCE_NUMBER_NORMAL_WAKE_UP_COUNT, RESOURCE_INSTANCE_NORMAL_WAKE_UP));
    MBED_ASSERT(setResourceValue(initialValues->batteryWakeUpTickPeriod,
                                 RESOURCE_NUMBER_BATTERY_WAKE_UP_TICK_PERIOD, RESOURCE_INSTANCE_BATTERY_WAKE_UP));
    MBED_ASSERT(setResourceValue(initialValues->gnssEnable, RESOURCE_NUMBER_GNSS_ENABLE));

    printfLog("IocCtrlConfig: object initialised.\n");
}

// Destructor.
IocCtrlConfig::~IocCtrlConfig()
{
}

// Callback when the object is updated by the server.
void IocCtrlConfig::objectUpdated(const char *resourceName)
{
    Config config;

    printfLog("IocCtrlConfig: resource \"%s\" has been updated.\n", resourceName);

    MBED_ASSERT(getResourceValue(&config.initWakeUpTickPeriod,
                                 RESOURCE_NUMBER_INIT_WAKE_UP_TICK_PERIOD, RESOURCE_INSTANCE_INIT_WAKE_UP));
    MBED_ASSERT(getResourceValue(&config.initWakeUpCount,
                                 RESOURCE_NUMBER_INIT_WAKE_UP_COUNT, RESOURCE_INSTANCE_INIT_WAKE_UP));
    MBED_ASSERT(getResourceValue(&config.normalWakeUpTickPeriod,
                                 RESOURCE_NUMBER_NORMAL_WAKE_UP_TICK_PERIOD, RESOURCE_INSTANCE_NORMAL_WAKE_UP));
    MBED_ASSERT(getResourceValue(&config.normalWakeUpCount,
                                 RESOURCE_NUMBER_NORMAL_WAKE_UP_COUNT, RESOURCE_INSTANCE_NORMAL_WAKE_UP));
    MBED_ASSERT(getResourceValue(&config.batteryWakeUpTickPeriod,
                                 RESOURCE_NUMBER_BATTERY_WAKE_UP_TICK_PERIOD, RESOURCE_INSTANCE_BATTERY_WAKE_UP));
    MBED_ASSERT(getResourceValue(&config.gnssEnable,
                                 RESOURCE_NUMBER_GNSS_ENABLE));

    printfLog("IocCtrlConfig: new config is:\n");
    printfLog("  initWakeUpTickPeriod %f.\n", config.initWakeUpTickPeriod);
    printfLog("  initWakeUpCount %d.\n", config.initWakeUpCount);
    printfLog("  normalWakeUpTickPeriod %f.\n", config.normalWakeUpTickPeriod);
    printfLog("  normalWakeUpCount %d.\n", config.normalWakeUpCount);
    printfLog("  batteryWakeUpTickPeriod %f.\n", config.batteryWakeUpTickPeriod);
    printfLog("  GNSS enable %d.\n", config.gnssEnable);

    if (_setCallback) {
        _setCallback(&config);
    }
}

/**********************************************************************
 * AUDIO OBJECT
 **********************************************************************/

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const M2MObjectHelper::DefObject IocCtrlAudio::_defObject =
    {0, "32770", 5,
        -1, RESOURCE_NUMBER_STREAMING_ENABLED, "boolean", M2MResourceBase::BOOLEAN, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_DURATION, "duration", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_FIXED_GAIN, "level", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE, "mode", M2MResourceBase::INTEGER, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_AUDIO_SERVER_URL, "string", M2MResourceBase::STRING, false, M2MBase::GET_PUT_ALLOWED, NULL
    };

// Constructor.
IocCtrlAudio::IocCtrlAudio(bool debugOn,
                           Callback<void(Audio *)> setCallback,
                           Audio *initialValues)
             :M2MObjectHelper(debugOn, &_defObject,
                              value_updated_callback(this, &IocCtrlAudio::objectUpdated))
{
    _setCallback = setCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Set the initial values
    MBED_ASSERT(setResourceValue(initialValues->streamingEnabled, RESOURCE_NUMBER_STREAMING_ENABLED));
    MBED_ASSERT(setResourceValue(initialValues->duration, RESOURCE_NUMBER_DURATION));
    MBED_ASSERT(setResourceValue(initialValues->fixedGain,  RESOURCE_NUMBER_FIXED_GAIN));
    MBED_ASSERT(setResourceValue(initialValues->audioCommunicationsMode, RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE));
    MBED_ASSERT(setResourceValue(initialValues->audioServerUrl, RESOURCE_NUMBER_AUDIO_SERVER_URL));

    printfLog("IocCtrlAudio: object initialised.\n");
}

// Destructor.
IocCtrlAudio::~IocCtrlAudio()
{
}

// Callback when the object is updated by the server.
void IocCtrlAudio::objectUpdated(const char *resourceName)
{
    Audio audio;

    printfLog("IocCtrlAudio: resource \"%s\" has been updated.\n", resourceName);

    MBED_ASSERT(getResourceValue(&audio.streamingEnabled, RESOURCE_NUMBER_STREAMING_ENABLED));
    MBED_ASSERT(getResourceValue(&audio.duration, RESOURCE_NUMBER_DURATION));
    MBED_ASSERT(getResourceValue(&audio.fixedGain, RESOURCE_NUMBER_FIXED_GAIN));
    MBED_ASSERT(getResourceValue(&audio.audioCommunicationsMode, RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE));
    MBED_ASSERT(getResourceValue(&audio.audioServerUrl, RESOURCE_NUMBER_AUDIO_SERVER_URL));

    printfLog("IocCtrlAudio: new audio parameters are:\n");
    printfLog("  streamingEnabled %d.\n", audio.streamingEnabled);
    printfLog("  duration %f (-1 == no limit).\n", audio.duration);
    printfLog("  fixedGain %f (-1 == use automatic gain).\n", audio.fixedGain);
    printfLog("  audioCommunicationsMode %d (0 for UDP, 1 for TCP).\n", audio.audioCommunicationsMode);
    printfLog("  audioServerUrl \"%s\".\n", audio.audioServerUrl.c_str());

    if (_setCallback) {
        _setCallback(&audio);
    }
}

/**********************************************************************
 * DIAGNOSTICS OBJECT
 **********************************************************************/

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const M2MObjectHelper::DefObject IocCtrlDiagnostics::_defObject =
    {0, "32771", 6,
        -1, RESOURCE_NUMBER_UP_TIME, "on time", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        RESOURCE_INSTANCE_WORST_CASE_SEND_DURATION, RESOURCE_NUMBER_WORST_CASE_SEND_DURATION, "duration", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, NULL,
        RESOURCE_INSTANCE_AVERAGE_SEND_DURATION, RESOURCE_NUMBER_AVERAGE_SEND_DURATION, "duration", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_MIN_NUM_DATAGRAMS_FREE, "down counter", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_NUM_SEND_FAILURES, "up counter", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_PERCENT_SENDS_TOO_LONG, "percent", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL
    };

// Constructor.
IocCtrlDiagnostics::IocCtrlDiagnostics(bool debugOn,
                                       Callback<bool(Diagnostics *)> getCallback)
                   :M2MObjectHelper(debugOn, &_defObject)
{
    _getCallback = getCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Update the values held in the resources
    updateObservableResources();

    printfLog("IocCtrlDiagnostics: object initialised.\n");
}

// Destructor.
IocCtrlDiagnostics::~IocCtrlDiagnostics()
{
}

// Update the observable data for this object.
void IocCtrlDiagnostics::updateObservableResources()
{
    Diagnostics data;

    // Update the data
    if (_getCallback) {
        if (_getCallback(&data)) {
            // Set the values in the resources based on the new data
            MBED_ASSERT(setResourceValue(data.upTime, RESOURCE_NUMBER_UP_TIME));
            MBED_ASSERT(setResourceValue(data.worstCaseSendDuration,
                                         RESOURCE_NUMBER_WORST_CASE_SEND_DURATION,
                                         RESOURCE_INSTANCE_WORST_CASE_SEND_DURATION));
            MBED_ASSERT(setResourceValue(data.averageSendDuration,
                                         RESOURCE_NUMBER_AVERAGE_SEND_DURATION,
                                         RESOURCE_INSTANCE_AVERAGE_SEND_DURATION));
            MBED_ASSERT(setResourceValue(data.minNumDatagramsFree, RESOURCE_NUMBER_MIN_NUM_DATAGRAMS_FREE));
            MBED_ASSERT(setResourceValue(data.numSendFailures, RESOURCE_NUMBER_NUM_SEND_FAILURES));
            MBED_ASSERT(setResourceValue(data.percentageSendsTooLong, RESOURCE_NUMBER_PERCENT_SENDS_TOO_LONG));
        }
    }
}

// End of file
