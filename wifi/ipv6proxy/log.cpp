/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "log.h"

#ifndef ANDROID
#include <stdarg.h>
#ifdef USE_LOG_TIMESTAMPS
#include <time.h>
#endif  // USE_LOG_TIMESTAMPS

static void vlogf(FILE* stream, const char* fmt, va_list args) {
#ifdef USE_LOG_TIMESTAMPS
    struct timespec ts;
    struct tm localTime;
    static bool newLine = true;
    if (newLine && clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        time_t now = ts.tv_sec;
        if (localtime_r(&now, &localTime)) {
            char format[32];
            char timestamp[1024];
            snprintf(format, sizeof(format), "[%%T.%03lld] ", (long long)(ts.tv_nsec) / 1000000);
            strftime(timestamp, sizeof(timestamp), format, &localTime);
            fprintf(stream, "%s ", timestamp);
        }
    }
    newLine = (fmt[strlen(fmt) - 1] == '\n');
#endif  // USE_LOG_TIMESTAMPS
    vfprintf(stream, fmt, args);
}

static void logf(FILE* stream, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlogf(stream, fmt, args);
    va_end(args);
}

void loge(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlogf(stderr, fmt, args);
    va_end(args);
}

void logd(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlogf(stdout, fmt, args);
    va_end(args);
}
#endif  // !ANDROID
