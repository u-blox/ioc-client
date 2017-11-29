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
#include "low_power.h"
#include "MbedCloudClient.h"
#include "m2m_object_helper.h"
#include "log.h"

#include "ioc_cloud_client_dm.h"
#include "ioc_dynamics.h"
#include "ioc_utils.h"
#include "ioc_power_control.h"

/* This file implements the LWM2M power control object.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The default power state
#define DEFAULT_POWER_ON_NOT_OFF false

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

BACKUP_SRAM
static bool gPowerOnNotOff;
static IocM2mPowerControl *gpM2mObject = NULL;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: HOOK FOR POWER CONTROL M2M C++ OBJECT
 * -------------------------------------------------------------- */

// Callback that sets the power switch via the IocM2mPowerControl
// object.
static void setPowerControl(bool value)
{
    LOG(EVENT_SET_POWER_CONTROL, value);
    printf("Power control set to %d.\n", value);

    // Something has happened, tell Ready mode about it
    readyModeInstructionReceived();

    gPowerOnNotOff = value;

    // TODO act on it
}

/* ----------------------------------------------------------------
 * PUBLIC: INITIALISATION
 * -------------------------------------------------------------- */

// Set power control defaults.
void resetPowerControl()
{
    gPowerOnNotOff = DEFAULT_POWER_ON_NOT_OFF;
}

// Initialise power control.
IocM2mPowerControl *pInitPowerControl()
{
    gpM2mObject = new IocM2mPowerControl(setPowerControl, true, MBED_CONF_APP_OBJECT_DEBUG_ON);
    return gpM2mObject;
}

// Shut down power control.
void deinitPowerControl()
{
    delete gpM2mObject;
    gpM2mObject = NULL;
}

/* ----------------------------------------------------------------
 * PUBLIC: POWER CONTROL M2M C++ OBJECT
 * -------------------------------------------------------------- */

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

    printf("IocM2mPowerControl: object initialised.\n");
}

// Destructor.
IocM2mPowerControl::~IocM2mPowerControl()
{
}

// Callback when the object is updated.
void IocM2mPowerControl::objectUpdated(const char *resourceName)
{
    bool onNotOff;

    printf("IocM2mPowerControl: resource \"%s\" has been updated.\n", resourceName);

    MBED_ASSERT(getResourceValue(&onNotOff, RESOURCE_NUMBER_POWER_SWITCH));

    printf("IocM2mPowerControl: new value is %d.\n", onNotOff);

    if (_setCallback) {
        _setCallback(onNotOff);
    }
}

// End of file
