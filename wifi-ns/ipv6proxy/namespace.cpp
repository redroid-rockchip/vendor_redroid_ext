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

#include "namespace.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"

static const char kNetNsDir[] = "/data/vendor/var/run/netns";

// Set the current namespace to that of the /proc/<pid>/ns/net provided in
// |path|. This may be a link or mount point for that same file, anything that
// when opened will be an fd usable by setns is fine.
static bool setNamespaceFromPath(const char* path) {
    int nsFd = open(path, O_RDONLY | O_CLOEXEC);
    if (nsFd == -1) {
        loge("Cannot open network namespace at '%s': %s\n",
             path, strerror(errno));
        return false;
    }

    if (setns(nsFd, CLONE_NEWNET) == -1) {
        loge("Cannot set network namespace at '%s': %s\n",
             path, strerror(errno));
        close(nsFd);
        return false;
    }
    close(nsFd);
    return true;
}

bool setNetworkNamespace(const char* ns) {
    // There is a file in the net namespace dir (usually /var/run/netns) with
    // the same name as the namespace. This file is bound to /proc/<pid>/ns/net
    // by the 'ip' command when the namespace is created. This allows us to
    // access the file of a process running in that network namespace without
    // knowing its pid, knowing the namespace name is enough.
    //
    // We are going to call setns which requires a file descriptor to that proc
    // file in /proc/<pid>/net. The process has to already be running in that
    // namespace. Since the file in the net namespace dir has been bound to
    // such a file already we just have to open /var/run/netns/<namespace> and
    // we have the required file descriptor.
    char nsPath[PATH_MAX];
    snprintf(nsPath, sizeof(nsPath), "%s/%s", kNetNsDir, ns);
    return setNamespaceFromPath(nsPath);
}

bool setNetworkNamespace(pid_t pid) {
    // If we know the pid we can create the path to the /proc file right away
    // and use that when we call setns.
    char nsPath[PATH_MAX];
    static_assert(sizeof(pid_t) <= sizeof(unsigned long long),
                  "Cast requires sizeof(pid_t) <= sizeof(unsigned long long)");
    snprintf(nsPath, sizeof(nsPath), "/proc/%llu/ns/net/",
             static_cast<unsigned long long>(pid));
    return setNamespaceFromPath(nsPath);
}

