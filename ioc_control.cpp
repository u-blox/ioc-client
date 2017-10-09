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
#include "MbedCloudClient.h"
#include "ioc_control.h"

#ifdef MBED_CLOUD_CLIENT_SUPPORT_UPDATE
#include "update_ui_example.h"
#endif

#define printfLog(format, ...) debug_if(_debugOn, format, ## __VA_ARGS__)

/**********************************************************************
 * BASE CLASS
 **********************************************************************/

// Constructor.
IocCtrlBase::IocCtrlBase(bool debugOn, const DefObject *defObject, value_updated_callback valueUpdatedCallback)
{
    _debugOn = debugOn;
    _defObject = defObject;
    _object = NULL;
    _valueUpdatedCallback = valueUpdatedCallback;
}

// Destructor.
IocCtrlBase::~IocCtrlBase()
{
    delete _object;
}

// Default implementation of updateObservableResources.
void IocCtrlBase::updateObservableResources()
{
}

// Create this object.
bool IocCtrlBase::makeObject()
{
    bool allResourcesCreated = true;
    M2MObjectInstance *objectInstance;
    M2MResource *resource;
    M2MResourceInstance *resourceInstance;
    const DefResource *defResource;

    if (_defObject != NULL) {
        printfLog("IocCtrlBase: making object \"%s\" with %d resource(s).\n",
                  _defObject->name, _defObject->numResources);

        // Create the object according to the definition
        _object = M2MInterfaceFactory::create_object(_defObject->name);
        if (_object != NULL) {
            // Create the resources according to the definition
            objectInstance = _object->create_object_instance();
            if (objectInstance != NULL) {
                for (int x = 0; x < _defObject->numResources; x++) {
                    defResource = &(_defObject->resources[x]);
                    // If this is a multi-instance resource, create the base
                    // instance if it's not already there
                    if ((defResource->instance != -1) && (objectInstance->resource(defResource->name) == NULL)) {
                        printfLog("IocCtrlBase: creating base instance of multi-instance resource \"%s\" in object \"%s\".\n",
                                  defResource->name, _object->name());
                        resource = objectInstance->create_dynamic_resource((const char *) defResource->name,
                                                                           (const char *) defResource->typeString,
                                                                           defResource->type,
                                                                           defResource->observable,
                                                                           true /* multi-instance */);
                        if (resource == NULL) {
                            allResourcesCreated = false;
                            printfLog("IocCtrlBase: unable to create base instance of multi-instance resource \"%s\" in object \"%s\".\n",
                                      defResource->name, _defObject->name);
                        }
                    }

                    // Now create the resource
                    if (defResource->instance >= 0) {
                        printfLog("IocCtrlBase: creating instance %d of multi-instance resource \"%s\" in object \"%s\".\n",
                                  defResource->instance, defResource->name, _object->name());
                        resourceInstance = objectInstance->create_dynamic_resource_instance((const char *) defResource->name,
                                                                                            (const char *) defResource->typeString,
                                                                                            defResource->type,
                                                                                            defResource->observable,
                                                                                            defResource->instance);
                        if (resourceInstance != NULL) {
                            resourceInstance->set_operation(defResource->operation);
                            if (_valueUpdatedCallback) {
                                resourceInstance->set_value_updated_function(_valueUpdatedCallback);
                            }
                        } else {
                            allResourcesCreated = false;
                            printfLog("IocCtrlBase: unable to create instance %d of multi-instance resource \"%s\" in object \"%s\".\n",
                                      defResource->instance, defResource->name, _defObject->name);
                        }
                    } else {
                        printfLog("IocCtrlBase: creating single-instance resource \"%s\" in object \"%s\".\n",
                                  defResource->name, _object->name());
                        resource = objectInstance->create_dynamic_resource((const char *) defResource->name,
                                                                           (const char *) defResource->typeString,
                                                                           defResource->type,
                                                                           defResource->observable);
                        if (resource != NULL) {
                            resource->set_operation(defResource->operation);
                            if (_valueUpdatedCallback) {
                                resource->set_value_updated_function(_valueUpdatedCallback);
                            }
                        } else {
                            allResourcesCreated = false;
                            printfLog("IocCtrlBase: unable to create single-instance resource \"%s\" in object \"%s\".\n",
                                      defResource->name, _defObject->name);
                        }
                    }
                }
            } else {
                printfLog("IocCtrlBase: unable to create instance of object \"%s\".\n", _defObject->name);
            }
        } else {
            printfLog("IocCtrlBase: unable to create object \"%s\".\n", _defObject->name);
        }
    } else {
        printfLog("IocCtrlBase: defObject is NULL.\n");
    }

    return (objectInstance != NULL) && allResourcesCreated;
}

