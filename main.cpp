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
#include "I2S.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

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

// The audio sampling frequency in Hz
// This is for a stereo channel, effectively it is the
// frequency of the WS signal on the I2S interface, and
// so the mono rate is half this
#define STEREO_SAMPLING_FREQUENCY 16000

// The duration of a block in milliseconds
#define BLOCK_DURATION_MS 20

// The number of samples in a 20 ms block (stereo, so including L & R channels)
#define STEREO_SAMPLES_PER_BLOCK (STEREO_SAMPLING_FREQUENCY * BLOCK_DURATION_MS / 1000)

// I looked at using RTP to send data up to the server
// (https://en.wikipedia.org/wiki/Real-time_Transport_Protocol
// and https://tools.ietf.org/html/rfc3551, a minimal RTP header)
// but we have no time to do audio processing and we have 24
// bit audio at 8000 samples/second to send, which doesn't
// correspond with an RTP payload type anyway.  So instead we do
// something RTP-like, in that we send gTimeMillisecondsed datagrams at
// regular intervals. A 20 ms interval would contain a block
// of 160 samples, so 3840 bits, hence an overall data rate of
// 192 kbits/s is required.  We can do some silence detection to
// save bandwidth.
//
// The datagram format is:
//
// Byte  |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
//--------------------------------------------------------
//  0    |              protocol version = 0             |
//  1    |                    unused                     |
//  2    |             Sequence number MSB               |
//  3    |             Sequence number LSB               |
//  4    |                Timestamp MSB                  |
//  5    |                Timestamp byte                 |
//  6    |                Timestamp byte                 |
//  7    |                Timestamp LSB                  |
//--------------------------------------------------------
//  8    |                 Sample 1 MSB                  |
//  9    |                 Sample 1 byte                 |
//  10   |                 Sample 1 LSB                  |
//  11   |                 Sample 2 MSB                  |
//  12   |                 Sample 2 byte                 |
//  13   |                 Sample 2 LSB                  |
//       |                     ...                       |
//  N    |                 Sample M MSB                  |
//  N+1  |                 Sample M byte                 |
//  N+2  |                 Sample M LSB                  |
//
// ...where the number of samples is between 0 and 160,
// the gTimeMilliseconds is in milliseconds and the sequence number
// increments by one for each datagram.
//
// The receiving end should be able to reconstruct an audio
// stream from this gTimeMillisecondsed/ordered data.  For the sake
// of a name, call this URTP.

#define PROTOCOL_VERSION   0
#define URTP_HEADER_SIZE   8
#define URTP_SAMPLE_SIZE   3
#define URTP_BODY_SIZE     (URTP_SAMPLE_SIZE * 160)

// The maximum number of URTP datagrams that we need to store
// This is sufficient for 2 seconds of audio
#define MAX_NUM_DATAGRAMS 100

// If 1 then use the right channel for our mono stream,
// otherwise set to 0 to use the left channel
#define USE_RIGHT_NOT_LEFT 0

// Macro to retrieve a byte of a 24 bit audio sample from
// its 32 bit representation in Philips protocol format.
// From the STM32F437 manual (section 28.4.3, figure 265),
// if 0x8EAA33 is received, the DMA will read 0x8EAA then
// 0x33XX, where XX can be anything.
#define RAW_AUDIO_BYTE_LSB(x) ((char) ((x) >> 8))
#define RAW_AUDIO_BYTE_OSB(x) ((char) ((x) >> 16))
#define RAW_AUDIO_BYTE_MSB(x) ((char) ((x) >> 24))

// A signal to indicate that a datagram is ready to send
#define SIG_DATAGRAM_READY 0x01

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// A URTP datagram
typedef struct UrtpDatagramTag {
    uint16_t sequenceNumber;
    uint32_t timeMilliseconds;
    char body[URTP_BODY_SIZE];
} UrtpDatagram;

// A linked list container
typedef struct ContainerTag {
    bool inUse;
    void * pContents;
    ContainerTag *pNext;
} Container;

// A struct to pass data to the task that sends data to the network
typedef struct {
    UDPSocket * pSock;
    SocketAddress * pServer;
} SendParams;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Thread required by I2S driver task
static Thread gI2sTask;

