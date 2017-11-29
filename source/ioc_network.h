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

#ifndef _IOC_NETWORK_
#define _IOC_NETWORK_

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

#ifndef MBED_CONF_APP_MODEM_DEBUG_ON
// Modem debug prints.
#  define MBED_CONF_APP_MODEM_DEBUG_ON false
#endif

// The baud rate to use with the modem.
#define MODEM_BAUD_RATE 230400

/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Initialise the network interface, including connecting it.
 * @return  a pointer to NetworkInterface, or NULL on failure.
 */
NetworkInterface *pInitNetwork();

/** Shut down the network interface.
 */
void deinitNetwork();

/** Return whether the network is connected or not.
 * @return true if connected, otherwise false.
 */
bool isNetworkConnected();

/** Get a pointer to the network interface.
 * @return the network interface.
 */
NetworkInterface *pGetNetworkInterface();

#endif // _IOC_NETWORK_

// End of file