// Set the execute function for a resource.
bool IocCtrlBase::setExecuteCallback(execute_callback callback, const char * resourceNumber)
{
    bool success = false;
    M2MObjectInstance *objectInstance;
    M2MResource *resource;

    if (_object != NULL) {
        printfLog("IocCtrlBase: setting execute callback for resource \"%s\" in object \"%s\".\n",
                  resourceNumber, _object->name());
        objectInstance = _object->object_instance();
        if (objectInstance != NULL) {
            resource = objectInstance->resource(resourceNumber);
            if (resource != NULL) {
                success = resource->set_execute_function(callback);
            } else {
                printfLog("IocCtrlBase: unable to find resource \"%s\" in object \"%s\".\n",
                          resourceNumber, _object->name());
            }
        } else {
            printfLog("IocCtrlBase: unable to get instance of object \"%s\".\n", _object->name());
        }
    } else {
        printfLog("IocCtrlBase: object is NULL.\n");
    }

    return success;
}

// Set the value of a given resource in an object.
bool IocCtrlBase::setResourceValue(const void * value,
                                   const char * resourceNumber,
                                   int wantedInstance)
{
    bool success = false;
    M2MObjectInstance *objectInstance;
    M2MResource *resource;
    M2MResourceInstance *resourceInstance = NULL;
    M2MResourceBase::ResourceType type;
    const char * format = NULL;
    bool foundIt = false;
    int64_t valueInt64;
    char buffer[32];
    int length;

    if (_object != NULL) {
        objectInstance = _object->object_instance();
        if (objectInstance != NULL) {
            resource = objectInstance->resource(resourceNumber);
            if (resource != NULL) {

                printfLog("IocCtrlBase: setting value of resource \"%s\", instance %d (-1 == single instance), in object \"%s\".\n",
                          resourceNumber, wantedInstance, _object->name());

                // Find the resource type and format from the object definition
                for (int x = 0; (x < _defObject->numResources) && !foundIt; x++) {
                    if ((strcmp(resourceNumber, _defObject->resources[x].name) == 0) &&
                        (wantedInstance == _defObject->resources[x].instance)) {
                        type = _defObject->resources[x].type;
                        format = _defObject->resources[x].format;
                        foundIt = true;
                    }
                }

                // If this is a multi-instance resource, get the resource instance
                if (resource->supports_multiple_instances()) {
                    resourceInstance = resource->resource_instance(wantedInstance);
                    if (resourceInstance == NULL) {
                        foundIt = false;
                    }
                }

                if (foundIt) {
                    switch (type) {
                        case M2MResourceBase::STRING:
                            printfLog("IocCtrlBase:   STRING resource set to \"%s\".\n", (*((String *) value)).c_str());
                            if (resourceInstance != NULL) {
                                success = resourceInstance->set_value((const uint8_t *) (*((const String *) value)).c_str(), (*((const String *) value)).size());
                            } else {
                                success = resource->set_value((const uint8_t *) (*((const String *) value)).c_str(), (*((const String *) value)).size());
                            }
                            break;
                        case M2MResourceBase::INTEGER:
                        case M2MResourceBase::TIME:
                            valueInt64 = *((int64_t *) value);
                            printfLog("IocCtrlBase:   INTEGER or TIME resource set to %lld.\n", valueInt64);
                            if (resourceInstance != NULL) {
                                success = resourceInstance->set_value(valueInt64);
                            } else {
                                success = resource->set_value(valueInt64);
                            }
                            break;
                        case M2MResourceBase::BOOLEAN:
                            valueInt64 = *((bool *) value);
                            printfLog("IocCtrlBase:   BOOLEAN resource set to %lld.\n", valueInt64);
                            if (resourceInstance != NULL) {
                                success = resourceInstance->set_value(valueInt64);
                            } else {
                                success = resource->set_value(valueInt64);
                            }
                            break;
                        case M2MResourceBase::FLOAT:
                            length = snprintf(buffer, sizeof(buffer), format, *((float *) value));
                            printfLog("IocCtrlBase:   FLOAT resource set to %f (\"%*s\", the format string being \"%s\").\n",
                                      *((float *) value), length, buffer, format);
                            if (resourceInstance != NULL) {
                                success = resourceInstance->set_value((uint8_t *) buffer, length);
                            } else {
                                success = resource->set_value((uint8_t *) buffer, length);
                            }
                            break;
                        case M2MResourceBase::OBJLINK:
                        case M2MResourceBase::OPAQUE:
                            printfLog("IocCtrlBase:   don't know how to handle resource type %d (OBJLINK or OPAQUE).\n", type);
                            break;
                        default:
                            printfLog("IocCtrlBase:   unknown resource type %d.\n", type);
                            break;
                    }
                } else {
                    printfLog("IocCtrlBase: unable to find resource \"%s\", instance %d, in object \"%s\".\n",
                              resourceNumber, wantedInstance, _object->name());
                }
            } else {
                printfLog("IocCtrlBase: unable to find resource \"%s\" of object \"%s\".\n",
                          resourceNumber, _object->name());
            }
        } else {
            printfLog("IocCtrlBase: unable to get instance of object \"%s\".\n", _object->name());
        }
    } else {
        printfLog("IocCtrlBase: object is NULL.\n");
    }

    return success;
}

