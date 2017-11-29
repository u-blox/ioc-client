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

#ifndef _IOC_LOGGING_
#define _IOC_LOGGING_

/* ----------------------------------------------------------------
 * FUNCTION PROTOTYPES
 * -------------------------------------------------------------- */

/** Return whether logging to file is enabled or not.
 * @return true if logging to file is enabled, otherwise false.
 */
bool isLoggingToFileEnabled();

/** Return whether log file uploading is enabled or not.
 * @return true if log file uploading is enabled, otherwise false.
 */
bool isLoggingUploadEnabled();

/** Return the URL of the logging server.
 * @return a pointer to a null terminated string that is the logging
 *         server URL.
 */
const char *pGetLoggingServerUrl();

#endif // _IOC_LOGGING_

// End of file
