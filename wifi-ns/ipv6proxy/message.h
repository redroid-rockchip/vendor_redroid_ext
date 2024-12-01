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

class Message {
public:

    size_t size() const { return mSize; }
    size_t capacity() const { return sizeof(mData); }
    const char* data() const { return mData; }
    char* data() { return mData; }

    void setSize(size_t size) { mSize = size; }

protected:
    char mData[8192];
    size_t mSize;
};

