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

#include "result.h"

#include <linux/netlink.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <string>
#include <vector>

// Convert an IPv6 address struct to a string for debugging purposes
std::string addrToStr(const struct in6_addr& addr);

// Represents any kind of address in a struct sockaddr.
class Address {
public:
    Address();
    explicit Address(const struct sockaddr_nl& address);
    explicit Address(const struct sockaddr_in6& address);
    explicit Address(struct in6_addr address);

    template<typename T>
    const T* get() const {
        return reinterpret_cast<const T*>(mStorage.data());
    }

    template<typename T>
    T* get() {
        return reinterpret_cast<T*>(mStorage.data());
    }

    ssize_t size() const { return mStorage.size(); }

    // Reset the address to be the max size possible for an address
    void reset();

    // Resolve |address| into an IPv6 address. |address| may be either a domain
    // name or just a string containing a numeric address.
    Result resolveInet(const std::string& address);
    // Resolve |interfaceName| into a link layer address. This can be used to
    // create a struct sockaddr_nl that can be used to listen on the given
    // interface at the link layer.
    Result resolveEth(const std::string& interfaceName);
private:
    std::vector<char> mStorage;
};