// Function that forms the body of the gI2sTask
static Callback<void()> gI2STaskCallback(&I2S::i2s_bh_queue, &events::EventQueue::dispatch_forever);

// Audio buffer, enough for 2 blocks of stereo audio,
// where each sample takes up 32 bits.
static uint32_t gRawAudio[STEREO_SAMPLES_PER_BLOCK * 2];

// Task to send data off to the network server
static Thread gSendTask;

// Array of our "RTP-like" datagrams.
__attribute__ ((section ("CCMRAM")))
static UrtpDatagram gDatagram[MAX_NUM_DATAGRAMS];

// A linked list to manage the datagrams
__attribute__ ((section ("CCMRAM")))
static Container gContainer[sizeof (gDatagram) / sizeof (gDatagram[0])];

// A sequence number
static int gSequenceNumber = 0;

// A millisecond timestamp
static Timer gTimeMilliseconds;

// Pointer to the next unused container
static Container *gpContainerNextEmpty = gContainer;

// Pointer to the next container to transmit
static volatile Container *gpContainerNextTx = gContainer;

// A buffer to hold an encoded URTP datagram
static char gSendBuf[URTP_HEADER_SIZE + URTP_BODY_SIZE];

// Control output to ICS43434 chip, telling it which
// channel to put the mono samples in
static DigitalOut gLeftRight(D8, USE_RIGHT_NOT_LEFT);

// A UDP socket
static UDPSocket gSock;

// A server address
static SocketAddress gServer;

// LEDs
static DigitalOut gLedRed(LED1, 1);
static DigitalOut gLedGreen(LED2, 1);
static DigitalOut gLedBlue(LED3, 1);

// The user button
static volatile bool gButtonPressed = false;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Indicate an event (blue)
static void event() {
    gLedBlue = 0;
    gLedGreen = 1;
    gLedRed = 1;
}

// Indicate good (green)
static void good() {
    gLedGreen = 0;
    gLedBlue = 1;
    gLedRed = 1;
}

// Indicate bad (red)
static void bad() {
    gLedRed = 0;
    gLedGreen = 1;
    gLedBlue = 1;
}

// Toggle green
static void toggleGreen() {
    gLedGreen = !gLedGreen;
}

// All off
static void ledOff() {
    gLedBlue = 1;
    gLedRed = 1;
    gLedGreen = 1;
}

// Something to attach to the button
static void buttonCallback()
{
    gButtonPressed = true;
    event();
}

// Initialise the linked list
static void initContainers(Container * pContainer, int numContainers)
{
    Container *pTmp;
    Container *pStart = pContainer;

    for (int x = 0; x < numContainers; x++) {
        pContainer->inUse = false;
        pContainer->pContents = (void *) &(gDatagram[x]);
        pContainer->pNext = NULL;
        pTmp = pContainer;
        pContainer++;
        pTmp->pNext = pContainer;
    }
    // Handle the final pNext, make the list circular
    pTmp->pNext = pStart;
}

// Fill a datagram with the audio from one block.
// pRawAudio must point to an array of STEREO_SAMPLES_PER_BLOCK
// uint32_t's.  Only the samples from the mono channel
// we are using are copied
static void fillMonoDatagramFromBlock(uint32_t *pRawAudio)
{
    bool isOdd = false;
    UrtpDatagram * pDatagram = (UrtpDatagram *) gpContainerNextEmpty->pContents;
    char * pBody = &(pDatagram->body[0]);

    // Check for overrun (nothing we can do, the oldest will just be overwritten)
    if (gpContainerNextEmpty->inUse) {
        bad();
    }
    toggleGreen();
    gpContainerNextEmpty->inUse = true;

    // Copy in the body
    for (uint32_t *pSample = pRawAudio; pSample < (pRawAudio + STEREO_SAMPLES_PER_BLOCK); pSample++) {
        if (isOdd) {
#if USE_RIGHT_NOT_LEFT == 1
            // Sample is in second(i.e. odd) uint32_t
            *pBody = RAW_AUDIO_BYTE_MSB(*pSample);
            pBody++;
            *pBody = RAW_AUDIO_BYTE_OSB(*pSample);
            pBody++;
            *pBody = RAW_AUDIO_BYTE_LSB(*pSample);
            pBody++;
#endif
        } else {
#if USE_RIGHT_NOT_LEFT == 0
            // Sample is in first (i.e. even) uint32_t
            *pBody = RAW_AUDIO_BYTE_MSB(*pSample);
            pBody++;
            *pBody = RAW_AUDIO_BYTE_OSB(*pSample);
            pBody++;
            *pBody = RAW_AUDIO_BYTE_LSB(*pSample);
            pBody++;
#endif
        }
        isOdd = !isOdd;
    }

    MBED_ASSERT (pBody <= (pDatagram->body + sizeof (pDatagram->body)));

    // Fill in the header
    pDatagram->sequenceNumber = gSequenceNumber;
    gSequenceNumber++;
    pDatagram->timeMilliseconds = gTimeMilliseconds.read_ms();

    // Rotate to the next empty container
    gpContainerNextEmpty = gpContainerNextEmpty->pNext;

    // Signal that a datagram is ready to send
    gSendTask.signal_set(SIG_DATAGRAM_READY);
}

