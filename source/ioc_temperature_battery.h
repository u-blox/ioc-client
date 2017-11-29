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
#include "battery_gauge_bq27441.h"
#include "battery_charger_bq24295.h"

#ifndef _IOC_TEMPERATURE_BATTERY_
#define _IOC_TEMPERATURE_BATTERY_

/* ----------------------------------------------------------------
 * GENERAL TYPES
 * -------------------------------------------------------------- */

// Local version of temperature data.
typedef struct {
    int32_t nowC;
    int32_t minC;
    int32_t maxC;
} TemperatureLocal;

/* ----------------------------------------------------------------
 * TEMPERATURE M2M C++ OBJECT DEFINITION
 * -------------------------------------------------------------- */
 
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

/* ----------------------------------------------------------------
 * BATTERY M2M C++ OBJECT DEFINITION
 * -------------------------------------------------------------- */
 
// Nothing is required here since this is all contained within the
// Device object
 
/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Initialise the stuff on the I2C bus (battery gauge and
 * charger, from which we also obtain a temperature reading).
 * @return  a pointer to the I2C instance or NULL on failure.
 */
I2C *pInitI2C();

/** Initialise the temperature object.
 * @return  a pointer to the Temperature object.
 */
IocM2mTemperature *pInitTemperature();

/**  Shut down the temperature object.
 */
void deinitTemperature();

/** Shut down the stuff on the I2C bus.
 */
void deinitI2C();

/** Determine if a battery is detected.
 * @return  true if a battery is detected, otherwise false.
 */
bool isBatteryDetected();


/** Determine whether external power is present or not.
 * @return  true if external power is present, otherwise false.
 */
bool isExternalPowerPresent();

/** Get the battery voltage.
 * @param pVoltageMV  a place to put the battery voltage.
 * @return            true if the battery voltage was read,
 *                    otherwise false.
 */
bool getBatteryVoltage(int32_t *pVoltageMV);

/** Get the battery current.
 * @param pCurrentMA  a place to put the current reading.
 * @return            true if the current was read,
 *                    otherwise false.
 */
bool getBatteryCurrent(int32_t *pCurrentMA);

/** Get the remaining battery percentage.
 * @param pBatteryLevelPercent  a place to put the percentage.
 * @return                      true if the percentage was read,
 *                              otherwise false.
 */
bool getBatteryRemainingPercentage(int32_t *pBatteryLevelPercent);

/** Return a bitmap containing the charger faults.
 * @return  a bitmap which may be tested against
 *          BatteryChargerBq24295:ChargerFault.
 */
char getChargerFaults();

/** Return the charger state.
 * @return  the charger state.
 */
BatteryChargerBq24295::ChargerState getChargerState();

#endif // _IOC_TEMPERATURE_BATTERY_

// End of file
