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
#include "log.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// How long to wait between flushes to file.
#define LOGGING_NUM_WRITES_BEFORE_FLUSH 10

// The maximum length of a file name with path.
#define LOGGING_MAX_LEN_FILE_PATH 64

// The maximum length of the URL of the logging server (including port).
#define LOGGING_MAX_LEN_SERVER_URL 128

// The TCP buffer size for log file uploads.
// NOTE: this is on the thread's stack (gotta use those 500 bytes for
// something) so don't make it too big.
#define LOGGING_TCP_BUFFER_SIZE 265

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// To obtain file system errors.
extern int errno;

// The strings associated with the enum values.
extern const char *gLogStrings[];
extern const int gNumLogStrings;

// Mutex to arbitrate logging.
// The callback which writes logging to disk
// will attempt to lock this mutex while the
// function that prints out the log owns the
// mutex. Note that the logging functions
// themselves shouldn't wait on it (they have
// no reason to as the buffering should
// handle any overlap); they MUST return quickly.
static Mutex logMutex;

// The number of calls to writeLog().
static int numWrites = 0;

// A logging buffer.
static LogEntry *gpLog = NULL;
static LogEntry *gpLogNextEmpty = NULL;
static LogEntry const *gpLogFirstFull = NULL;

// A logging timestamp.
static Timer gLogTime;

// A file to write logs to.
static FILE *gpFile = NULL;

// A pointer to the name of the partition.
static const char *gpPartition = NULL;

// A pointer to the directory where log files are stored.
static Dir *gpDir = NULL;

// The name of the current log file.
static char gCurrentLogFileNameBuffer[LOGGING_MAX_LEN_FILE_PATH];

// The address of the logging server.
static SocketAddress *gpLoggingServer = NULL;

// A TCP socket to the above server.
static TCPSocket *gpTcpSock = NULL;

// A thread to run the log upload process.
static Thread *gpLogUploadThread = NULL;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Print a single item from a log.
void printLogItem(const LogEntry *pItem, unsigned int itemIndex)
{
    if (pItem->event > gNumLogStrings) {
        printf("%.3f: out of range event at entry %d (%d when max is %d)\n",
               (float) pItem->timestamp / 1000, itemIndex, pItem->event, gNumLogStrings);
    } else {
        printf ("%6.3f: %s %d (%#x)\n", (float) pItem->timestamp / 1000,
                gLogStrings[pItem->event], pItem->parameter, pItem->parameter);
    }

}

// Open a log file, storing its name in gCurrentLogFileNameBuffer
// and returning a handle to it.
FILE * newLogFile(const char * pPartition)
{
    FILE *pFile = NULL;

    if (strlen(pPartition) < sizeof (gCurrentLogFileNameBuffer) - 11) {
        // 11 above is for two file separators, "xxxx.log" and a null terminator
        // (see sprintf below)
        // BE CAREFUL if you change the filename format as the
        // beginLogFileUpload() function expects this exact format
        for (unsigned int x = 0; (x < 1000) && (pFile == NULL); x++) {
            sprintf(gCurrentLogFileNameBuffer, "/%s/%04d.log", pPartition, x);
            // Try to open the file to see if it exists
            pFile = fopen(gCurrentLogFileNameBuffer, "r");
            // If it doesn't exist, use it, otherwise close
            // it and go around again
            if (pFile == NULL) {
                printf("Log file will be \"%s\".\n", gCurrentLogFileNameBuffer);
                pFile = fopen (gCurrentLogFileNameBuffer, "wb+");
                if (pFile != NULL) {
                    LOG(EVENT_FILE_OPEN, 0);
                } else {
                    LOG(EVENT_FILE_OPEN_FAILURE, errno);
                    perror ("Error initialising log file");
                }
            } else {
                fclose(pFile);
                pFile = NULL;
            }
        }
    }

    return pFile;
}

// Get the address portion of a URL, leaving off the port number etc.
static void getAddressFromUrl(const char * pUrl, char * pAddressBuf, int lenBuf)
{
    const char * pPortPos;
    int lenUrl;

    if (lenBuf > 0) {
        // Check for the presence of a port number
        pPortPos = strchr(pUrl, ':');
        if (pPortPos != NULL) {
            // Length wanted is up to and including the ':'
            // (which will be overwritten with the terminator)
            if (lenBuf > pPortPos - pUrl + 1) {
                lenBuf = pPortPos - pUrl + 1;
            }
        } else {
            // No port number, take the whole thing
            // including the terminator
            lenUrl = strlen (pUrl);
            if (lenBuf > lenUrl + 1) {
                lenBuf = lenUrl + 1;
            }
        }
        memcpy (pAddressBuf, pUrl, lenBuf);
        *(pAddressBuf + lenBuf - 1) = 0;
    }
}