// Get the value of a given resource in an object.
bool IocCtrlBase::getResourceValue(void * value,
                                   const char * resourceNumber,
                                   int wantedInstance)
{
    bool success = false;
    M2MObjectInstance *objectInstance;
    M2MResource *resource;
    M2MResourceInstance *resourceInstance = NULL;
    M2MResourceBase::ResourceType type;
    bool foundIt = false;
    int64_t localValue;
    String str;

    if (_object != NULL) {
        objectInstance = _object->object_instance();
        if (objectInstance != NULL) {
            resource = objectInstance->resource(resourceNumber);
            if (resource != NULL) {
                printfLog("IocCtrlBase: getting value of resource \"%s\", instance %d (-1 == single instance), from object \"%s\".\n",
                          resourceNumber, wantedInstance, _object->name());

                // Find the resource type from the object definition
                for (int x = 0; (x < _defObject->numResources) && !foundIt; x++) {
                    if ((strcmp(resourceNumber, _defObject->resources[x].name) == 0) &&
                        (wantedInstance == _defObject->resources[x].instance)) {
                        type = _defObject->resources[x].type;
                        foundIt = true;
                    }
                }

                // If this is a multi-instance resource, get the resource instance
                if (resource->supports_multiple_instances()) {
                    resourceInstance = resource->resource_instance(wantedInstance);
                    if (resourceInstance == NULL) {
                        foundIt = false;
                    }
                }

                if (foundIt) {
                    switch (type) {
                        case M2MResourceBase::STRING:
                            if (resourceInstance != NULL) {
                                *(String *) value = resourceInstance->get_value_string();
                            } else {
                                *(String *) value = resource->get_value_string();
                            }
                            printfLog("IocCtrlBase:   STRING resource value is \"%s\".\n", (*((String *) value)).c_str());
                            success = true;
                            break;
                        case M2MResourceBase::INTEGER:
                        case M2MResourceBase::TIME:
                            if (resourceInstance != NULL) {
                                *((int64_t *) value) = resourceInstance->get_value_int();
                            } else {
                                *((int64_t *) value) = resource->get_value_int();
                            }
                            printfLog("IocCtrlBase:   INTEGER or TIME resource is %lld.\n", *((int64_t *) value));
                            success = true;
                            break;
                        case M2MResourceBase::BOOLEAN:
                            if (resourceInstance != NULL) {
                                localValue = resourceInstance->get_value_int();
                            } else {
                                localValue = resource->get_value_int();
                            }
                            *(bool *) value = (bool) localValue;
                            printfLog("IocCtrlBase:   BOOLEAN resource is %d.\n", *((bool *) value));
                            success = true;
                            break;
                        case M2MResourceBase::FLOAT:
                            if (resourceInstance != NULL) {
                                str = resourceInstance->get_value_string();
                            } else {
                                str = resource->get_value_string();
                            }
                            sscanf(str.c_str(), "%f", (float *) value);
                            printfLog("IocCtrlBase:   FLOAT resource is %f (\"%s\").\n", *((float *) value), str.c_str());
                            success = true;
                            break;
                        case M2MResourceBase::OBJLINK:
                        case M2MResourceBase::OPAQUE:
                            printfLog("IocCtrlBase:   don't know how to handle resource type %d (OBJLINK or OPAQUE).\n", type);
                            break;
                        default:
                            printfLog("IocCtrlBase:   unknown resource type %d.\n", type);
                            break;
                    }
                } else {
                    printfLog("IocCtrlBase: unable to find resource \"%s\", instance %d, in object \"%s\".\n",
                              resourceNumber, wantedInstance, _object->name());
                }
            } else {
                printfLog("IocCtrlBase: unable to find resource \"%s\" of object \"%s\".\n",
                          resourceNumber, _object->name());
            }
        } else {
            printfLog("IocCtrlBase: unable to get instance of object \"%s\".\n", _object->name());
        }
    } else {
        printfLog("IocCtrlBase: object is NULL.\n");
    }

    return success;
}