// Callback for I2S events.
// We get here when the DMA has either half-filled
// the gRawAudio buffer (so one 20 ms block) or
// completely filled it (two 20 ms blocks), or if
// an error has occurred.  We can use the non-error
// calls as a sort of double buffer.
static void i2sEventCallback (int arg)
{
    if (arg & I2S_EVENT_RX_HALF_COMPLETE) {
        fillMonoDatagramFromBlock(gRawAudio);
        good();
    } else if (arg & I2S_EVENT_RX_COMPLETE) {
        fillMonoDatagramFromBlock(gRawAudio + sizeof (gRawAudio) / 2);
        good();
    } else {
        bad();
        printf("Unexpected event mask 0x%08x.\n", arg);
    }
}

// Initialise the I2S interface and begin reading from it.
// The ICS43434 microphone outputs 24 bit words in a 64 bit frame,
// with the LR pin dictating whether the word appears in the first
// 32 bits (LR = 0, left channel, WS low) or the second 32 bits
// (LR = 1, right channel, WS high).  Each data bit is valid on the
// rising edge of SCK and the MSB of the data word is clocked out on the
// second clock edge after WS changes, as follows:
//      ___                                 ______________________   ___
// WS      \____________...________..._____/                      ...   \______
//          0   1   2       23  24      31  32  33  34     55  56     63
// SCK  ___   _   _   _       _   _      _   _   _   _       _   _      _   _
//         \_/ \_/ \_/ \...\_/ \_/ ...\_/ \_/ \_/ \_/ \...\_/ \_/ ...\_/ \_/ \_
//
// SD   ________--- ---     --- --- ___________--- ---     --- ---_____________
//              --- --- ... --- ---            --- --- ... --- ---
//              23  22       1   0             23  22       1   0
//              Left channel data              Right channel data
//
// This is known as the Philips protocol (24-bit frame with CPOL = 0).
static bool startI2s(I2S * pI2s)
{
    bool success = false;

    if ((pI2s->protocol(PHILIPS) == 0) &&
        (pI2s->mode(MASTER_RX, true) == 0) &&
        (pI2s->format(24, 32, 0) == 0) &&
        (pI2s->audio_frequency(STEREO_SAMPLING_FREQUENCY) == 0)) {
        if (gI2sTask.start(gI2STaskCallback) == osOK) {
            gTimeMilliseconds.reset();
            gTimeMilliseconds.start();
            if (pI2s->transfer((void *) NULL, 0,
                               (void *) gRawAudio, sizeof (gRawAudio),
                               event_callback_t(&i2sEventCallback),
                               I2S_EVENT_ALL) == 0) {
                success = true;
            }
        }
    }

    return success;
}

// Stop the I2S interface
static void stopI2s(I2S * pI2s)
{
    pI2s->abort_all_transfers();
    gI2sTask.terminate();
    gTimeMilliseconds.stop();
}

// Connect to the network, returning a pointer to a socket or NULL
static UDPSocket * startNetwork(INTERFACE_CLASS * pInterface)
{
    UDPSocket *pSock = NULL;

    if (pInterface->init(PIN)) {
        pInterface->set_credentials(APN, USERNAME, PASSWORD);
        if (pInterface->connect() == 0) {
            if (gSock.open(pInterface) == 0) {
                gSock.set_timeout(10000);
                pSock = &gSock;
            }
        }
    }

    return pSock;
}

