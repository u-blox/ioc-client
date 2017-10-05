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
#include "mbed-cloud-client/MbedCloudClient.h"
#include "ioc_control.h"

#ifdef MBED_CLOUD_CLIENT_SUPPORT_UPDATE
#include "update_ui_example.h"
#endif

#define printfLog(format, ...) debug_if(_debugOn, format, ## __VA_ARGS__)

/**********************************************************************
 * HELPER CLASS
 **********************************************************************/

// Constructor.
IocCtrlHelper::IocCtrlHelper(bool debugOn)
{
    _debugOn = debugOn;
}

// Destructor.
IocCtrlHelper::~IocCtrlHelper()
{
}

// Create an object.
M2MObject *IocCtrlHelper::makeObject(const DefObject *defObject)
{
    M2MObject * object;
    M2MObjectInstance *objectInstance;
    M2MResource *resource;
    const DefResource *defResource;

    printfLog("IocCtrlHelper: making object \"%s\" with %d resource(s).\n",
              defObject->name, defObject->numResources);

    // Create the object according to the definition
    object = M2MInterfaceFactory::create_object(defObject->name);
    objectInstance = object->create_object_instance();

    // Create the resources according to the definition
    for (int x = 0; x < defObject->numResources; x++) {
        defResource = &(defObject->resources[x]);
        resource = objectInstance->create_dynamic_resource((const char *) defResource->name,
                                                           (const char *) defResource->typeString,
                                                           defResource->type,
                                                           defResource->observable);
        resource->set_operation(defResource->operation);
    }

    return object;
}

/**********************************************************************
 * POWER CONTROL OBJECT
 **********************************************************************/

/** The definition of the following object (C++ pre C11 won't
 * let this const initialisation be done in the class definition).
 */
const IocCtrlHelper::DefObject IocCtrlPowerControl::_defObject =
    {"3312", 1,
        RESOURCE_NUMBER_POWER_SWITCH, "on/off", M2MResourceBase::BOOLEAN, false, M2MBase::GET_PUT_ALLOWED
    };

// Constructor.
IocCtrlPowerControl::IocCtrlPowerControl(bool debugOn,
                                         Callback<void(bool)> setCallback,
                                         bool initialValue)
                    :IocCtrlHelper(debugOn)
{
    M2MObjectInstance *objectInstance;
    M2MResource *resource;

    _debugOn = debugOn;
    _setCallback = setCallback;

    // Make the object and its resources
    _object = makeObject(&_defObject);
    objectInstance = _object->object_instance();;

    resource = objectInstance->resource(RESOURCE_NUMBER_POWER_SWITCH);
    resource->set_value((int64_t) initialValue);

    printfLog("IocCtrlPowerControl: object initialised.\n");
}

// Destructor.
IocCtrlPowerControl::~IocCtrlPowerControl()
{
    // TODO: find out why clearing up crashes things
    //delete _object;
}

// MbedCloudClientCallback.
void IocCtrlPowerControl::value_updated(M2MBase *base, M2MBase::BaseType type)
{
    M2MObjectInstance *objectInstance = _object->object_instance();;
    M2MResource *resource = objectInstance->resource(RESOURCE_NUMBER_POWER_SWITCH);
    bool value = (bool) resource->get_value_int();

    printfLog("IocCtrlPowerControl: PUT request received.\n");
    printfLog("IocCtrlPowerControl: name: \"%s\", path: \"%s\", type: %d (0 for object, 1 for resource), type: \"%s\".\n",
              base->name(), base->uri_path(), type, base->resource_type());

    printfLog("IocCtrlPowerControl: new value is %d.\n", value);

    if (_setCallback) {
        _setCallback(value);
    }
}

// Return this object.
M2MObject * IocCtrlPowerControl::getObject()
{
    return _object;
}

/**********************************************************************
 * LOCATION OBJECT
 **********************************************************************/

/** The definition of the following object (C++ pre C11 won't
 * let this const initialisation be done in the class definition).
 */
const IocCtrlHelper::DefObject IocCtrlLocation::_defObject =
    {"6", 6,
        RESOURCE_NUMBER_LATITUDE, "latitude degrees", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED,
        RESOURCE_NUMBER_LONGITUDE, "longitude degrees", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED,
        RESOURCE_NUMBER_RADIUS, "radius metres", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED,
        RESOURCE_NUMBER_ALTITUDE, "altitude metres", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED,
        RESOURCE_NUMBER_SPEED, "speed MPS", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED,
        RESOURCE_NUMBER_TIMESTAMP, "timestamp", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED
    };

// Constructor.
IocCtrlLocation::IocCtrlLocation(bool debugOn,
                                 Callback<bool(Location*)> getCallback)
                :IocCtrlHelper(debugOn)
{
    _debugOn = debugOn;
    _getCallback = getCallback;

    // Create the object
    // Make the object and its resources
    _object = makeObject(&_defObject);

    // Update the values held in the resources
    updateData();

    printfLog("IocCtrlLocation: object initialised.\n");
}

// Destructor.
IocCtrlLocation::~IocCtrlLocation()
{
    // TODO: find out why clearing up crashes things
    // delete _object;
}

// Update the reportable data for this object.
void IocCtrlLocation::updateData()
{
    M2MObjectInstance *objectInstance = _object->object_instance();;
    M2MResource *resource;
    Location data;
    char buffer[32];
    int length;

    // Update the data
    if (_getCallback) {
        if (_getCallback(&data)) {
            // Set the values in the resources based on the new data
            resource = objectInstance->resource(RESOURCE_NUMBER_LATITUDE);
            length = snprintf(buffer, sizeof(buffer), FORMAT_DEGREES, data.latitudeDegrees);
            resource->set_value((uint8_t*)buffer, length);
            resource = objectInstance->resource(RESOURCE_NUMBER_LONGITUDE);
            length = snprintf(buffer, sizeof(buffer), FORMAT_DEGREES, data.longitudeDegrees);
            resource->set_value((uint8_t*)buffer, length);
            resource = objectInstance->resource(RESOURCE_NUMBER_RADIUS);
            length = snprintf(buffer, sizeof(buffer), FORMAT_METRES, data.radiusMetres);
            resource->set_value((uint8_t*)buffer, length);
            resource = objectInstance->resource(RESOURCE_NUMBER_ALTITUDE);
            length = snprintf(buffer, sizeof(buffer), FORMAT_METRES, data.altitudeMetres);
            resource->set_value((uint8_t*)buffer, length);
            resource = objectInstance->resource(RESOURCE_NUMBER_SPEED);
            length = snprintf(buffer, sizeof(buffer), FORMAT_SPEED, data.speedMPS);
            resource->set_value((uint8_t*)buffer, length);
            resource = objectInstance->resource(RESOURCE_NUMBER_TIMESTAMP);
            length = snprintf(buffer, sizeof(buffer), FORMAT_TIMESTAMP, data.timestampUnix);
            resource->set_value((uint8_t*)buffer, length);
        }
    }
}

// Return this object.
M2MObject * IocCtrlLocation::getObject()
{
    return _object;
}

// End of file
