/*
 * Copyright 2017, The Android Open Source Project
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

#include "dhcpserver.h"

#include "log.h"

#include <arpa/inet.h>
#include <net/if.h>

static void usage(const char* program) {
    ALOGE("Usage: %s -i <interface> -r <", program);
}

int main(int argc, char* argv[]) {
    char* excludeInterfaceName = nullptr;
    unsigned int excludeInterfaceIndex = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp("--exclude-interface", argv[i]) == 0) {
            if (i + 1 >= argc) {
                ALOGE("ERROR: Missing argument to "
                     "--exclude-interfaces parameter");
                usage(argv[0]);
                return 1;
            }
            excludeInterfaceName = argv[i + 1];
            excludeInterfaceIndex = if_nametoindex(excludeInterfaceName);
            if (excludeInterfaceIndex == 0) {
                ALOGE("ERROR: Invalid argument '%s' to --exclude-interface",
                     argv[i + 1]);
                usage(argv[0]);
                return 1;
            }
        }
    }

    DhcpServer server(excludeInterfaceIndex);
    Result res = server.init();
    if (!res) {
        ALOGE("Failed to initialize DHCP server: %s\n", res.c_str());
        return 1;
    }

    res = server.run();
    if (!res) {
        ALOGE("DHCP server failed: %s\n", res.c_str());
        return 1;
    }
    // This is weird and shouldn't happen, the server should run forever.
    return 0;
}


