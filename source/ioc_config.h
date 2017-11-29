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

#ifndef _IOC_CONFIG_
#define _IOC_CONFIG_

/* ----------------------------------------------------------------
 * GENERAL TYPES
 * -------------------------------------------------------------- */

// Local version of config data.
typedef struct {
    time_t initWakeUpTickCounterPeriod;
    int64_t initWakeUpTickCounterModulo;
    time_t readyWakeUpTickCounterPeriod1;
    time_t readyWakeUpTickCounterPeriod2;
    int64_t readyWakeUpTickCounterModulo;
    bool gnssEnable;
} ConfigLocal;

/* ----------------------------------------------------------------
 * CONFIG M2M C++ OBJECT DEFINITION
 * -------------------------------------------------------------- */
 
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
        float initWakeUpTickCounterPeriod;
        int64_t initWakeUpTickCounterModulo;
        float readyWakeUpTickCounterPeriod1;
        float readyWakeUpTickCounterPeriod2;
        int64_t readyWakeUpTickCounterModulo;
        bool gnssEnable;
    } Config;

    /** Constructor.
     *
     * @param setCallback   callback to set the configuration values.
     * @param initialValues the initial state of the configuration values.
     * @param debugOn       true if you want debug prints, otherwise false.
     */
    IocM2mConfig(Callback<void(const Config *)> setCallback,
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

    /** The resource instance for initWakeUp.
     */
#   define RESOURCE_INSTANCE_INIT_WAKE_UP 0

    /** The resource number for initWakeUpTickCounterPeriod,
     * a Duration resource.
     */
#   define RESOURCE_NUMBER_INIT_WAKE_UP_TICK_COUNTER_PERIOD "5524"

    /** The resource number for initWakeUpTickCounterModulo,
     * a Counter resource.
     */
#   define RESOURCE_NUMBER_INIT_WAKE_UP_TICK_COUNTER_MODULO "5534"

    /** The resource instance for readyWakeUpTickCounterPeriod1.
     */
#   define RESOURCE_INSTANCE_READY_WAKE_UP_TICK_COUNTER_PERIOD_1 1

    /** The resource number for readyWakeUpTickCounterPeriod1,
     * a Duration resource.
     */
#   define RESOURCE_NUMBER_READY_WAKE_UP_TICK_COUNTER_PERIOD_1 "5524"

    /** The resource instance for readyWakeUpTickCounterPeriod2.
     */
#   define RESOURCE_INSTANCE_READY_WAKE_UP_TICK_COUNTER_PERIOD_2 2

    /** The resource number for readyWakeUpTickCounterPeriod2,
     * a Duration resource.
     */
#   define RESOURCE_NUMBER_READY_WAKE_UP_TICK_COUNTER_PERIOD_2 "5524"

    /** The resource instance for readyWakeUpTickCounterModulo.
     */
#   define RESOURCE_INSTANCE_READY_WAKE_UP_TICK_COUNTER_MODULO 1

    /** The resource number for readyWakeUpTickCounterModulo,
     * a Counter resource.
     */
#   define RESOURCE_NUMBER_READY_WAKE_UP_TICK_COUNTER_MODULO "5534"

    /** The resource number for gnssEnable, a Boolean
     * resource.
     */
#   define RESOURCE_NUMBER_GNSS_ENABLE "5850"

    /** Definition of this object.
     */
    static const DefObject _defObject;

    /** Callback to set configuration values.
     */
    Callback<void(const Config *)> _setCallback;
};

/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Reset confioguration to defaults.
 */
void resetConfig();

/** Initialise the configuration object.
 * @return  a pointer to the IocM2mConfig object.
 */
IocM2mConfig *pInitConfig();

/** Shut down the configuration object.
 */
void deinitConfig();

/** Get the value of initWakeUpTickCounterPeriod.
 * @return initWakeUpTickCounterPeriod.
 */
time_t getInitWakeUpTickCounterPeriod();

/** Get the value of initWakeUpTickCounterPeriod.
 * @return initWakeUpTickCounterModulo.
 */
int64_t getInitWakeUpTickCounterModulo();

/** Get the value of readyWakeUpTickCounterPeriod1.
 * @return readyWakeUpTickCounterPeriod1.
 */
time_t getReadyWakeUpTickCounterPeriod1();

/** Get the value of readyWakeUpTickCounterPeriod2.
 * @return readyWakeUpTickCounterPeriod2.
 */
time_t getReadyWakeUpTickCounterPeriod2();

/** Get the value of readyWakeUpTickCounterModulo.
 * @return readyWakeUpTickCounterModulo.
 */
int64_t getReadyWakeUpTickCounterModulo();

/** Return whether GNSS should be enabled or not.
 * @return true if GNSS should be enabled, else false.
 */
bool configIsGnssEnabled();

#endif // _IOC_CONFIG_

// End of file