// Return this object.
M2MObject *IocCtrlBase::getObject()
{
    return _object;
}

/**********************************************************************
 * POWER CONTROL OBJECT
 **********************************************************************/

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const IocCtrlBase::DefObject IocCtrlPowerControl::_defObject =
    {"3312", 1,
        -1, RESOURCE_NUMBER_POWER_SWITCH, "on/off", M2MResourceBase::BOOLEAN, false, M2MBase::GET_PUT_ALLOWED, NULL
    };

// Constructor.
IocCtrlPowerControl::IocCtrlPowerControl(bool debugOn,
                                         Callback<void(bool)> setCallback,
                                         bool initialValue)
                    :IocCtrlBase(debugOn, &_defObject, value_updated_callback(this, &IocCtrlPowerControl::objectUpdated))
{
    _setCallback = setCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Set the initial value
    MBED_ASSERT(setResourceValue(&initialValue, RESOURCE_NUMBER_POWER_SWITCH));

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
const IocCtrlBase::DefObject IocCtrlLocation::_defObject =
    {"6", 6,
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
                :IocCtrlBase(debugOn, &_defObject)
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
            MBED_ASSERT(setResourceValue(&data.latitudeDegrees, RESOURCE_NUMBER_LATITUDE));
            MBED_ASSERT(setResourceValue(&data.longitudeDegrees, RESOURCE_NUMBER_LONGITUDE));
            MBED_ASSERT(setResourceValue(&data.radiusMetres, RESOURCE_NUMBER_RADIUS));
            MBED_ASSERT(setResourceValue(&data.altitudeMetres, RESOURCE_NUMBER_ALTITUDE));
            MBED_ASSERT(setResourceValue(&data.speedMPS, RESOURCE_NUMBER_SPEED));
            MBED_ASSERT(setResourceValue(&data.timestampUnix, RESOURCE_NUMBER_TIMESTAMP));
        }
    }
}

/**********************************************************************
 * TEMPERATURE OBJECT
 **********************************************************************/

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const IocCtrlBase::DefObject IocCtrlTemperature::_defObject =
    {"3303", 7,
        -1, RESOURCE_NUMBER_TEMPERATURE, "temperature", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_TEMPERATURE,
        -1, RESOURCE_NUMBER_MIN_TEMPERATURE, "temperature", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_TEMPERATURE,
        -1, RESOURCE_NUMBER_MAX_TEMPERATURE, "temperature", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_TEMPERATURE,
        -1, RESOURCE_NUMBER_RESET_MIN_MAX, "string", M2MResourceBase::STRING, false, M2MBase::POST_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_MIN_RANGE, "temperature", M2MResourceBase::FLOAT, false, M2MBase::GET_ALLOWED, FORMAT_TEMPERATURE,
        -1, RESOURCE_NUMBER_MAX_RANGE, "temperature", M2MResourceBase::FLOAT, false, M2MBase::GET_ALLOWED, FORMAT_TEMPERATURE,
        -1, RESOURCE_NUMBER_UNITS, "string", M2MResourceBase::STRING, false, M2MBase::GET_ALLOWED, NULL
    };

