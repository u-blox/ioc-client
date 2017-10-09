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

#ifndef _IOC_CONTROL_
#define _IOC_CONTROL_

/** This file contains the classes that make up the control
 * plane of the IOC device, consisting of LWM2M objects.
 *
 * Please see http://www.openmobilealliance.org/wp/OMNA/LwM2M/LwM2MRegistry.html
 * and https://github.com/IPSO-Alliance/pub/tree/master/reg for reference.
 */

/**********************************************************************
 * BASE CLASS
 **********************************************************************/

/** Base class to make all the other classes simpler.
 */
class IocCtrlBase {
public:

    /** Destructor.
     */
    virtual ~IocCtrlBase();

    /** Update this objects' resources.
     * Derived classes should implement
     * this function (and make a call to
     * setCallback()) if they have observable
     * resources which need to be updated from
     * somewhere (e.g. the temperature has
     * changed).
     */
    virtual void updateObservableResources();

    /** Return this object.
     *
     * @return pointer to this object.
     */
    M2MObject *getObject();

protected:

    /** The maximum length of an object
     * name or resource name.
     */
#   define MAX_OBJECT_RESOURCE_NAME_LENGTH 8

    /** The maximum length of the string
     * representation of a resource type.
     */
#   define MAX_RESOURCE_TYPE_LENGTH 20

    /** The maximum number of resources
     * an object can have.
     */
#   define MAX_NUM_RESOURCES 8

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

    /** Format for all values in seconds.
     */
#   define FORMAT_SECONDS "%6.3f"

    /** Format for gain.
     */
#   define FORMAT_GAIN "%6.1f"

    /** Format if no format is defined.
     */
#   define FORMAT_DEFAULT "%6.6f"

    /** Structure to represent a resource.
     */
    typedef struct {
        int instance; ///< use -1 if there is only a single instance
        const char name[MAX_OBJECT_RESOURCE_NAME_LENGTH]; ///< the name, e.g. "3303"
        const char typeString[MAX_RESOURCE_TYPE_LENGTH]; ///< the type. e.g. "on/off"
        M2MResourceBase::ResourceType type;
        bool observable; ///< true if the object is observable, otherwise false
        M2MBase::Operation operation;
        const char * format; ///< format string, required if type is FLOAT
    } DefResource;

    /** Structure to represent an object.
     */
    typedef struct {
        char name[MAX_OBJECT_RESOURCE_NAME_LENGTH];
        int numResources;
        DefResource resources[MAX_NUM_RESOURCES];
    } DefObject;

    /** Constructor.
     *
     * @param debugOn                true to switch debug prints on,
     *                               otherwise false.
     * @param defObject              the definition of the LWM2M object.
     * @param value_updated_callback callback to be called if any
     *                               resource in this object is written
     *                               to; the callback will receive the
     *                               resource number as a string so that
     *                               if finer-grained action can be
     *                               performed if required.
     */
    IocCtrlBase(bool debugOn, const DefObject *defObject, value_updated_callback valueUpdatedCallback = NULL);

    /** Create an object as defined in the defObject
     * structure passed in in the constructor. This
     * must be called before any of the other
     * functions can be called.
     *
     * @return  true if successful, otherwise false.
     */
    bool makeObject();

    /** Set the execute callback (for an executable resource).
     *
     * @param callback the callback.
     * @return         true if successful, otherwise false.
     */
    bool setExecuteCallback(execute_callback callback,
                            const char * resourceNumber);

    /** Set the value of a given resource in an object.
     *
     * @param value            pointer to the value of the
     *                         resource to set. If the
     *                         resource is of type STRING,
     *                         value should be a pointer to
     *                         String, if the resource
     *                         is of type INTEGER, value
     *                         should be a pointer to int64_t,
     *                         if the resource is of type FLOAT,
     *                         value should be a pointer to
     *                         float and if the resource is of
     *                         type BOOLEAN the value should
     *                         be a pointer to bool. YOU HAVE TO
     *                         GET THIS RIGHT, THERE IS NO
     *                         WAY FOR THE COMPILER TO CHECK.
     * @param resourceNumber   the number of the resource whose
     *                         value is to be set.
     * @param resourceInstance the resource instance if there
     *                         is more than one.
     * @return                 true if successful, otherwise
     *                         false.
     */
    bool setResourceValue(const void * value,
                          const char * resourceNumber,
                          int resourceInstance = -1);

