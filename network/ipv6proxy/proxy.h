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

#include <string>
#include <vector>

#include "interface.h"
#include "router.h"

struct pollfd;
class Message;
class Packet;
class Socket;

class Proxy {
public:
    template<typename Iter>
    Proxy(std::string outerInterfaceName,
          Iter innerInterfacesBegin, Iter innerInterfacesEnd)
        : mOuterIf(outerInterfaceName),
          mLogDebug(false) {

        for (Iter i = innerInterfacesBegin; i != innerInterfacesEnd; ++i) {
            mInnerIfs.emplace_back(*i);
        }
    }

    int run();

private:
    enum ForwardOpt {
        kForwardOnly = 0,
        kRewriteTargetLink = (1 << 0),
        kRewriteSourceLink = (1 << 1),
        kSpoofSource = (1 << 2),
        kAddRoute = (1 << 3),
        kSetDefaultGateway = (1 << 4)
    };

    bool receiveIfPossible(const pollfd&, Socket& socket, Message* message);

    void handleOuterMessage(Message& message);
    void handleInnerMessage(const Interface& inner, Message& message);
    void forward(const Interface& from, Interface& to,
                 Packet& packet, uint32_t options);

    std::vector<Interface> mInnerIfs;
    Interface mOuterIf;

    Router mRouter;
    bool mLogDebug;
};

