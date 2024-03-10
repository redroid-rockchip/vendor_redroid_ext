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

#include <stdint.h>

#include <netinet/in.h>

#include "socket.h"

class Router {
public:
    // Initialize the router, this has to be called before any other methods can
    // be called. It only needs to be called once.
    bool init();

    // Indicate that |address| is a neighbor to this node and that it is
    // accessible on the interface with index |interfaceIndex|.
    bool addNeighbor(const struct in6_addr& address, uint32_t interfaceIndex);

    // Add a route to |address|/|bits| on interface |interfaceIndex|. The
    // |bits| parameter indicates the bitmask of the address, for example in
    // the routing entry 2001:db8::/32 the |bits| parameter would be 32.
    bool addRoute(const struct in6_addr& address,
                  uint8_t bits,
                  uint32_t interfaceIndex);

    // Set the default gateway route to |address| on interface with index
    // |interfaceIndex|. Overwrites any existing default gateway with the same
    // address.
    bool setDefaultGateway(const struct in6_addr& address,
                           unsigned int interfaceIndex);
private:
    bool sendNetlinkMessage(const void* data, size_t size);

    // Netlink socket for setting up neighbors and routes
    Socket mSocket;
};

