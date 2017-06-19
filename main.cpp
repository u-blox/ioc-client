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
#include "UbloxATCellularInterface.h"
#include "UbloxPPPCellularInterface.h"

// If you wish to use LWIP and the PPP cellular interface on the mbed
// MCU, select the line UbloxPPPCellularInterface instead of the line
// UbloxATCellularInterface.  Using the AT cellular interface does not
// require LWIP and hence uses less RAM (significant on C027).  It also
// allows other AT command operations (e.g. sending an SMS) to happen
// during a data transfer.
//#define INTERFACE_CLASS  UbloxATCellularInterface
#define INTERFACE_CLASS  UbloxPPPCellularInterface

// The credentials of the SIM in the board.  If PIN checking is enabled
// for your SIM card you must set this to the required PIN.
#define PIN "0000"

// Network credentials.  You should set this according to your
// network/SIM card.  For C030 boards, leave the parameters as NULL
// otherwise, if you do not know the APN for your network, you may
// either try the fairly common "internet" for the APN (and leave the
// username and password NULL), or you may leave all three as NULL and then
// a lookup will be attempted for a small number of known networks
// (see APN_db.h in mbed-os/features/netsocket/cellular/utils).
#define APN         NULL
#define USERNAME    NULL
#define PASSWORD    NULL

// The name and port number of the UDP server to talk to
#define SERVER_NAME "ciot.it-sgn.u-blox.com"
#define SERVER_PORT 5065

// LEDs
DigitalOut ledRed(LED1, 1);
DigitalOut ledGreen(LED2, 1);
DigitalOut ledBlue(LED3, 1);

// The user button
volatile bool buttonPressed = false;

// Data to exchange
static const char sendData[] =   "_____0000:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0100:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0200:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0300:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0400:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0500:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0600:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0700:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0800:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0900:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1000:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789";

static void good() {
    ledGreen = 0;
    ledBlue = 1;
    ledRed = 1;
}

static void bad() {
    ledRed = 0;
    ledGreen = 1;
    ledBlue = 1;
}

static void event() {
    ledBlue = 0;
    ledRed = 1;
    ledGreen = 1;
}

static void pulseEvent() {
    event();
    wait_ms(500);
    good();
}

static void ledOff() {
    ledBlue = 1;
    ledRed = 1;
    ledGreen = 1;
}

static void cbButton()
{
    buttonPressed = true;
    pulseEvent();
}

/* This program for the u-blox C030 board instantiates the UbloxAtCellularInterface
 * or UbloxPPPCellularInterface and sends data to a UDP server using that connection.
 * Progress may be monitored with a serial terminal running at 115200 baud.
 * The LED on the C030 board will turn green when this program is
 * operating correctly, pulse blue when a sockets operation is completed
 * and turn red if there is a failure.
 */

int main()
{
    INTERFACE_CLASS *interface = new INTERFACE_CLASS();
    // If you need to debug the cellular interface, comment out the
    // instantiation above and uncomment the one below.
//    INTERFACE_CLASS *interface = new INTERFACE_CLASS(MDMTXD, MDMRXD,
//                                                     MBED_CONF_UBLOX_CELL_BAUD_RATE,
//                                                     true);
    UDPSocket sock;
    SocketAddress server;
    int count;
    InterruptIn userButton(SW0);
    
    // Attach a function to the user button
    userButton.rise(&cbButton);
    
    good();
    printf("Starting up, please wait up to 180 seconds for network registration to complete...\n");
    if (interface->init(PIN)) {
        pulseEvent();
        interface->set_credentials(APN, USERNAME, PASSWORD);
        printf("Registered, connecting to the packet network...\n");
        if (interface->connect() == 0) {
            pulseEvent();
            printf("Getting the IP address of \"%s\"...\n", SERVER_NAME);
            if (interface->gethostbyname(SERVER_NAME, &server) == 0) {
                pulseEvent();
                server.set_port(SERVER_PORT);
                printf("\"%s\" address: %s on port %d.\n", SERVER_NAME, server.get_ip_address(), server.get_port());
                printf("Opening a UDP socket...\n");
                if (sock.open(interface) == 0) {
                    pulseEvent();
                    printf("UDP socket open.\n");
                    sock.set_timeout(10000);
                    printf("Sending data in a loop until the user button is pressed...\n");
                    while (!buttonPressed) {
                        count = sock.sendto(server, (void *) sendData, 1024);
                        if (count > 0) {
                            printf("Sent %d byte(s).\n", count);
                        }
                    }            
                    pulseEvent();
                    printf("User button was pressed, stopping...\n");
                    sock.close();
                    interface->disconnect();
                    interface->deinit();
                    ledOff();
                    printf("Stopped.\n");
                } else {
                    bad();
                    printf("Unable to open socket to \"%s:%d\".\n", SERVER_NAME, SERVER_PORT);
                }
            } else {
                bad();
                printf("Unable to get IP address of \"%s\".\n", SERVER_NAME);
            }
        } else {
            bad();
            printf("Unable to make a data connection with the network.\n");
        }
    } else {
        bad();
        printf("Unable to initialise the interface.\n");
    }
}

// End Of File