// Disconnect from the network
static void stopNetwork(INTERFACE_CLASS * pInterface)
{
    gSock.close();
    pInterface->disconnect();
    pInterface->deinit();
}

// Verify that the server is there
static SocketAddress * verifyServer(INTERFACE_CLASS * pInterface)
{
    SocketAddress *pServer = NULL;

    if (pInterface->gethostbyname(SERVER_NAME, &gServer) == 0) {
        gServer.set_port(SERVER_PORT);
        pServer = &gServer;
    }

    return pServer;
}

// The send function that forms the body of the send task
// This task runs whenever there is a datagram ready to send
static void sendData(const SendParams * pSendParams)
{
    UrtpDatagram * pDatagram;
    gSendBuf[0] = PROTOCOL_VERSION;
    gSendBuf[1] = 0;

    MBED_ASSERT (pSendParams != NULL);
    MBED_ASSERT (pSendParams->pSock != NULL);
    MBED_ASSERT (pSendParams->pServer != NULL);

    while (1) {
        // Wait for a datagram to be ready to send
        Thread::signal_wait(SIG_DATAGRAM_READY);

        MBED_ASSERT (gpContainerNextTx != NULL);
        MBED_ASSERT (gpContainerNextTx->inUse == true);

        // Take the datagram and move the pointer on
        pDatagram = (UrtpDatagram *) (gpContainerNextTx->pContents);
        gpContainerNextTx->inUse = false;
        gpContainerNextTx = gpContainerNextTx->pNext;

        // Fill in the header
        gSendBuf[2] = (char) (pDatagram->sequenceNumber >> 8);
        gSendBuf[3] = (char) (pDatagram->sequenceNumber);
        gSendBuf[4] = (char) (pDatagram->timeMilliseconds >> 24);
        gSendBuf[5] = (char) (pDatagram->timeMilliseconds >> 16);
        gSendBuf[6] = (char) (pDatagram->timeMilliseconds >> 8);
        gSendBuf[7] = (char) (pDatagram->timeMilliseconds);

        // Copy in the body
        memcpy (gSendBuf + URTP_HEADER_SIZE, pDatagram->body, sizeof (gSendBuf) - URTP_HEADER_SIZE);

        // ...and send the datagram
        if (pSendParams->pSock->sendto(*(pSendParams->pServer), gSendBuf, sizeof (gSendBuf)) != sizeof (gSendBuf)) {
            bad();
        }
    }
}

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

// Entry point
int main(void)
{
    INTERFACE_CLASS *pInterface = new INTERFACE_CLASS(MDMTXD, MDMRXD, 115200);
    I2S *pMic = new I2S(PB_15, PB_10, PB_9);
    SendParams sendParams;
    InterruptIn userButton(SW0);

    // Attach a function to the user button
    userButton.rise(&buttonCallback);
    
    // Initialise the linked list
    initContainers(gContainer, sizeof (gContainer) / sizeof (gContainer[0]));

    good();
    printf("Starting up, please wait up to 180 seconds to connect to the packet network...\n");
    sendParams.pSock = startNetwork(pInterface);
    if (sendParams.pSock != NULL) {
        sendParams.pSock->set_timeout(10000);

        printf("Verifying that the server is there...\n");
        sendParams.pServer = verifyServer(pInterface);
        if (sendParams.pServer != NULL) {

            printf ("Starting task to send data to the server...\n");
            if (gSendTask.start(callback(sendData, &sendParams)) == osOK) {

                printf("Reading from I2S and sending to server until the user button is pressed...\n");
                if (startI2s(pMic)) {

                    while (!gButtonPressed);
                    printf("User button pressed, stopping...\n");
                    stopI2s(pMic);
                    // This commented out as it doesn't seem to ever return for me
                    //gSendTask.terminate();
                    stopNetwork(pInterface);
                    ledOff();
                    printf("Stopped.\n");
                } else {
                    bad();
                    printf("Unable to start reading from I2S.\n");
                }
            } else {
                bad();
                printf("Unable to start start sending task.\n");
            }
        } else {
            bad();
            printf("Unable to locate server.\n");
        }
    } else {
        bad();
        printf("Unable to connect to the network and open a socket.\n");
    }
}
