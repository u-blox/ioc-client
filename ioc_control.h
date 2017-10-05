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

#include "mbed.h"

/** This file contains the classes that make up the control
 * plane of the IOC device, made up of LWM2M objects.
 *
 * Please see http://www.openmobilealliance.org/wp/OMNA/LwM2M/LwM2MRegistry.html for full descriptions.
 *
 *   - Power Control (urn:oma:lwm2m:ext:3312), to turn the device on or off
 *   - Location Object (urn:oma:lwm2m:oma:6),
 *   - Temperature Sensor (urn:oma:lwm2m:ext:3303),
 *   - Config as Private Object (urn:oma:lwm2m:x:32769) containing the following reusable resources:
 *       - InitWakeUpTick as Duration (urn:oma:lwm2m:ext:5524),
 *       - InitWakeUpCount as Counter (urn:oma:lwm2m:ext:5534),
 *       - NormalWakeUpTick as Duration (urn:oma:lwm2m:ext:5524),
 *       - NormalWakeUpCount as Counter (urn:oma:lwm2m:ext:5534),
 *       - BatteryWakeUpTick as Duration (urn:oma:lwm2m:ext:5524)
 *       - On/Off (urn:oma:lwm2m:ext:5850), used to switch GNSS on and off.
 *   - Audio as a Private Object (urn:oma:lwm2m:x:32770) containing the following reusable resources:
 *       - On/Off (urn:oma:lwm2m:ext:5850), used to switch audio on and off (which also disconnects socket if TCP)
 *       - Duration (urn:oma:lwm2m:ext:5524), used to send audio for a given time only,
 *       - Level (urn:oma:lwm2m:ext:5548), used to set a fixed gain if desired,
 *       - Mode (urn:oma:lwm2m:ext:5526), used to set UDP or TCP transmission,
 *       - Text (urn:oma:lwm2m:ext:5527), used to set the URL of the audio server.
 *   - Diagnostics as a Private Object (urn:oma:lwm2m:x:32771) containing the following reusable resources:
 *       - On Time (urn:oma:lwm2m:ext:5852),
 *       - Duration (urn:oma:lwm2m:ext:5524), used to contain the worst case time to perform a send,
 *       - Duration (urn:oma:lwm2m:ext:5524), used to contain the average time to perform a send,
 *       - Down Counter (urn:oma:lwm2m:ext:5542), used to contain the minimum number of datagrams free,
 *       - Up Counter (urn:oma:lwm2m:ext:5541), used to contain the number of send failures,
 *       - Percent (urn:oma:lwm2m:ext:3320), used to contain the percentage of sends that took longer than a block.
 */

/**********************************************************************
 * HELPER CLASS
 **********************************************************************/

/** Helper to make all the others simpler.
 */
class IocCtrlHelper {
public:

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
#   define FORMAT_METRES "%6f"

    /** Format for all values of speed.
     */
#   define FORMAT_SPEED "%6f"

    /** Format for a UNIX timestamp.
     */
#   define FORMAT_TIMESTAMP "%dl"

    /** Format for temperature.
     */
#   define FORMAT_TEMPERATURE "%3.1f"

    /** Structure to represent a resource.
     */
    typedef struct {
        const char name[MAX_OBJECT_RESOURCE_NAME_LENGTH];
        const char typeString[MAX_RESOURCE_TYPE_LENGTH];
        M2MResourceBase::ResourceType type;
        bool observable;
        M2MBase::Operation operation;
    } DefResource;

    /** Structure to represent an object.
     */
    typedef struct {
        char name[MAX_OBJECT_RESOURCE_NAME_LENGTH];
        int numResources;
        DefResource resources[MAX_NUM_RESOURCES];
    } DefObject;

    /** Constructor.
     */
    IocCtrlHelper(bool debugOn);

    /** Destructor.
     */
    ~IocCtrlHelper();

    /** Create an object.
     *
     * @param defObject pointer to the object definition.
     * @return          a pointer to the created object.
     */
    M2MObject *makeObject(const DefObject *defObject);

    /** Set to true to have debug prints.
     */
    bool _debugOn;

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
class IocCtrlPowerControl : public MbedCloudClientCallback, private IocCtrlHelper {
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
    virtual ~IocCtrlPowerControl();

    /** Return this object.
     *
     * @return pointer to this object.
     */
    M2MObject * getObject();

    /** MbedCloudClientCallback.
     *
     * @param base  information about the thing.
     * @param type  the type of the thing (object or resource).
     */
    virtual void value_updated(M2MBase *base, M2MBase::BaseType type);

protected:

    /** The resource number for the only resource
     * in this object: the on/off switch.
     */
#   define RESOURCE_NUMBER_POWER_SWITCH "5850"

    /** Definition of this object.
     */
    static const DefObject _defObject;

    /** Set to true to have debug prints.
     */
    bool _debugOn;

    /** Callback to set device on (true) or off (false).
     */
    Callback<void(bool)> _setCallback;

    /** The LWM2M object.
     */
    M2MObject *_object;
};

/**********************************************************************
 * LOCATION OBJECT
 **********************************************************************/

/** Report location.
 * Implementation is according to urn:oma:lwm2m:oma:6
 * with all optional resources included except velocity.
 */
class IocCtrlLocation : private IocCtrlHelper  {
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
    IocCtrlLocation(bool debugOn, Callback<bool(Location*)> getCallback);

    /** Destructor.
     */
    virtual ~IocCtrlLocation();

    /** Return this object.
     *
     * @return pointer to this object.
     */
    M2MObject * getObject();

protected:

    /** Update the reportable data.
     */
    void updateData();

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

    /** Set to true to have debug prints.
     */
    bool _debugOn;

    /** Callback to obtain location information.
     */
    Callback<bool(Location*)> _getCallback;

    /** The LWM2M object.
     */
    M2MObject *_object;
};

/**********************************************************************
 * TEMPERATURE OBJECT
 **********************************************************************/

/** Report temperature.
 * Implementation is according to urn:oma:lwm2m:ext:3303
 * with all optional resources included.
 */
class IocCtrlTemperature : private IocCtrlHelper  {
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
                       Callback<bool(Temperature*)> getCallback,
                       Callback<void(void)> resetMinMaxCallback,
                       int minRange,
                       int maxRange,
                       const char *units);

    /** Destructor.
     */
    virtual ~IocCtrlTemperature();

    /** Executable function.
     */
    void executeFunction(void * parameter);

    /** Return this object.
     *
     * @return pointer to this object.
     */
    M2MObject * getObject();

protected:

    /** Update the reportable data.
     */
    void updateData();

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

    /** Set to true to have debug prints.
     */
    bool _debugOn;

    /** Callback to obtain temperature information.
     */
    Callback<bool(Temperature*)> _getCallback;

    /** Callback to reset the min/max readings.
     */
    Callback<void(void)> _resetMinMaxCallback;

    /** The LWM2M object.
     */
    M2MObject *_object;
};

#endif // _IOC_CONTROL_

// End of file
