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
#include "log.h"

#include "ioc_cloud_client_dm.h"
#include "ioc_diagnostics.h"
#include "ioc_location.h"
#include "ioc_utils.h"

/* This file implements the LWM2M location object.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

 // The period at which GNSS location is read (if it is active).
#define GNSS_UPDATE_PERIOD_MS (CONFIG_DEFAULT_READY_WAKE_UP_TICK_COUNTER_PERIOD_1 / 2)

// Timeout for comms with the GNSS chip,
// should be less than GNSS_UPDATE_PERIOD_MS.
#define GNSS_COMMS_TIMEOUT_MS 1000

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

static GnssSerial *gpGnss = NULL;
static char gGnssBuffer[256];
static bool gPendingGnssStop = false;

static const char gDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const char gDaysInMonthLeapYear[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static bool getLocationData(IocM2mLocation::Location *pData);
static IocM2mLocation *gpM2mObject = NULL;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: MISC
 * -------------------------------------------------------------- */

// Check if a year is a leap year.
static bool isLeapYear(int year) {
    bool leapYear = false;

    if (year % 400 == 0) {
        leapYear = true;
    } else if (year % 4 == 0) {
        leapYear = true;
    }

    return leapYear;
}

// Initialise the GNSS chip.
static bool initGnssChip(GnssSerial * pGnss)
{
    bool success = false;
    Timer timer;
    int length;
    int returnCode;

    if (pGnss->init()) {
        // See ublox7-V14_ReceiverDescrProtSpec section 35.14.3 (CFG-PRT)
        // Switch off NMEA messages as they get in the way
        memset(gGnssBuffer, sizeof (gGnssBuffer), 0);
        gGnssBuffer[0] = 1; // The UART port
        gGnssBuffer[7] = 0x10; // Set Reserved1 bit for compatibility reasons
        gGnssBuffer[13] = 0x01; // UBX protocol only in
        gGnssBuffer[15] = 0x01; // UBX protocol only out
        // Send length is 20 bytes of payload + 6 bytes header + 2 bytes CRC
        if (gpGnss->sendUbx(0x06, 0x00, gGnssBuffer, 20) == 28) {
            timer.start();
            while (!success && (timer.read_ms() < GNSS_COMMS_TIMEOUT_MS)) {
                // Wait for the Ack
                returnCode = gpGnss->getMessage(gGnssBuffer, sizeof(gGnssBuffer));
                if ((returnCode != GnssSerial::WAIT) && (returnCode != GnssSerial::NOT_FOUND)) {
                    length = LENGTH(returnCode);
                    if (((PROTOCOL(returnCode) == GnssSerial::UBX) && (length >= 10))) {
                        // Ack is  0xb5-62-05-00-02-00-msgclass-msgid-crcA-crcB
                        // Nack is 0xb5-62-05-01-02-00-msgclass-msgid-crcA-crcB
                        // (see ublox7-V14_ReceiverDescrProtSpec section 33)
                        if ((gGnssBuffer[0] == 0xb5) &&
                            (gGnssBuffer[1] == 0x62) &&
                            (gGnssBuffer[2] == 0x05) &&
                            (gGnssBuffer[3] == 0x00) &&
                            (gGnssBuffer[4] == 0x02) &&
                            (gGnssBuffer[5] == 0x00) &&
                            (gGnssBuffer[6] == 0x06) &&
                            (gGnssBuffer[7] == 0x00)) {
                            success = true;
                        }
                    }
                }
            }
        }
    }

    return success;
}

/// Derive an unsigned int from a pointer to a
// little-endian unsigned int in memory.
static unsigned int littleEndianUInt(char *pByte) {
    unsigned int value;

    value = *pByte;
    value += ((unsigned int) *(pByte + 1)) << 8;
    value += ((unsigned int) *(pByte + 2)) << 16;
    value += ((unsigned int) *(pByte + 3)) << 24;

    return value;
}

