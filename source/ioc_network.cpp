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
#include "log.h"

#include "ioc_utils.h"
#include "ioc_network.h"

/* This file implements cellular network connectivity.
 */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// The network interface.
UbloxPPPCellularInterface *gpCellular = NULL;

/* ----------------------------------------------------------------
 * PUBLIC: INITIALISATION
 * -------------------------------------------------------------- */

// Initialise the network connection.
// Note: here be multiple return statements.
NetworkInterface *pInitNetwork()
{
    Ticker *pSecondTicker = NULL;

    flash();
    LOG(EVENT_MODEM_START, 0);
    printf("Initialising modem...\n");
    gpCellular = new UbloxPPPCellularInterface(MDMTXD, MDMRXD, MODEM_BAUD_RATE,
                                               MBED_CONF_APP_MODEM_DEBUG_ON);
    if (!gpCellular->init()) {
        delete gpCellular;
        gpCellular = NULL;
        bad();
        LOG(EVENT_MODEM_START_FAILURE, 0);
        printf("Unable to initialise cellular.\n");
        return gpCellular;
    }

    // Run a ticker to feed the watchdog while we wait for registration
    pSecondTicker = new Ticker();
    pSecondTicker->attach_us(callback(&feedWatchdog), 1000000);

    flash();
    LOG(EVENT_NETWORK_CONNECTING, 0);
    printf("Please wait up to 180 seconds to connect to the cellular packet network...\n");
    if (gpCellular->connect() != NSAPI_ERROR_OK) {
        delete gpCellular;
        gpCellular = NULL;
        pSecondTicker->detach();
        delete pSecondTicker;
        bad();
        LOG(EVENT_NETWORK_CONNECTION_FAILURE, 0);
        printf("Unable to connect to the cellular packet network.\n");
        return gpCellular;
    }
    pSecondTicker->detach();
    delete pSecondTicker;
    LOG(EVENT_NETWORK_CONNECTED, 0);

    return (NetworkInterface *) gpCellular;
}

// Shut down the network.
void deinitNetwork()
{
    if (gpCellular != NULL) {
        feedWatchdog();
        flash();
        LOG(EVENT_NETWORK_DISCONNECTING, 0);
        printf("Disconnecting from the cellular packet network...\n");
        gpCellular->disconnect();
        flash();
        LOG(EVENT_NETWORK_DISCONNECTED, 0);
        LOG(EVENT_MODEM_STOP, 0);
        printf("Stopping modem...\n");
        gpCellular->deinit();
        delete gpCellular;
        gpCellular = NULL;
    }
}

// Return whether the network is connected or not.
bool isNetworkConnected()
{
    return (gpCellular != NULL) && gpCellular->is_connected();
}

// Return the network interface;
NetworkInterface *pGetNetworkInterface()
{
    return (NetworkInterface *) gpCellular;
}

// End of file
