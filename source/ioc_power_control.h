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

#ifndef _IOC_POWER_CONTROL_
#define _IOC_POWER_CONTROL_

/* ----------------------------------------------------------------
 * POWER CONTROL M2M C++ OBJECT DEFINITION
 * -------------------------------------------------------------- */

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

/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Reset power control to defaults.
 */
void resetPowerControl();

/** Initialise power control.
 *
 * @return  a pointer to the IocM2mPowerControl object.
 */
IocM2mPowerControl *pInitPowerControl();

/** Shut down power control.
 */
void deinitPowerControl();

#endif // _IOC_POWER_CONTROL_

// End of file
