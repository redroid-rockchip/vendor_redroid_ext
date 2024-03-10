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

#include <vector>

#include <string.h>

#include "log.h"
#include "namespace.h"
#include "proxy.h"

void usage(const char* program) {
    loge("Usage: %s -n [namespace|pid] -o <outer if> -i <inner ifs>\n",
         program);
    loge("  -o   Outer interface that connects to an existing IPv6 network.\n"
         "  -i   Comma separated list of inner interfaces that would like\n"
         "       to access the IPv6 network available on the outer interface.\n"
         "  -n   Optional parameter that causes the proxy to run in the given\n"
         "       network namespace. If a name is given instead of a PID the\n"
         "       namespace is expected to be set up by iproute2 or with a\n"
         "       similar approach where the namespace is linked in\n"
         "       /var/run/netns. A PID is assumed if the argument is numeric.\n"
         "       If providing a PID the same namespace that the PID is\n"
         "       running in will be used. In this scenario there is no\n"
         "       requirement for a file in /var/run/netns.\n"
         "\n"
         "       The proxy will ensure that router solicitations from inner\n"
         "       interfaces are forwarded to the outer interface and that\n"
         "       router advertisements from the outer interface are forwarded\n"
         "       to the inner interfaces. In addition to this neighbor\n"
         "       solicitations and advertisements will also be forwarded in a\n"
         "       way that enables IPv6 connectivity and routes will be set up\n"
         "       for source addresses on the inner interfaces so that replies\n"
         "       can reach those sources as expected.\n"
    );
}

static std::vector<const char*> splitString(char* str, char delimiter) {
    std::vector<const char*> parts;
    char* part = nullptr;
    do {
        parts.push_back(str);
        part = strchr(str, delimiter);
        if (part != nullptr) {
            *part = '\0';
            str = part + 1;
        }
    } while (part != nullptr);
    return parts;
}

int main(int argc, char* argv[]) {
    char* inner = nullptr;
    const char* outer = nullptr;
    const char* netNamespace = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            outer = argv[++i];
        } else if (strcmp(argv[i], "-i") == 0) {
            inner = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 1;
        } else if (strcmp(argv[i], "-n") == 0) {
            netNamespace = argv[++i];
        } else {
            loge("ERROR: Unknown argument '%s'\n\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }
    bool error = false;
    if (inner == nullptr) {
        loge("ERROR: Missing inner interface\n");
        error = true;
    }
    if (outer == nullptr) {
        loge("ERROR: Missing outer interface\n");
        error = true;
    }
    if (netNamespace) {
        if (!setNetworkNamespace(netNamespace)) {
            error = true;
        }
    }
    if (error) {
        usage(argv[0]);
        return 1;
    }

    std::vector<const char*> innerInterfaces = splitString(inner, ',');

    Proxy proxy(outer, innerInterfaces.begin(), innerInterfaces.end());

    return proxy.run();
}

