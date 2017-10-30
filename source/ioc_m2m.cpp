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
#include "ioc_m2m.h"

#define printfLog(format, ...) debug_if(_debugOn, format, ## __VA_ARGS__)

/**********************************************************************
 * POWER CONTROL OBJECT
 **********************************************************************/

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const M2MObjectHelper::DefObject IocM2mPowerControl::_defObject =
    {0, "3312", 1,
        -1, RESOURCE_NUMBER_POWER_SWITCH, "on/off", M2MResourceBase::BOOLEAN, false, M2MBase::GET_PUT_ALLOWED, NULL
    };

// Constructor.
IocM2mPowerControl::IocM2mPowerControl(Callback<void(bool)> setCallback,
                                        bool initialValue,
                                        bool debugOn)
                   :M2MObjectHelper(&_defObject,
                                    value_updated_callback(this, &IocM2mPowerControl::objectUpdated),
                                    NULL,
                                    debugOn)
{
    _setCallback = setCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Set the initial value
    MBED_ASSERT(setResourceValue(initialValue, RESOURCE_NUMBER_POWER_SWITCH));

    printfLog("IocM2mPowerControl: object initialised.\n");
}

// Destructor.
IocM2mPowerControl::~IocM2mPowerControl()
{
}

// Callback when the object is updated.
void IocM2mPowerControl::objectUpdated(const char *resourceName)
{
    bool onNotOff;

    printfLog("IocM2mPowerControl: resource \"%s\" has been updated.\n", resourceName);

    MBED_ASSERT(getResourceValue(&onNotOff, RESOURCE_NUMBER_POWER_SWITCH));

    printfLog("IocM2mPowerControl: new value is %d.\n", onNotOff);

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
const M2MObjectHelper::DefObject IocM2mLocation::_defObject =
    {0, "6", 6,
        -1, RESOURCE_NUMBER_LATITUDE, "latitude", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_DEGREES,
        -1, RESOURCE_NUMBER_LONGITUDE, "longitude", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_DEGREES,
        -1, RESOURCE_NUMBER_RADIUS, "radius", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_METRES,
        -1, RESOURCE_NUMBER_ALTITUDE, "altitude", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_METRES,
        -1, RESOURCE_NUMBER_SPEED, "speed", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_SPEED,
        -1, RESOURCE_NUMBER_TIMESTAMP, "timestamp", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
    };

// Constructor.
IocM2mLocation::IocM2mLocation(Callback<bool(Location *)> getCallback,
                               bool debugOn)
               :M2MObjectHelper(&_defObject, NULL, NULL, debugOn)
{
    _getCallback = getCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Update the values held in the resources
    updateObservableResources();

    printfLog("IocM2mLocation: object initialised.\n");
}

// Destructor.
IocM2mLocation::~IocM2mLocation()
{
}

// Update the observable data for this object.
void IocM2mLocation::updateObservableResources()
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

    printfLog("IocM2mTemperature: object initialised.\n");
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

/** Executable function.
 */
void IocM2mTemperature::executeFunction (void *parameter)
{
    printfLog("IocM2mTemperature: reset min/max received.\n");

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

    printfLog("IocM2mConfig: object initialised.\n");
}

// Destructor.
IocM2mConfig::~IocM2mConfig()
{
}

// Callback when the object is updated by the server.
void IocM2mConfig::objectUpdated(const char *resourceName)
{
    Config config;

    printfLog("IocM2mConfig: resource \"%s\" has been updated.\n", resourceName);

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

    printfLog("IocM2mConfig: new config is:\n");
    printfLog("  initWakeUpTickCounterPeriod %f.\n", config.initWakeUpTickCounterPeriod);
    printfLog("  initWakeUpTickCounterModulo %d.\n", config.initWakeUpTickCounterModulo);
    printfLog("  readyWakeUpTickCounterPeriod1 %f.\n", config.readyWakeUpTickCounterPeriod1);
    printfLog("  readyWakeUpTickCounterPeriod2 %d.\n", config.readyWakeUpTickCounterPeriod2);
    printfLog("  readyWakeUpTickCounterModulo %f.\n", config.readyWakeUpTickCounterModulo);
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
const M2MObjectHelper::DefObject IocM2mAudio::_defObject =
    {0, "32770", 5,
        -1, RESOURCE_NUMBER_STREAMING_ENABLED, "boolean", M2MResourceBase::BOOLEAN, true, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_DURATION, "duration", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_FIXED_GAIN, "level", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE, "mode", M2MResourceBase::INTEGER, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_AUDIO_SERVER_URL, "string", M2MResourceBase::STRING, false, M2MBase::GET_PUT_ALLOWED, NULL
    };

// Constructor.
IocM2mAudio::IocM2mAudio(Callback<void(const Audio *)> setCallback,
                         Callback<bool(bool *)> getStreamingEnabledCallback,
                         Audio *initialValues,
                         bool debugOn)
            :M2MObjectHelper(&_defObject,
                             value_updated_callback(this, &IocM2mAudio::objectUpdated),
                             NULL,
                             debugOn)
{
    _setCallback = setCallback;
    _getStreamingEnabledCallback = getStreamingEnabledCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Set the initial values
    MBED_ASSERT(setResourceValue(initialValues->streamingEnabled, RESOURCE_NUMBER_STREAMING_ENABLED));
    MBED_ASSERT(setResourceValue(initialValues->duration, RESOURCE_NUMBER_DURATION));
    MBED_ASSERT(setResourceValue(initialValues->fixedGain,  RESOURCE_NUMBER_FIXED_GAIN));
    MBED_ASSERT(setResourceValue(initialValues->audioCommunicationsMode, RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE));
    MBED_ASSERT(setResourceValue(initialValues->audioServerUrl, RESOURCE_NUMBER_AUDIO_SERVER_URL));

    // Update the observable resources
    updateObservableResources();

    printfLog("IocM2mAudio: object initialised.\n");
}

// Destructor.
IocM2mAudio::~IocM2mAudio()
{
}

// Callback when the object is updated by the server.
void IocM2mAudio::objectUpdated(const char *resourceName)
{
    Audio audio;

    printfLog("IocM2mAudio: resource \"%s\" has been updated.\n", resourceName);

    MBED_ASSERT(getResourceValue(&audio.streamingEnabled, RESOURCE_NUMBER_STREAMING_ENABLED));
    MBED_ASSERT(getResourceValue(&audio.duration, RESOURCE_NUMBER_DURATION));
    MBED_ASSERT(getResourceValue(&audio.fixedGain, RESOURCE_NUMBER_FIXED_GAIN));
    MBED_ASSERT(getResourceValue(&audio.audioCommunicationsMode, RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE));
    MBED_ASSERT(getResourceValue(&audio.audioServerUrl, RESOURCE_NUMBER_AUDIO_SERVER_URL));

    printfLog("IocM2mAudio: new audio parameters are:\n");
    printfLog("  streamingEnabled %d.\n", audio.streamingEnabled);
    printfLog("  duration %f (-1 == no limit).\n", audio.duration);
    printfLog("  fixedGain %f (-1 == use automatic gain).\n", audio.fixedGain);
    printfLog("  audioCommunicationsMode %d (0 for UDP, 1 for TCP).\n", audio.audioCommunicationsMode);
    printfLog("  audioServerUrl \"%s\".\n", audio.audioServerUrl.c_str());

    if (_setCallback) {
        _setCallback(&audio);
    }
}

// Update the observable data for this object.
void IocM2mAudio::updateObservableResources()
{
    bool streamingEnabled;

    // Update the data
    if (_getStreamingEnabledCallback) {
        if (_getStreamingEnabledCallback(&streamingEnabled)) {
            // Set the values in the resources based on the new data
            MBED_ASSERT(setResourceValue(streamingEnabled, RESOURCE_NUMBER_STREAMING_ENABLED));
        }
    }
}

/**********************************************************************
 * DIAGNOSTICS OBJECT
 **********************************************************************/

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const M2MObjectHelper::DefObject IocM2mDiagnostics::_defObject =
    {0, "32771", 7,
        -1, RESOURCE_NUMBER_UP_TIME, "on time", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_RESET_REASON, "reset reason", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        RESOURCE_INSTANCE_WORST_CASE_SEND_DURATION, RESOURCE_NUMBER_WORST_CASE_SEND_DURATION, "duration", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, NULL,
        RESOURCE_INSTANCE_AVERAGE_SEND_DURATION, RESOURCE_NUMBER_AVERAGE_SEND_DURATION, "duration", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_MIN_NUM_DATAGRAMS_FREE, "down counter", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_NUM_SEND_FAILURES, "up counter", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_PERCENT_SENDS_TOO_LONG, "percent", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL
    };

// Constructor.
IocM2mDiagnostics::IocM2mDiagnostics(Callback<bool(Diagnostics *)> getCallback,
                                     bool debugOn)
                  :M2MObjectHelper(&_defObject, NULL, NULL, debugOn)
{
    _getCallback = getCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Update the values held in the resources
    updateObservableResources();

    printfLog("IocM2mDiagnostics: object initialised.\n");
}

// Destructor.
IocM2mDiagnostics::~IocM2mDiagnostics()
{
}

// Update the observable data for this object.
void IocM2mDiagnostics::updateObservableResources()
{
    Diagnostics data;

    // Update the data
    if (_getCallback) {
        if (_getCallback(&data)) {
            // Set the values in the resources based on the new data
            MBED_ASSERT(setResourceValue(data.upTime, RESOURCE_NUMBER_UP_TIME));
            MBED_ASSERT(setResourceValue(data.resetReason, RESOURCE_NUMBER_RESET_REASON));
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
