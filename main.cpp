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
#include "OnboardCellularInterface.h"

#define PIN         "0000"
#define APN         "jtm2m"
#define USERNAME    NULL
#define PASSWORD    NULL

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

// Entry point
int main(void)
{
    OnboardCellularInterface *pInterface = new OnboardCellularInterface();
    UDPSocket sock;

    printf("Starting up, please wait up to 180 seconds to connect to the packet network...\n");
    pInterface->set_credentials(APN, USERNAME, PASSWORD);
    if (pInterface->connect() == 0) {
        if (sock.open(pInterface) == 0) {
            sock.set_timeout(10000);
        }
    }
    sock.close();
    pInterface->disconnect();
    printf("SUCCESS!!.\n");
}
