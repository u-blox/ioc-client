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
 *   - Connectivity Monitoring (urn:oma:lwm2m:oma:4),
 *   - Temperature Sensor (urn:oma:lwm2m:ext:3303),
 *   - Humidity (urn:oma:lwm2m:ext:3304),
 *   - System Log (urn:oma:lwm2m:x:10259),
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

/** Control the power state of the device.
 * Implementation is according to urn:oma:lwm2m:ext:3312
 * with only the mandatory resource (on/off).
 */
class IocCtrlPowerControl : public MbedCloudClientCallback {
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

    /** Return this object.
     *
     * @return pointer to this object.
     */
    M2MObject * getObject();

    /** MbedCloudClientCallback.
     *
     * @return base information about the thing.
     * @return type the type of the thing (object or resource).
     */
    virtual void value_updated(M2MBase *base, M2MBase::BaseType type);

protected:

    /** Set to true to have debug prints.
     */
    bool                 _debugOn;

    /** Callback to set device on (true) or off (false).
     */
    Callback<void(bool)> _setCallback;

    /** The power control object.
     */
    M2MObject           *_object;
};

#endif // _IOC_CONTROL_

// End of file
