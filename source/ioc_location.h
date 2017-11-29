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
#include "gnss.h"

#ifndef _IOC_LOCATION_
#define _IOC_LOCATION_

/* ----------------------------------------------------------------
 * LOCATION M2M C++ OBJECT DEFINITION
 * -------------------------------------------------------------- */

/** Report location.
 * Implementation is according to urn:oma:lwm2m:oma:6
 * with all optional resources included except velocity.
 */
class IocM2mLocation : public M2MObjectHelper  {
public:

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

    /** Location structure.
     */
    typedef struct {
        float latitudeDegrees;
        float longitudeDegrees;
        float radiusMetres;
        float altitudeMetres;
        float speedMPS;
        int64_t timestampUnix;
    } Location;

    /** Constructor.
     *
     * @param getCallback  callback to get location information
     * @param debugOn      true if you want debug prints, otherwise false.
     */
    IocM2mLocation(Callback<bool(Location *)> getCallback,
                   bool debugOn = false);

    /** Destructor.
     */
    ~IocM2mLocation();

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

/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Initialise the location object.
 * Note: this does not initialise the GNSS chip.
 * @return  a pointer to the IocM2mLocation object.
 */
IocM2mLocation *pInitLocation();

/** Start the GNSS chip.
 * @return  true of successful, otherwise false.
 */
bool startGnss();

/** Stop the GNSS chip.
 */
void stopGnss();

/** Shut down the location object.
 * This will shut down the GNSS chip if it is on.
 */
void deinitLocation();

/** Set a flag indicating that GNSS should be
 * stopped when possible.
 * @param isOn the desired state of the flag.
 */
void setPendingGnssStop(bool isOn);

/** Get the pending GNSS stop flag.
 * @return the pending GNSS stop flag.
 */
bool getPendingGnssStop();

/** Return whether the GNSS chip is on or not.
 * @return true if the chip is on, else false.
 */
bool isGnssOn();

#endif // _IOC_LOCATION_

// End of file