    /** Get the value of a given resource in an object.
     *
     * @param value            pointer to a place to put
     *                         the resource value.  If
     *                         the resource is of type STRING,
     *                         value should be a pointer to
     *                         String (NOT char *), if the
     *                         resource is of type INTEGER,
     *                         value should be a pointer to
     *                         int64_t, if the resource is of
     *                         type FLOAT, value should be a
     *                         pointer to float and if the
     *                         resource is of type BOOLEAN
     *                         the value should be a pointer
     *                         to bool. YOU HAVE TO GET THIS
     *                         RIGHT, THERE IS NO WAY FOR THE
     *                         COMPILER TO CHECK.
     * @param resourceNumber   the number of the resource whose
     *                         value is to be set.
     * @param resourceInstance the resource instance if there
     *                         is more than one.
     * @return                 true if successful, otherwise
     *                         false.
     */
    bool getResourceValue(void * value,
                          const char * resourceNumber,
                          int resourceInstance = -1);

    /** True if debug is on, otherwise false.
     */
    bool _debugOn;

    /** The definition for this object.
     */
    const DefObject *_defObject;

    /** The LWM2M object.
     */
    M2MObject *_object;

    /** The value updated callback set to NULL where there
     * is none.  Required if the object includes a writable
     * object.
     */
    value_updated_callback _valueUpdatedCallback;

    /** Need this for the types.
     */
    friend class M2MBase;
};

/**********************************************************************
 * POWER CONTROL OBJECT
 **********************************************************************/

/** Control the power state of the device.
 * Implementation is according to urn:oma:lwm2m:ext:3312
 * with only the mandatory resource (on/off).
 */
class IocCtrlPowerControl : public IocCtrlBase {
public:

    /** Constructor.
     *
     * @param debugOn      true if you want debug prints, otherwise false.
     * @param setCallback  callback to switch the device on (true) or off (false).
     * @param initialValue the initial state of the switch.
     */
    IocCtrlPowerControl(bool debugOn,
                        Callback<void(bool)> setCallback,
                        bool initialValue);

    /** Destructor.
     */
    ~IocCtrlPowerControl();

    /** Callback for when the object is updated.
     *
     * @param thing information about the thing.
     */
    void objectUpdated(const char * thing);

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
class IocCtrlLocation : public IocCtrlBase  {
public:

    /** Location structure.
     */
    typedef struct {
        float latitudeDegrees;
        float longitudeDegrees;
        float radiusMetres;
        float altitudeMetres;
        float speedMPS;
        int timestampUnix;
    } Location;

    /** Constructor.
     *
     * @param debugOn      true if you want debug prints, otherwise false.
     * @param getCallback  callback to get location information
     */
    IocCtrlLocation(bool debugOn, Callback<bool(Location *)> getCallback);

    /** Destructor.
     */
    ~IocCtrlLocation();

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
class IocCtrlTemperature : public IocCtrlBase  {
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
     * @param debugOn              true if you want debug prints, otherwise false.
     * @param getCallback          callback to get location information.
     * @param resetMinMaxCallback  callback to reset the min/max readings.
     * @param minRange             the minimum temperature the sensor can measure.
     * @param units                a string representing the units of temperature used.
     */
    IocCtrlTemperature(bool debugOn,
                       Callback<bool(Temperature *)> getCallback,
                       Callback<void(void)> resetMinMaxCallback,
                       float minRange,
                       float maxRange,
                       String units);

    /** Destructor.
     */
    ~IocCtrlTemperature();

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
class IocCtrlConfig : public IocCtrlBase {
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
     * @param debugOn       true if you want debug prints, otherwise false.
     * @param setCallback   callback to set the configuration values.
     * @param initialValues the initial state of the configuration values.
     */
    IocCtrlConfig(bool debugOn,
                  Callback<void(Config *)> setCallback,
                  Config *initialValues);

    /** Destructor.
     */
    ~IocCtrlConfig();

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
class IocCtrlAudio : public IocCtrlBase {
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
     * @param debugOn       true if you want debug prints, otherwise false.
     * @param setCallback   callback to set the audio parameter values.
     * @param initialValues the initial state of the audio parameter values.
     */
    IocCtrlAudio(bool debugOn,
                 Callback<void(Audio *)> setCallback,
                 Audio *initialValues);

    /** Destructor.
     */
    ~IocCtrlAudio();

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
     * a Text resource
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
class IocCtrlDiagnostics : public IocCtrlBase {
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
     * @param debugOn     true if you want debug prints, otherwise false.
     * @param getCallback callback to get diagnostics information.
     */
    IocCtrlDiagnostics(bool debugOn, Callback<bool(Diagnostics *)> getCallback);

    /** Destructor.
     */
    ~IocCtrlDiagnostics();

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

#endif // _IOC_CONTROL_

// End of file