// Get the port number from the end of a URL.
static bool getPortFromUrl(const char * pUrl, int *port)
{
    bool success = false;
    const char * pPortPos = strchr(pUrl, ':');

    if (pPortPos != NULL) {
        *port = atoi(pPortPos + 1);
        success = true;
    }

    return success;
}

// Function to sit in a thread and upload log files.
void logFileUploadCallback(const char *pCurrentLogFile)
{
    nsapi_error_t nsapiError;
    int x;
    int y = 0;
    int z;
    int numFiles;
    struct dirent dirEnt;
    FILE *pFile = NULL;
    int sendCount;
    int sendTotalThisFile;
    int size;
    char readBuffer[LOGGING_TCP_BUFFER_SIZE];
    char fileNameBuffer[LOGGING_MAX_LEN_FILE_PATH];

    // Socket is open, send those log files, using a different
    // TCP connection for each one so that the logging server
    // stores them in separate files
    numFiles = gpDir->size();
    gpDir->rewind();
    for (x = 0; x < numFiles; x++) {
        if (gpDir->read(&dirEnt) == 1) {
            // Read the entries in the directory
            if ((dirEnt.d_type == DT_REG) &&
                ((pCurrentLogFile == NULL) || (strcmp(dirEnt.d_name, pCurrentLogFile) != 0))) {
                y++;
                LOG(EVENT_TCP_CONNECTING, 0);
                nsapiError = gpTcpSock->connect(*gpLoggingServer);
                if (nsapiError == NSAPI_ERROR_OK) {
                    LOG(EVENT_TCP_CONNECTED, 0);
                    LOG(EVENT_LOG_UPLOAD_STARTING, y);
                    // Open the file, provided it's not the one we're currently logging to
                    sprintf(fileNameBuffer, "/%s/%s", gpPartition, dirEnt.d_name);
                    pFile = fopen(fileNameBuffer, "r");
                    if (pFile != NULL) {
                        LOG(EVENT_FILE_OPEN, 0);
                        sendTotalThisFile = 0;
                        do {
                            // Read the file and send it
                            size = fread(readBuffer, 1, sizeof (readBuffer), pFile);
                            sendCount = 0;
                            while (sendCount < size) {
                                z = gpTcpSock->send(readBuffer + sendCount, size - sendCount);
                                if (z > 0) {
                                    sendCount += z;
                                    sendTotalThisFile += z;
                                }
                            }
                            LOG(EVENT_LOG_FILE_BYTE_COUNT, sendTotalThisFile);
                        } while (size > 0);
                        LOG(EVENT_LOG_FILE_UPLOAD_COMPLETED, y);

                        // The file has now been sent, so close the socket
                        LOG(EVENT_FILE_CLOSE, 0);
                        gpTcpSock->close();

                        // Delete the file, so that we don't try to send it again
                        if (remove(fileNameBuffer) == 0) {
                            LOG(EVENT_FILE_DELETED, 0);
                        } else {
                            LOG(EVENT_FILE_DELETE_FAILURE, 0);
                        }
                    } else {
                        LOG(EVENT_FILE_OPEN_FAILURE, 0);
                    }
                } else {
                    LOG(EVENT_TCP_CONNECT_FAILURE, nsapiError);
                }
            }
        }
    }

    LOG(EVENT_LOG_ALL_UPLOADS_COMPLETED, 0);

    // Clear up
    delete gpTcpSock;
    gpTcpSock = NULL;
    delete gpLoggingServer;
    gpLoggingServer = NULL;
    delete gpDir;
    gpDir = NULL;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise logging.
bool initLog(void *pBuffer, const char *pPartition)
{
    gLogTime.reset();
    gLogTime.start();
    gpLog = (LogEntry * ) pBuffer;
    gpLogNextEmpty = gpLog;
    gpLogFirstFull = gpLog;
    LOG(EVENT_LOG_START, LOG_VERSION);

    if (pPartition != NULL) {
        gpPartition = pPartition;
        gpFile = newLogFile(pPartition);
    }

    return (pPartition == NULL) || (gpFile != NULL);
}

// Initialise the log file.
bool initLogFile(const char *pPartition)
{
    if (gpFile == NULL) {
        gpPartition = pPartition;
        gpFile = newLogFile(pPartition);
    }

    return (gpFile != NULL);
}

// Upload previous log files.
bool beginLogFileUpload(FATFileSystem *pFileSystem,
                        NetworkInterface *pNetworkInterface,
                        const char *pLoggingServerUrl,
                        const char *pPath)
{
    bool success = false;
    char *pBuf = new char[LOGGING_MAX_LEN_SERVER_URL];
    int port;
    nsapi_error_t nsapiError;
    int x;
    int y;
    int z = 0;
    struct dirent dirEnt;
    const char * pCurrentLogFile = NULL;

    if (gpLogUploadThread == NULL) {
        // First, determine if there are any log files to be uploaded.
        if (gpPartition != NULL) {
            gpDir = new Dir();
            if (gpDir != NULL) {
                x = gpDir->open(pFileSystem, pPath);
                if (x == 0) {
                    printf("Checking for log files to upload...\n");
                    // Point to the name portion of the current log file
                    // (format "/*/xxxx.log")
                    pCurrentLogFile = strstr(gCurrentLogFileNameBuffer, ".log");
                    if (pCurrentLogFile != NULL) {
                        pCurrentLogFile -= 4; // Point to the start of the file name
                    }
                    do {
                        y = gpDir->read(&dirEnt);
                        if ((y == 1) && (dirEnt.d_type == DT_REG) &&
                            ((pCurrentLogFile == NULL) || (strcmp(dirEnt.d_name, pCurrentLogFile) != 0))) {
                            z++;
                        }
                    } while (y > 0);

                    LOG(EVENT_LOG_FILES_TO_UPLOAD, z);
                    printf("%d log files to upload.\n", z);

                    if (z > 0) {
                        gpLoggingServer = new SocketAddress();
                        getAddressFromUrl(pLoggingServerUrl, pBuf, LOGGING_MAX_LEN_SERVER_URL);
                        LOG(EVENT_DNS_LOOKUP, 0);
                        printf("Looking for logging server URL \"%s\"...\n", pBuf);
                        if (pNetworkInterface->gethostbyname(pBuf, gpLoggingServer) == 0) {
                            printf("Found it at IP address %s.\n", gpLoggingServer->get_ip_address());
                            if (getPortFromUrl(pLoggingServerUrl, &port)) {
                                gpLoggingServer->set_port(port);
                                printf("Logging server port set to %d.\n", gpLoggingServer->get_port());
                            } else {
                                printf("WARNING: no port number was specified in the logging server URL (\"%s\").\n",
                                        pLoggingServerUrl);
                            }
                        } else {
                            LOG(EVENT_DNS_LOOKUP_FAILURE, 0);
                            printf("Unable to locate logging server \"%s\".\n", pLoggingServerUrl);
                        }

                        printf("Opening socket to logging server...\n");
                        LOG(EVENT_SOCKET_OPENING, 0);
                        gpTcpSock = new TCPSocket();
                        nsapiError = gpTcpSock->open(pNetworkInterface);
                        if (nsapiError == NSAPI_ERROR_OK) {
                            LOG(EVENT_SOCKET_OPENED, 0);
                            gpTcpSock->set_timeout(1000);
                            // Socket is open, start a thread to upload the log files
                            // in the background
                            gpLogUploadThread = new Thread();
                            if (gpLogUploadThread != NULL) {
                                if (gpLogUploadThread->start(callback(logFileUploadCallback, pCurrentLogFile)) == osOK) {
                                    printf("Log File upload background thread running.\n");
                                    success = true;
                                } else {
                                    printf("Unable to start thread to upload files to logging server.\n");
                                }
                            } else {
                                printf("Unable to instantiate thread to upload files to logging server (error %d).\n", nsapiError);
                            }
                        } else {
                            LOG(EVENT_SOCKET_OPENING_FAILURE, nsapiError);
                            printf("Unable to open socket to logging server (error %d).\n", nsapiError);
                        }
                    } else {
                        success = true; // Nothing to do
                    }
                } else {
                    printf("Unable to open partition \"%s\" (error %d).\n", gpPartition, x);
                }
            } else {
                printf("Unable to instantiate directory object for partition \"%s\".\n", gpPartition);
            }
        } else {
            printf("Tried to open file system for log uploads but gpPartition is not yet set.\n");
        }
    } else {
        printf("Log upload thread already running.\n");
    }

    return success;
}

// Stop uploading previous log files, returning memory.
void stopLogFileUpload()
{
    if (gpLogUploadThread != NULL) {
        gpLogUploadThread->terminate();
        gpLogUploadThread->join();
        delete gpLogUploadThread;
        gpLogUploadThread = NULL;
    }

    if (gpTcpSock != NULL) {
        delete gpTcpSock;
        gpTcpSock = NULL;
    }

    if (gpLoggingServer != NULL) {
        delete gpLoggingServer;
        gpLoggingServer = NULL;
    }

    if (gpDir != NULL) {
        delete gpDir;
        gpDir = NULL;
    }
}

// Close down logging.
void deinitLog()
{
    stopLogFileUpload(); // Just in case

    LOG(EVENT_LOG_STOP, LOG_VERSION);
    if (gpFile != NULL) {
        LOG(EVENT_FILE_CLOSE, 0);
        fclose(gpFile);
        gpFile = NULL;
    }

    // Don't reset the variables
    // here so that printLog() still
    // works afterwards if we're just
    // logging to RAM rather than
    // to file.
}

// Log an event plus parameter.
void LOG(LogEvent event, int parameter)
{
    if (gpLogNextEmpty) {
        gpLogNextEmpty->timestamp = gLogTime.read_us();
        gpLogNextEmpty->event = event;
        gpLogNextEmpty->parameter = parameter;
        if (gpLogNextEmpty < gpLog + MAX_NUM_LOG_ENTRIES - 1) {
            gpLogNextEmpty++;
        } else {
            gpLogNextEmpty = gpLog;
        }

        if (gpLogNextEmpty == gpLogFirstFull) {
            // Logging has wrapped, so move the
            // first pointer on to reflect the
            // overwrite
            if (gpLogFirstFull < gpLog + MAX_NUM_LOG_ENTRIES - 1) {
                gpLogFirstFull++;
            } else {
                gpLogFirstFull = gpLog;
            }
        }
    }
}

// Flush the log file.
// Note: log file mutex must be locked before calling.
void flushLog()
{
    if (gpFile != NULL) {
        fclose(gpFile);
        LOG(EVENT_FILE_CLOSE, 0);
        gpFile = fopen(gCurrentLogFileNameBuffer, "wb+");
        if (gpFile) {
            LOG(EVENT_FILE_OPEN, 0);
        } else {
            LOG(EVENT_FILE_OPEN_FAILURE, errno);
        }
    }
}

// This should be called periodically to write the log
// to file, if a filename was provided to initLog().
void writeLog()
{
    if (logMutex.trylock()) {
        if (gpFile != NULL) {
            while (gpLogNextEmpty != gpLogFirstFull) {
                fwrite(gpLogFirstFull, sizeof(LogEntry), 1, gpFile);
                numWrites++;
                if (numWrites > LOGGING_NUM_WRITES_BEFORE_FLUSH) {
                    numWrites = 0;
                    flushLog();
                }
                if (gpLogFirstFull < gpLog + MAX_NUM_LOG_ENTRIES - 1) {
                    gpLogFirstFull++;
                } else {
                    gpLogFirstFull = gpLog;
                }
            }
        }
        logMutex.unlock();
    }
}

// Print out the log.
void printLog()
{
    const LogEntry *pItem = gpLogNextEmpty;
    LogEntry fileItem;
    bool loggingToFile = false;
    FILE *pFile = gpFile;
    unsigned int x = 0;

    logMutex.lock();
    printf ("------------- Log starts -------------\n");
    if (gpFile != NULL) {
        // If we were logging to file, read it back
        // First need to flush the file to disk
        loggingToFile = true;
        fclose(gpFile);
        gpFile = NULL;
        LOG(EVENT_FILE_CLOSE, 0);
        pFile = fopen(gCurrentLogFileNameBuffer, "rb");
        if (pFile != NULL) {
            LOG(EVENT_FILE_OPEN, 0);
            while (fread(&fileItem, sizeof(fileItem), 1, pFile) == 1) {
                printLogItem(&fileItem, x);
                x++;
            }
            // If we're not at the end of the file, there must have been an error
            if (!feof(pFile)) {
                perror ("Error reading portion of log stored in file system");
            }
            fclose(pFile);
            LOG(EVENT_FILE_CLOSE, 0);
        } else {
            perror ("Error opening portion of log stored in file system");
        }
    }

    // Print the log items remaining in RAM
    pItem = gpLogFirstFull;
    x = 0;
    while (pItem != gpLogNextEmpty) {
        printLogItem(pItem, x);
        x++;
        pItem++;
        if (pItem >= gpLog + MAX_NUM_LOG_ENTRIES) {
            pItem = gpLog;
        }
    }

    // Allow writeLog() to resume with the same file name
    if (loggingToFile) {
        gpFile = fopen(gCurrentLogFileNameBuffer, "wb+");
        if (gpFile) {
            LOG(EVENT_FILE_OPEN, 0);
        } else {
            LOG(EVENT_FILE_OPEN_FAILURE, errno);
            perror ("Error initialising log file");
        }
    }

    printf ("-------------- Log ends --------------\n");
    logMutex.unlock();
}

// End of file
