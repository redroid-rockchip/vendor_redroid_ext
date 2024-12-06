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
#include <string>

#include "address.h"
#include "socket.h"

// A class that contains the information used by the proxy that is specific to a
// single interface. This includes the name and index of the interface as well
// as the sockets used to send and receive data on the interface.
//
// The class also contains the functionality needed to initialized and configure
// the interface, sockets and addresses.
class Interface {
public:
    explicit Interface(const std::string& name);

    bool init();

    const std::string& name() const { return mName; }
    uint32_t index() const { return mIndex; }
    Socket& ipSocket() { return mIpSocket; }
    Socket& icmpSocket() { return mIcmpSocket; }
    const Address& linkAddr() const { return mLinkAddr; }

private:
    bool setAllMulti();
    bool resolveAddresses();
    bool configureIcmpSocket();
    bool configureIpSocket();

    std::string mName;
    uint32_t mIndex;
    Socket mIpSocket;
    Socket mIcmpSocket;
    Address mLinkAddr;
};

