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

#ifndef _IOC_M2M_
#define _IOC_M2M_

/** This file contains the classes that make up the control
 * plane of the IOC device, consisting of LWM2M objects.
 *
 * Please see http://www.openmobilealliance.org/wp/OMNA/LwM2M/LwM2MRegistry.html
 * and https://github.com/IPSO-Alliance/pub/tree/master/reg for reference.
 */

/**********************************************************************
 * POWER CONTROL OBJECT
 **********************************************************************/

/** Control the power state of the device.
 * Implementation is according to urn:oma:lwm2m:ext:3312
 * with only the mandatory resource (on/off).
 */
class IocM2mPowerControl : public M2MObjectHelper {
public:

    /** Constructor.
     *
     * @param setCallback  callback to switch the device on (true) or off (false).
     * @param initialValue the initial state of the switch.
     * @param debugOn      true if you want debug prints, otherwise false.
     */
    IocM2mPowerControl(Callback<void(bool)> setCallback,
                       bool initialValue,
                       bool debugOn = false);

    /** Destructor.
     */
    ~IocM2mPowerControl();

    /** Callback for when the object is updated.
     *
     * @param resourceName the resource name.
     */
    void objectUpdated(const char * resourceName);

protected:

    /** The resource number for the only resource
     * in this object: the on/off switch.
     */
#   define RESOURCE_NUMBER_POWER_SWITCH "5850"

    /** Definition of this object.
     */
    static const DefObject _defObject;

    /** Callback to set device on (true) or off (false).
     */
    Callback<void(bool)> _setCallback;
};

/**********************************************************************
 * LOCATION OBJECT
 **********************************************************************/

/** Report location.
 * Implementation is according to urn:oma:lwm2m:oma:6
 * with all optional resources included except velocity.
 */
class IocM2mLocation : public M2MObjectHelper  {
public:

    /** Format for all values in degrees.
     */
#   define FORMAT_DEGREES "%6.6f"

    /** Format for all values in metres.
     */
#   define FORMAT_METRES "%6.0f"

    /** Format for all values of speed.
     */
#   define FORMAT_SPEED "%6.0f"

    /** Format for temperature.
     */
#   define FORMAT_TEMPERATURE "%3.1f"

    /** Location structure.
     */
    typedef struct {
        float latitudeDegrees;
        float longitudeDegrees;
        float radiusMetres;
        float altitudeMetres;
        float speedMPS;
        int64_t timestampUnix;
    } Location;

    /** Constructor.
     *
     * @param getCallback  callback to get location information
     * @param debugOn      true if you want debug prints, otherwise false.
     */
    IocM2mLocation(Callback<bool(Location *)> getCallback,
                   bool debugOn = false);

    /** Destructor.
     */
    ~IocM2mLocation();

    /** Update the observable resources (using getCallback()).
     */
    void updateObservableResources();

protected:

    /** The resource number for latitude.
     */
#   define RESOURCE_NUMBER_LATITUDE "0"

    /** The resource number for longitude.
     */
#   define RESOURCE_NUMBER_LONGITUDE "1"

    /** The resource number for radius.
     */
#   define RESOURCE_NUMBER_RADIUS "3"

    /** The resource number for altitude.
     */
#   define RESOURCE_NUMBER_ALTITUDE "2"

    /** The resource number for speed.
     */
#   define RESOURCE_NUMBER_SPEED "6"

    /** The resource number for timestamp.
     */
#   define RESOURCE_NUMBER_TIMESTAMP "5"

    /** Definition of this object.
     */
    static const DefObject _defObject;

    /** Callback to obtain location information.
     */
    Callback<bool(Location *)> _getCallback;
};

/**********************************************************************
 * TEMPERATURE OBJECT
 **********************************************************************/

/** Report temperature.
 * Implementation is according to urn:oma:lwm2m:ext:3303
 * with all optional resources included.
 */
class IocM2mTemperature : public M2MObjectHelper  {
public:

    /** Temperature structure.
     */
    typedef struct {
        float temperature;
        float minTemperature;
        float maxTemperature;
        float resetMinMax;
    } Temperature;

    /** Constructor.
     *
     * @param getCallback          callback to get location information.
     * @param resetMinMaxCallback  callback to reset the min/max readings.
     * @param minRange             the minimum temperature the sensor can measure.
     * @param units                a string representing the units of temperature used.
     * @param debugOn              true if you want debug prints, otherwise false.
     */
    IocM2mTemperature(Callback<bool(Temperature *)> getCallback,
                      Callback<void(void)> resetMinMaxCallback,
                      float minRange,
                      float maxRange,
                      String units,
                      bool debugOn = false);

    /** Destructor.
     */
    ~IocM2mTemperature();

