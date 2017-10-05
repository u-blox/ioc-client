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
 * POWER CONTROL OBJECT
 **********************************************************************/

// Constructor.
IocCtrlPowerControl::IocCtrlPowerControl(bool debugOn,
                                         Callback<void(bool)> setCallback,
                                         bool initialValue)
{
    M2MObjectInstance *objectInstance;
    M2MResource *resource;

    _debugOn = debugOn;
    _setCallback = setCallback;

    _object = M2MInterfaceFactory::create_object(OBJECT_NUMBER);
    objectInstance = _object->create_object_instance();
    resource = objectInstance->create_dynamic_resource(RESOURCE_NUMBER_POWER_SWITCH,
                                                       "on/off",
                                                        M2MResourceInstance::BOOLEAN,
                                                        false /* observable */);
    resource->set_operation(M2MBase::GET_PUT_ALLOWED);
    resource->set_value((int64_t) initialValue);

    printfLog("IocCtrlPowerControl: object initialised.\n");
}

// Destructor.
IocCtrlPowerControl::~IocCtrlPowerControl()
{
    delete _object;
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

// End of file
