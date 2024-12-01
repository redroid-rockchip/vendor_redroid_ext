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

class Result {
public:
    static Result success() {
        return Result(true);
    }
    // Construct a result indicating an error. NOTE: the data in |message| will
    // NOT be copied. It must be kept alive for as long as its intended to be
    // used. This way the object is kept light-weight.
    static Result error(const char* message) {
        return Result(message);
    }

    bool isSuccess() const { return mSuccess; }
    bool operator!() const { return !mSuccess; }

    const char* c_str() const { return mMessage; }
private:
    explicit Result(bool success) : mSuccess(success) { }
    explicit Result(const char* message)
        : mMessage(message), mSuccess(false) {
    }
    const char* mMessage;
    bool mSuccess;
};