// Callback to update GNSS.
static bool gnssUpdate(IocM2mLocation::Location *location)
{
    bool response = false;
    bool success = false;
    Timer timer;
    int length;
    int returnCode;
    int year;
    int months;
    int gpsTime = 0;

    if (gpGnss) {
        // See ublox7-V14_ReceiverDescrProtSpec section 39.7 (NAV-PVT)
        // Send length is 0 bytes of payload + 6 bytes header + 2 bytes CRC
        if (gpGnss->sendUbx(0x01, 0x07) == 8) {
            timer.start();
            while (!response && (timer.read_ms() < GNSS_COMMS_TIMEOUT_MS)) {
                returnCode = gpGnss->getMessage(gGnssBuffer, sizeof(gGnssBuffer));
                if ((returnCode != GnssSerial::WAIT) && (returnCode != GnssSerial::NOT_FOUND)) {
                    length = LENGTH(returnCode);
                    if ((PROTOCOL(returnCode) == GnssSerial::UBX) && (length >= 84)) {
                        response = true;
                        // Note in what follows that the offsets include 6 bytes of header,
                        // consisting of 0xb5-62-msgclass-msgid-length1-length2.

                        // The time/date is contained at byte offsets as follows:
                        //
                        // 10 - two bytes of year, little-endian (UTC)
                        // 12 - month, range 1..12 (UTC)
                        // 13 - day, range 1..31 (UTC)
                        // 14 - hour, range 0..23 (UTC)
                        // 15 - min, range 0..59 (UTC)
                        // 16 - sec, range 0..60 (UTC)
                        // 17 - validity (0x03 or higher means valid)
                        if ((gGnssBuffer[17] & 0x03) == 0x03) {
                            // Year 1999-2099, so need to adjust to get year since 1970
                            year = ((int) (gGnssBuffer[10])) + ((int) (gGnssBuffer[11]) << 8) - 1999 + 29;
                            // Month (1 to 12), so take away 1 to make it zero-based
                            months = gGnssBuffer[12] - 1;
                            months += year * 12;
                            // Work out the number of seconds due to the year/month count
                            for (int x = 0; x < months; x++) {
                                if (isLeapYear ((x / 12) + 1970)) {
                                    gpsTime += gDaysInMonthLeapYear[x % 12] * 3600 * 24;
                                } else {
                                    gpsTime += gDaysInMonth[x % 12] * 3600 * 24;
                                }
                            }
                            // Day (1 to 31)
                            gpsTime += ((int) gGnssBuffer[13] - 1) * 3600 * 24;
                            // Hour (0 to 23)
                            gpsTime += ((int) gGnssBuffer[14]) * 3600;
                            // Minute (0 to 59)
                            gpsTime += ((int) gGnssBuffer[15]) * 60;
                            // Second (0 to 60)
                            gpsTime += gGnssBuffer[16];

                            LOG(EVENT_GNSS_TIMESTAMP, gpsTime);
                            location->timestampUnix = gpsTime;
                            // Update system time
                            setStartTime(getStartTime() + gpsTime - time(NULL));
                            set_time(gpsTime);
                            LOG(EVENT_CURRENT_TIME_UTC, time(NULL));
                        }

                        // The fix information is contained at byte offsets as follows:
                        //
                        // 26 - fix type, where 0x02 (2D) or 0x03 (3D) are good enough
                        // 27 - fix status flag, where bit 0 must be set for gnssFixOK
                        // 30 - 4 bytes of longitude, little-endian, in degrees * 10000000
                        // 34 - 4 bytes of latitude, little-endian, in degrees * 10000000
                        // 42 - 4 bytes of height above sea level, little-endian, millimetres
                        // 46 - 4 bytes of horizontal accuracy estimate, little-endian, millimetres
                        // 66 - 4 bytes of speed, little-endian, millimetres/second
                        if (((gGnssBuffer[26] == 0x03) || (gGnssBuffer[26] == 0x02)) &&
                            ((gGnssBuffer[27] & 0x01) == 0x01)) {
                            location->longitudeDegrees = ((float) littleEndianUInt(&(gGnssBuffer[30]))) / 10000000;
                            location->latitudeDegrees = ((float) littleEndianUInt(&(gGnssBuffer[34]))) / 10000000;
                            location->radiusMetres = ((float) littleEndianUInt(&(gGnssBuffer[46]))) / 1000;
                            location->speedMPS = ((float) littleEndianUInt(&(gGnssBuffer[66]))) / 1000;
                            LOG(EVENT_GNSS_LONGITUDE, littleEndianUInt(&(gGnssBuffer[30])));
                            LOG(EVENT_GNSS_LATITUDE, littleEndianUInt(&(gGnssBuffer[34])));
                            LOG(EVENT_GNSS_RADIUS, littleEndianUInt(&(gGnssBuffer[46])));
                            LOG(EVENT_GNSS_SPEED, littleEndianUInt(&(gGnssBuffer[66])));
                            if (gGnssBuffer[26] == 0x03) {
                                location->altitudeMetres = ((float) littleEndianUInt(&(gGnssBuffer[42]))) / 1000;
                                LOG(EVENT_GNSS_ALTITUDE, littleEndianUInt(&(gGnssBuffer[42])));
                            }
                            success = true;
                        }
                    }
                }
            }
        }
    }

    return success;
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: HOOK FOR GNSS M2M C++ OBJECT
 * -------------------------------------------------------------- */

// Callback that retrieves location data for the IocM2mLocation
// object.
static bool getLocationData(IocM2mLocation::Location *pData)
{
    return gnssUpdate(pData);
}

/* ----------------------------------------------------------------
 * PUBLIC: INITIALISATION
 * -------------------------------------------------------------- */

// Initialise the location object.
IocM2mLocation *pInitLocation()
{
    gpM2mObject = new IocM2mLocation(getLocationData, MBED_CONF_APP_OBJECT_DEBUG_ON);
    return gpM2mObject;

}

// Start the GNSS chip.
bool startGnss()
{
    flash();
    LOG(EVENT_GNSS_START, 0);
    printf("Starting GNSS...\n");
    gpGnss = new GnssSerial();
    if (!initGnssChip(gpGnss)) {
        bad();
        LOG(EVENT_GNSS_START_FAILURE, 0);
        printf ("WARNING: unable to initialise GNSS.\n");
        delete gpGnss;
        gpGnss = NULL;
    }
    
    return (gpGnss != NULL);
}

// Shut down the GNSS chip.
void stopGnss()
{
    if (gpGnss != NULL) {
        flash();
        LOG(EVENT_GNSS_STOP, 0);
        printf ("Stopping GNSS...\n");
        delete gpGnss;
        gpGnss = NULL;
    }
}

// Shut down the location object.
void deinitLocation()
{
    stopGnss();
    delete gpM2mObject;
    gpM2mObject = NULL;
}

// Set the pending stop flag.
void setPendingGnssStop(bool isOn)
{
    if (isOn) {
        LOG(EVENT_GNSS_STOP_PENDING, 0);
    }
    gPendingGnssStop = isOn;
}

// Get the pending stop flag.
bool getPendingGnssStop()
{
    return gPendingGnssStop;
}

// Return whether the GNSS chip is on or not
bool isGnssOn()
{
    return (gpGnss != NULL) && !gPendingGnssStop;
}

/* ----------------------------------------------------------------
 * PUBLIC: LOCATION M2M C++ OBJECT
 * -------------------------------------------------------------- */

/** The definition of the object (C++ pre C11 won't let this const
 * initialisation be done in the class definition).
 */
const M2MObjectHelper::DefObject IocM2mLocation::_defObject =
    {0, "6", 6,
        -1, RESOURCE_NUMBER_LATITUDE, "latitude", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_DEGREES,
        -1, RESOURCE_NUMBER_LONGITUDE, "longitude", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_DEGREES,
        -1, RESOURCE_NUMBER_RADIUS, "radius", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_METRES,
        -1, RESOURCE_NUMBER_ALTITUDE, "altitude", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_METRES,
        -1, RESOURCE_NUMBER_SPEED, "speed", M2MResourceBase::FLOAT, true, M2MBase::GET_ALLOWED, FORMAT_SPEED,
        -1, RESOURCE_NUMBER_TIMESTAMP, "timestamp", M2MResourceBase::INTEGER, true, M2MBase::GET_ALLOWED, NULL,
    };

// Constructor.
IocM2mLocation::IocM2mLocation(Callback<bool(Location *)> getCallback,
                               bool debugOn)
               :M2MObjectHelper(&_defObject, NULL, NULL, debugOn)
{
    _getCallback = getCallback;

    // Make the object and its resources
    MBED_ASSERT(makeObject());

    // Update the values held in the resources
    updateObservableResources();

    printf("IocM2mLocation: object initialised.\n");
}

// Destructor.
IocM2mLocation::~IocM2mLocation()
{
}

// Update the observable data for this object.
void IocM2mLocation::updateObservableResources()
{
    Location data;

    // Update the data
    if (_getCallback) {
        if (_getCallback(&data)) {
            // Set the values in the resources based on the new data
            MBED_ASSERT(setResourceValue(data.latitudeDegrees, RESOURCE_NUMBER_LATITUDE));
            MBED_ASSERT(setResourceValue(data.longitudeDegrees, RESOURCE_NUMBER_LONGITUDE));
            MBED_ASSERT(setResourceValue(data.radiusMetres, RESOURCE_NUMBER_RADIUS));
            MBED_ASSERT(setResourceValue(data.altitudeMetres, RESOURCE_NUMBER_ALTITUDE));
            MBED_ASSERT(setResourceValue(data.speedMPS, RESOURCE_NUMBER_SPEED));
            MBED_ASSERT(setResourceValue(data.timestampUnix, RESOURCE_NUMBER_TIMESTAMP));
        }
    }
}

// End of file