// Constructor.
IocCtrlTemperature::IocCtrlTemperature(bool debugOn,
                                       Callback<bool(Temperature *)> getCallback,
                                       Callback<void(void)> resetMinMaxCallback,
                                       float minRange,
                                       float maxRange,
                                       String units)
                   :IocCtrlBase(debugOn, &_defObject)
{
    _getCallback = getCallback;
    _resetMinMaxCallback = resetMinMaxCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Set the fixed value resources here
    MBED_ASSERT(setResourceValue(&minRange, RESOURCE_NUMBER_MIN_RANGE));
    MBED_ASSERT(setResourceValue(&maxRange, RESOURCE_NUMBER_MAX_RANGE));
    MBED_ASSERT(setResourceValue(&units, RESOURCE_NUMBER_UNITS));

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
            MBED_ASSERT(setResourceValue(&data.temperature, RESOURCE_NUMBER_TEMPERATURE));
            MBED_ASSERT(setResourceValue(&data.minTemperature, RESOURCE_NUMBER_MIN_TEMPERATURE));
            MBED_ASSERT(setResourceValue(&data.maxTemperature, RESOURCE_NUMBER_MAX_TEMPERATURE));
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
const IocCtrlBase::DefObject IocCtrlConfig::_defObject =
    {"32769", 6,
        RESOURCE_INSTANCE_INIT_WAKE_UP, RESOURCE_NUMBER_INIT_WAKE_UP_TICK_PERIOD, "seconds", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, FORMAT_SECONDS,
        RESOURCE_INSTANCE_INIT_WAKE_UP, RESOURCE_NUMBER_INIT_WAKE_UP_COUNT, "counter", M2MResourceBase::INTEGER, false, M2MBase::GET_PUT_ALLOWED, NULL,
        RESOURCE_INSTANCE_NORMAL_WAKE_UP, RESOURCE_NUMBER_NORMAL_WAKE_UP_TICK_PERIOD, "seconds", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, FORMAT_SECONDS,
        RESOURCE_INSTANCE_NORMAL_WAKE_UP, RESOURCE_NUMBER_NORMAL_WAKE_UP_COUNT, "counter", M2MResourceBase::INTEGER, false, M2MBase::GET_PUT_ALLOWED, NULL,
        RESOURCE_INSTANCE_BATTERY_WAKE_UP, RESOURCE_NUMBER_BATTERY_WAKE_UP_TICK_PERIOD, "seconds", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, FORMAT_SECONDS,
        -1, RESOURCE_NUMBER_GNSS_ENABLE, "boolean", M2MResourceBase::BOOLEAN, false, M2MBase::GET_PUT_ALLOWED, NULL
    };

// Constructor.
IocCtrlConfig::IocCtrlConfig(bool debugOn,
                             Callback<void(Config *)> setCallback,
                             Config *initialValues)
              :IocCtrlBase(debugOn, &_defObject, value_updated_callback(this, &IocCtrlConfig::objectUpdated))
{
    _setCallback = setCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Set the initial values
    MBED_ASSERT(setResourceValue(&initialValues->initWakeUpTickPeriod,
                                 RESOURCE_NUMBER_INIT_WAKE_UP_TICK_PERIOD, RESOURCE_INSTANCE_INIT_WAKE_UP));
    MBED_ASSERT(setResourceValue(&initialValues->initWakeUpCount,
                                 RESOURCE_NUMBER_INIT_WAKE_UP_COUNT, RESOURCE_INSTANCE_INIT_WAKE_UP));
    MBED_ASSERT(setResourceValue(&initialValues->normalWakeUpTickPeriod,
                                 RESOURCE_NUMBER_NORMAL_WAKE_UP_TICK_PERIOD, RESOURCE_INSTANCE_NORMAL_WAKE_UP));
    MBED_ASSERT(setResourceValue(&initialValues->normalWakeUpCount,
                                 RESOURCE_NUMBER_NORMAL_WAKE_UP_COUNT, RESOURCE_INSTANCE_NORMAL_WAKE_UP));
    MBED_ASSERT(setResourceValue(&initialValues->batteryWakeUpTickPeriod,
                                 RESOURCE_NUMBER_BATTERY_WAKE_UP_TICK_PERIOD, RESOURCE_INSTANCE_BATTERY_WAKE_UP));
    MBED_ASSERT(setResourceValue(&initialValues->gnssEnable, RESOURCE_NUMBER_GNSS_ENABLE));

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
    printfLog("               initWakeUpTickPeriod %f.\n", config.initWakeUpTickPeriod);
    printfLog("               initWakeUpCount %d.\n", config.initWakeUpCount);
    printfLog("               normalWakeUpTickPeriod %f.\n", config.normalWakeUpTickPeriod);
    printfLog("               normalWakeUpCount %d.\n", config.normalWakeUpCount);
    printfLog("               batteryWakeUpTickPeriod %f.\n", config.batteryWakeUpTickPeriod);
    printfLog("               GNSS enable %d.\n", config.gnssEnable);

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
const IocCtrlBase::DefObject IocCtrlAudio::_defObject =
    {"32770", 5,
        -1, RESOURCE_NUMBER_STREAMING_ENABLED, "boolean", M2MResourceBase::BOOLEAN, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_DURATION, "duration", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, FORMAT_SECONDS,
        -1, RESOURCE_NUMBER_FIXED_GAIN, "level", M2MResourceBase::FLOAT, false, M2MBase::GET_PUT_ALLOWED, FORMAT_GAIN,
        -1, RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE, "mode", M2MResourceBase::INTEGER, false, M2MBase::GET_PUT_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_AUDIO_SERVER_URL, "string", M2MResourceBase::STRING, false, M2MBase::GET_PUT_ALLOWED, NULL
    };

// Constructor.
IocCtrlAudio::IocCtrlAudio(bool debugOn,
                           Callback<void(Audio *)> setCallback,
                           Audio *initialValues)
             :IocCtrlBase(debugOn, &_defObject, value_updated_callback(this, &IocCtrlAudio::objectUpdated))
{
    _setCallback = setCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Set the initial values
    MBED_ASSERT(setResourceValue(&initialValues->streamingEnabled, RESOURCE_NUMBER_STREAMING_ENABLED));
    MBED_ASSERT(setResourceValue(&initialValues->duration, RESOURCE_NUMBER_DURATION));
    MBED_ASSERT(setResourceValue(&initialValues->fixedGain,  RESOURCE_NUMBER_FIXED_GAIN));
    MBED_ASSERT(setResourceValue(&initialValues->audioCommunicationsMode, RESOURCE_NUMBER_AUDIO_COMMUNICATIONS_MODE));
    MBED_ASSERT(setResourceValue(&initialValues->audioServerUrl, RESOURCE_NUMBER_AUDIO_SERVER_URL));

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
    printfLog("              streamingEnabled %d.\n", audio.streamingEnabled);
    printfLog("              duration %f (-1 == no limit).\n", audio.duration);
    printfLog("              fixedGain %f (-1 == use automatic gain).\n", audio.fixedGain);
    printfLog("              audioCommunicationsMode %d (0 for UDP, 1 for TCP).\n", audio.audioCommunicationsMode);
    printfLog("              audioServerUrl \"%s\".\n", audio.audioServerUrl.c_str());

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
const IocCtrlBase::DefObject IocCtrlDiagnostics::_defObject =
    {"32771", 6,
        -1, RESOURCE_NUMBER_UP_TIME, "on time", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        RESOURCE_INSTANCE_WORST_CASE_SEND_DURATION, RESOURCE_NUMBER_WORST_CASE_SEND_DURATION, "duration", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_SECONDS,
        RESOURCE_INSTANCE_AVERAGE_SEND_DURATION, RESOURCE_NUMBER_AVERAGE_SEND_DURATION, "duration", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_SECONDS,
        -1, RESOURCE_NUMBER_MIN_NUM_DATAGRAMS_FREE, "down counter", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_NUM_SEND_FAILURES, "up counter", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
        -1, RESOURCE_NUMBER_PERCENT_SENDS_TOO_LONG, "percent", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL
    };

// Constructor.
IocCtrlDiagnostics::IocCtrlDiagnostics(bool debugOn,
                                       Callback<bool(Diagnostics *)> getCallback)
                   :IocCtrlBase(debugOn, &_defObject)
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
            MBED_ASSERT(setResourceValue(&data.upTime, RESOURCE_NUMBER_UP_TIME));
            MBED_ASSERT(setResourceValue(&data.worstCaseSendDuration,
                                         RESOURCE_NUMBER_WORST_CASE_SEND_DURATION,
                                         RESOURCE_INSTANCE_WORST_CASE_SEND_DURATION));
            MBED_ASSERT(setResourceValue(&data.averageSendDuration,
                                         RESOURCE_NUMBER_AVERAGE_SEND_DURATION,
                                         RESOURCE_INSTANCE_AVERAGE_SEND_DURATION));
            MBED_ASSERT(setResourceValue(&data.minNumDatagramsFree, RESOURCE_NUMBER_MIN_NUM_DATAGRAMS_FREE));
            MBED_ASSERT(setResourceValue(&data.numSendFailures, RESOURCE_NUMBER_NUM_SEND_FAILURES));
            MBED_ASSERT(setResourceValue(&data.percentageSendsTooLong, RESOURCE_NUMBER_PERCENT_SENDS_TOO_LONG));
        }
    }
}

// End of file