    /** Executable function.
     */
    void executeFunction(void *parameter);

    /** Update the observable resources (using getCallback()).
     */
    void updateObservableResources();

protected:

    /** The resource number for temperature.
     */
#   define RESOURCE_NUMBER_TEMPERATURE "5700"

    /** The resource number for minimum temperature.
     */
#   define RESOURCE_NUMBER_MIN_TEMPERATURE "5601"

    /** The resource number for maximum temperature.
     */
#   define RESOURCE_NUMBER_MAX_TEMPERATURE "5602"

    /** The resource number for the executable
     * resource that resets the min/max.
     */
#   define RESOURCE_NUMBER_RESET_MIN_MAX "5605"

    /** The resource number for the minimum measurable
     * temperature value.
     */
#   define RESOURCE_NUMBER_MIN_RANGE "5603"

    /** The resource number for the maximum measurable
     * temperature value.
     */
#   define RESOURCE_NUMBER_MAX_RANGE "5604"

    /** The resource number for theh units of temperature.
     */
#   define RESOURCE_NUMBER_UNITS "5701"

    /** Definition of this object.
     */
    static const DefObject _defObject;

    /** Callback to obtain temperature information.
     */
    Callback<bool(Temperature *)> _getCallback;

    /** Callback to reset the min/max readings.
     */
    Callback<void(void)> _resetMinMaxCallback;
};

/**********************************************************************
 * CONFIG OBJECT
 **********************************************************************/

/** Configuration items for the device.
 * Implementation is as a custom object, I have chosen
 * ID urn:oma:lwm2m:x:32769, with writable resources.
 */
class IocM2mConfig : public M2MObjectHelper {
public:

    /** Configuration values
     * Note: these values need to follow
     * the types used in the cloud client,
     * so int must be int64_t and durations
     * are given as floats (of seconds).
     */
    typedef struct {
        float initWakeUpTickPeriod;
        int64_t initWakeUpCount;
        float normalWakeUpTickPeriod;
        int64_t normalWakeUpCount;
        float batteryWakeUpTickPeriod;
        bool gnssEnable;
    } Config;

    /** Constructor.
     *
     * @param setCallback   callback to set the configuration values.
     * @param initialValues the initial state of the configuration values.
     * @param debugOn       true if you want debug prints, otherwise false.
     */
    IocM2mConfig(Callback<void(Config *)> setCallback,
                 Config *initialValues,
                 bool debugOn = false);

    /** Destructor.
     */
    ~IocM2mConfig();

    /** Callback for when the object is updated, which
     * will set the local variables using setCallback().
     *
     * @param resourceName the resource that was updated.
     */
    void objectUpdated(const char *resourceName);

protected:

    /** The resource instance for initWakeUp*.
     */
#   define RESOURCE_INSTANCE_INIT_WAKE_UP 0

    /** The resource number for initWakeUpTickPeriod,
     * a Duration resource.
     */
#   define RESOURCE_NUMBER_INIT_WAKE_UP_TICK_PERIOD "5524"

    /** The resource number for initWakeUpCount,
     * a Counter resource.
     */
#   define RESOURCE_NUMBER_INIT_WAKE_UP_COUNT "5534"

    /** The resource instance for normalWakeUp*.
     */
#   define RESOURCE_INSTANCE_NORMAL_WAKE_UP 1

    /** The resource number for normalWakeUpTickPeriod,
     * a Duration resource.
     */
#   define RESOURCE_NUMBER_NORMAL_WAKE_UP_TICK_PERIOD "5524"

    /** The resource number for normalWakeUpCount,
     * a Counter resource.
     */
#   define RESOURCE_NUMBER_NORMAL_WAKE_UP_COUNT "5534"

    /** The resource instance for batteryWakeUp*.
     */
#   define RESOURCE_INSTANCE_BATTERY_WAKE_UP 2

    /** The resource number for batteryWakeUpTickPeriod,
     * a Duration resource.
     */
#   define RESOURCE_NUMBER_BATTERY_WAKE_UP_TICK_PERIOD "5524"

    /** The resource number for gnssEnable, a Boolean
     * resource.
     */
#   define RESOURCE_NUMBER_GNSS_ENABLE "5850"

    /** Definition of this object.
     */
    static const DefObject _defObject;

    /** Callback to set configuration values.
     */
    Callback<void(Config *)> _setCallback;
};

/**********************************************************************
 * AUDIO OBJECT
 **********************************************************************/

/** Control for the audio stream.
 * Implementation is as a custom object, I have chosen
 * ID urn:oma:lwm2m:x:32770, with writable resources.
 */
class IocM2mAudio : public M2MObjectHelper {
public:

    /** The audio communication mode options.
     */
    typedef enum {
        AUDIO_COMMUNICATIONS_MODE_UDP,
        AUDIO_COMMUNICATIONS_MODE_TCP,
        MAX_NUM_AUDIO_COMMUNICATIONS_MODES
    } AudioCommunicationsMode;

    /** The audio control parameters (with
     * types that match the LWM2M types).
     */
    typedef struct {
        bool streamingEnabled;
        float duration;  ///< -1 = no limit.
        float fixedGain; ///< -1 = use automatic gain.
        int64_t audioCommunicationsMode; ///< valid values are
                                         /// those from the
                                         /// AudioCommunicationsMode
                                         /// enum (this has to be
                                         /// an int64_t as it is an
                                         /// integer type).
        String audioServerUrl;
    } Audio;

    /** Constructor.
     *
     * @param setCallback   callback to set the audio parameter values.
     * @param initialValues the initial state of the audio parameter values.
     * @param debugOn       true if you want debug prints, otherwise false.
     */
    IocM2mAudio(Callback<void(Audio *)> setCallback,
                Audio *initialValues,
                bool debugOn = false);

    /** Destructor.
     */
    ~IocM2mAudio();

    /** Callback for when the object is updated, which
     * will set the local variables using setCallback().
     *
     * @param resourceName the resource that was updated.
     */
    void objectUpdated(const char *resourceName);

protected:

    /** The resource number for streamingEnabled,
     * a Boolean resource.
     */
#   define RESOURCE_NUMBER_STREAMING_ENABLED "5850"

    /** The resource number for duration,
     * a Duration resource.
     */
#   define RESOURCE_NUMBER_DURATION "5524"

    /** The resource number for fixedGain,
     *  a Level resource.
     */
#   define RESOURCE_NUMBER_FIXED_GAIN "5548"

    /** The resource number for audioCommunicationsMode,
     * a Mode resource.
     */
#   define RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE "5526"

    /** The resource number for audioServerUrl,
     * a Text resource.
     */
#   define RESOURCE_NUMBER_AUDIO_SERVER_URL "5527"

    /** Definition of this object.
     */
    static const DefObject _defObject;

    /** Callback to set audio parmeters.
     */
    Callback<void(Audio *)> _setCallback;
};

/**********************************************************************
 * DIAGNOSTICS OBJECT
 **********************************************************************/

/** Diagnostics reporting.
 * Implementation is as a custom object, I have chosen ID urn:oma:lwm2m:x:32771.
 */
class IocM2mDiagnostics : public M2MObjectHelper {
public:

    /** The diagnostics information (with types that match
     * the LWM2M types).
     */
    typedef struct {
        int64_t upTime;
        float worstCaseSendDuration;
        float averageSendDuration;
        int64_t minNumDatagramsFree;
        int64_t numSendFailures;
        int64_t percentageSendsTooLong;
    } Diagnostics;

    /** Constructor.
     *
     * @param getCallback callback to get diagnostics information.
     * @param debugOn     true if you want debug prints, otherwise false.
     */
    IocM2mDiagnostics(Callback<bool(Diagnostics *)> getCallback,
                      bool debugOn = false);

    /** Destructor.
     */
    ~IocM2mDiagnostics();

    /** Update the observable resources (using getCallback()).
     */
    void updateObservableResources();

protected:

    /** The resource number for upTime, an On Time
     * resource.
     */
#   define RESOURCE_NUMBER_UP_TIME "5852"

    /** The resource number for worstCaseSendDuration,
     * a Duration resource.
     */
#   define RESOURCE_NUMBER_WORST_CASE_SEND_DURATION "5524"

    /** The resource instance for worstCaseSendDuration.
     */
#   define RESOURCE_INSTANCE_WORST_CASE_SEND_DURATION 0

    /** The resource number for averageDuration, a
     * Duration resource.
     */
#   define RESOURCE_NUMBER_AVERAGE_SEND_DURATION "5524"

    /** The resource instance for averageSendDuration.
     */
#   define RESOURCE_INSTANCE_AVERAGE_SEND_DURATION  1

    /** The resource number for minDatagramsFree, a
     * Down Counter resource.
     */
#   define RESOURCE_NUMBER_MIN_NUM_DATAGRAMS_FREE "5542"

    /** The resource number for numSendFailures, an
     * Up Counter resource.
     */
#   define RESOURCE_NUMBER_NUM_SEND_FAILURES "5541"

    /** The resource number for percentageSendsTooLong,
     * a Percent resource.
     */
#   define RESOURCE_NUMBER_PERCENT_SENDS_TOO_LONG "3320"

    /** Definition of this object.
     */
    static const DefObject _defObject;

    /** Callback to get diagnostics values.
     */
    Callback<bool(Diagnostics *)> _getCallback;
};

#endif // _IOC_M2M_

// End of file
