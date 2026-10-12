#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <limits>
#include <string>
#include <vector>

static std::string directory;
static std::string mode;
static int writes = 0;
static bool unmounted = false;

static std::string mappedPath(const char* path) {
    const char prefix[] = "/data/vendor/var/run/netns/";
    assert(strncmp(path, prefix, sizeof(prefix) - 1) == 0);
    return directory + "/" + (path + sizeof(prefix) - 1);
}

static int testOpen(const char* path, int flags, mode_t permissions) {
    return ::open(mappedPath(path).c_str(), flags, permissions);
}

static int testUnlink(const char* path) {
    return ::unlink(mappedPath(path).c_str());
}

static int testRename(const char* from, const char* to) {
    assert(::access((directory + "/router.pid").c_str(), F_OK) != 0);
    if (mode == "rename") {
        errno = EACCES;
        return -1;
    }
    return ::rename(mappedPath(from).c_str(), mappedPath(to).c_str());
}

static ssize_t testWrite(int fd, const void* buffer, size_t size) {
    assert(::access((directory + "/router.pid").c_str(), F_OK) != 0);
    if (mode == "zero") return 0;
    if (mode == "error") {
        errno = EIO;
        return -1;
    }
    if (writes++ == 0) {
        errno = EINTR;
        return -1;
    }
    return ::write(fd, buffer, size > 2 ? 2 : size);
}

static int testUnshare(int) { return 0; }
static int testMount(const char*, const char*, const char*, unsigned long, const void*) {
    return 0;
}
static int testUmount(const char*) {
    unmounted = true;
    return 0;
}

struct Ready {};
static int testPause() { throw Ready(); }

int property_set(const char* name, const char* value) {
    assert(strcmp(name, "vendor.qemu.networknamespace") == 0);
    assert(strcmp(value, "ready") == 0);
    assert(::access((directory + "/router.pid").c_str(), F_OK) == 0);
    return mode == "property" ? -1 : 0;
}

#define open testOpen
#define unlink testUnlink
#define rename testRename
#define write testWrite
#define unshare testUnshare
#define mount testMount
#define umount testUmount
#define pause testPause
#define main namespaceServiceMain
#include "../network/createns/createns.cpp"
#undef main
#undef pause
#undef umount
#undef mount
#undef unshare
#undef write
#undef rename
#undef unlink
#undef open

int main(int argc, char* argv[]) {
    assert(argc == 3);
    directory = argv[1];
    mode = argv[2];
    char program[] = "createns2";
    char name[] = "router";
    char* arguments[] = {program, name};
    int result = -1;
    try {
        result = namespaceServiceMain(2, arguments);
    } catch (const Ready&) {
        assert(mode == "success");
        FILE* file = fopen((directory + "/router.pid").c_str(), "r");
        assert(file != nullptr);
        long pid = 0;
        assert(fscanf(file, "%ld", &pid) == 1);
        fclose(file);
        assert(pid == getpid());
        assert(!unmounted);
        assert(::access((directory + "/router.pid.tmp").c_str(), F_OK) != 0);
        return 0;
    }
    assert(mode != "success");
    assert(result == 1);
    assert(unmounted);
    assert(::access((directory + "/router.pid").c_str(), F_OK) != 0);
    assert(::access((directory + "/router.pid.tmp").c_str(), F_OK) != 0);
    assert(::access((directory + "/router").c_str(), F_OK) != 0);
    return 0;
}
