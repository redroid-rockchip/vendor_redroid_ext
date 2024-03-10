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
#pragma once

#ifdef ANDROID

#define LOG_TAG "ipv6proxy"
#include <log/log.h>

#define loge(...) ALOGE(__VA_ARGS__)
#define logd(...) ALOGD(__VA_ARGS__)

#else
#include <stdio.h>

void loge(const char* fmt, ...);
void logd(const char* fmt, ...);

#endif
