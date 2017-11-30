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
#include "UbloxPPPCellularInterface.h"
#include "MbedCloudClient.h"
#include "cloud_client_dm.h"
#include "m2m_object_helper.h"

#ifndef _IOC_CLOUD_CLIENT_DM_
#define _IOC_CLOUD_CLIENT_DM_

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

#ifndef MBED_CONF_APP_OBJECT_DEBUG_ON
// Get debug prints from LWM2M object stuff.
#  define MBED_CONF_APP_OBJECT_DEBUG_ON true
#endif

// The configuration items for the LWM2M Device object.
#define DEVICE_OBJECT_DEVICE_TYPE      "ioc"
#define DEVICE_OBJECT_SERIAL_NUMBER    "0"
#define DEVICE_OBJECT_HARDWARE_VERSION "0"
#define DEVICE_OBJECT_SOFTWARE_VERSION "0.0.0.0"
#define DEVICE_OBJECT_FIRMWARE_VERSION "0"
#define DEVICE_OBJECT_MEMORY_TOTAL     256
#define DEVICE_OBJECT_UTC_OFFSET       "+00:00"
#define DEVICE_OBJECT_TIMEZONE         "+513030-0000731" // London

// The interval at which we check for LWM2M server
// registration during startup.
#define CLOUD_CLIENT_REGISTRATION_CHECK_INTERVAL_MS 1000

// The threshold for low battery warning.
#define LOW_BATTERY_WARNING_PERCENTAGE 20

/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Initialise Mbed Cloud Client, its Device Management object
 * and all the other associated objects.
 *
 * @return  a pointer to the CloudClientDM object, or NULL on failure.
 */
CloudClientDm *pInitCloudClientDm();

/** Connect the Mbed Cloud Client to the server.
 *
 * @param   a pointer to a connected network interface.
 * @return  true if successful, otherwise false.
 */
bool connectCloudClientDm(NetworkInterface *pNetworkInterface);

/** Shut down Mbed Cloud Client and all objects.
 */
void deinitCloudClientDm();

/** Return whether the Cloud Client has connected.
 * @return true if it has connected, otherwise false.
 */
bool isCloudClientConnected();

/** Callback to update the observable values in all of the
 * LWM2M objects.
 */
void cloudClientObjectUpdate();

#endif // _IOC_CLOUD_CLIENT_DM_

// End of file
