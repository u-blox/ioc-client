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

#include "ioc_logging.h"
#include "ioc_utils.h"

/* This file implements the control of logging.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The default logging setup data.
#define LOGGING_DEFAULT_TO_FILE_ENABLED true
#define LOGGING_DEFAULT_UPLOAD_ENABLED  true
#define LOGGING_DEFAULT_SERVER_URL      "ciot.it-sgn.u-blox.com:5060"

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// The logging configuration.
typedef struct {
    bool loggingToFileEnabled;
    bool loggingUploadEnabled;
    String loggingServerUrl;
} LoggingLocal;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

static LoggingLocal gLoggingLocal = {LOGGING_DEFAULT_TO_FILE_ENABLED,
                                     LOGGING_DEFAULT_TO_FILE_ENABLED,
                                     LOGGING_DEFAULT_SERVER_URL};

/* ----------------------------------------------------------------
 * PUBLIC: INITIALISATION
 * -------------------------------------------------------------- */

// Return whether loggin to file is enabled or not.
bool isLoggingToFileEnabled()
{
    return gLoggingLocal.loggingToFileEnabled;
}

// Return whether log file uploading is enabled or not.
bool isLoggingUploadEnabled()
{
    return gLoggingLocal.loggingUploadEnabled;
}

// Get the logging server URL as a null terminated string.
const char *pGetLoggingServerUrl()
{
    return gLoggingLocal.loggingServerUrl.c_str();
}

// End of file
