"""Host regression tests using bash and mocked network/init commands.

The production script is copied with filesystem paths remapped; it is not
executed by Android mksh or init, and no real network namespaces are created.
"""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NetworkStartupTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="redroid-network-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.state_path = self.directory / "state.json"
        self.environment = dict(os.environ, REDROID_TEST_STATE=str(self.state_path))
        directories = ("system/bin", "vendor/bin", "vendor/etc", "data/vendor")
        source = (ROOT / "network/init.redroid.net.sh").read_text()
        for relative in directories:
            path = self.directory / relative
            path.mkdir(parents=True, exist_ok=True)
            source = source.replace("/" + relative, str(path))
        self.script = self.directory / "init.redroid.net.sh"
        self.script.write_text(source)
        binary = self.directory / "vendor/bin"
        fixture = ROOT / "tests/_network_commands.py"
        fixture_copy = binary / "commands"
        shutil.copyfile(fixture, fixture_copy)
        fixture_copy.chmod(0o755)
        for command in (
            "create_radios2", "execns2", "iw", "getprop", "setprop",
            "sysctl", "ifconfig", "chmod", "chown", "sleep",
        ):
            (binary / command).symlink_to(fixture_copy)
        for command in ("ip", "iptables"):
            (self.directory / "system/bin" / command).symlink_to(fixture_copy)
        self.environment["PATH"] = str(binary) + os.pathsep + os.environ["PATH"]
        (self.directory / "vendor/etc/hostapd.conf").write_text("bssid=<bssid1>\n")
        (self.directory / "data/vendor/wifi/hostapd").mkdir(parents=True)
        pid = self.directory / "data/vendor/var/run/netns/router.pid"
        pid.parent.mkdir(parents=True)
        pid.write_text("999")
        rc = (ROOT / "redroid.ext.rc").read_text()
        dhcp_argument = re.search(r"--exclude-interface (\S+)", rc).group(1)
        self.state = {
            "router": True,
            "properties": {
                "ro.boot.redroid_wifi": "1",
                "ro.boot.redroid_radio": "1",
                "vendor.qemu.networknamespace": "ready",
            },
            "pid_path": str(pid),
            "interface": "eth0",
            "dhcp_argument": dhcp_argument,
            "events": [], "started": [], "violations": [],
            "dhcp_interfaces": [], "polls": 0,
        }

    def run_script(self) -> subprocess.CompletedProcess:
        self.state_path.write_text(json.dumps(self.state))
        result = subprocess.run(
            ["bash", str(self.script)], env=self.environment,
            text=True, capture_output=True, timeout=20,
        )
        self.state = json.loads(self.state_path.read_text())
        return result

    def assert_failed_before_consumers(self) -> None:
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.state["started"], [])

    def test_startup_and_dhcp_restart_in_both_topologies(self) -> None:
        for router in (True, False):
            with self.subTest(router=router):
                self.setUp()
                self.state["router"] = router
                result = self.run_script()
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(self.state["violations"], [])
                self.assertEqual(self.state["started"], [
                    "redroid_hostapd", "redroid_dhcpserver", "redroid_vlte",
                ])
                subprocess.run(
                    [str(self.directory / "vendor/bin/setprop"),
                     "ctl.restart", "redroid_dhcpserver"],
                    env=self.environment, check=True,
                )
                self.state = json.loads(self.state_path.read_text())
                self.assertEqual(self.state["dhcp_interfaces"],
                                 ["eth0", "eth0"] if router else ["veth0", "veth0"])
                self.assertEqual(self.state["violations"], [])
                if router:
                    self.assertEqual(self.state["polls"], 3)
                    events = self.state["events"]
                    self.assertLess(events.index("setprop vendor.qemu.networknamespace "),
                                    events.index("setprop ctl.start redroid_router_ns"))
                else:
                    events = self.state["events"]
                    self.assertLess(events.index("setprop ctl.start redroid_vlte"),
                                    events.index("sleep 5s"))

    def test_namespace_timeout_does_not_use_stale_readiness(self) -> None:
        self.state["timeout"] = True
        self.assert_failed_before_consumers()
        self.assertEqual(self.state["events"].count("sleep 1s"), 20)

    def test_invalid_published_pid_is_rejected(self) -> None:
        for pid in ("", "0", "12\n3"):
            with self.subTest(pid=pid):
                self.setUp()
                self.state["pid"] = pid
                self.assert_failed_before_consumers()

    def test_critical_operation_failures_stop_startup(self) -> None:
        for router, failure in (
            (True, "setprop vendor.qemu.networknamespace "),
            (True, "setprop ctl.start redroid_router_ns"),
            (True, "create_radios2 "),
            (True, "ip link set eth0 netns "),
            (True, "ip route add default "),
            (True, "setprop vendor.redroid.net.interface "),
            (False, "ip link set eth0 name veth0"),
            (False, "ip addr replace "),
            (False, "ip route replace default "),
        ):
            with self.subTest(router=router, failure=failure):
                self.setUp()
                self.state["router"] = router
                self.state["fail"] = failure
                self.assert_failed_before_consumers()

    def test_service_start_failure_does_not_start_later_consumers(self) -> None:
        self.state["fail"] = "setprop ctl.start redroid_dhcpserver"
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.state["started"], ["redroid_hostapd"])

    def test_single_transport_and_disabled_switches(self) -> None:
        for router in (True, False):
            for wifi, radio in (("1", "0"), ("0", "1"), ("0", "0")):
                with self.subTest(router=router, wifi=wifi, radio=radio):
                    self.setUp()
                    self.state["router"] = router
                    self.state["properties"]["ro.boot.redroid_wifi"] = wifi
                    self.state["properties"]["ro.boot.redroid_radio"] = radio
                    result = self.run_script()
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    expected = []
                    if wifi == "1":
                        expected.extend(("redroid_hostapd", "redroid_dhcpserver"))
                    if radio == "1":
                        expected.append("redroid_vlte")
                    self.assertEqual(self.state["started"], expected)
                    self.assertEqual(self.state["violations"], [])


class NamespacePublicationTest(unittest.TestCase):
    def test_atomic_publication_and_failure_cleanup(self) -> None:
        with tempfile.TemporaryDirectory(prefix="redroid-createns-") as temporary:
            directory = Path(temporary)
            (directory / "log").mkdir()
            (directory / "cutils").mkdir()
            (directory / "log/log.h").write_text(
                '#pragma once\n#include <stdio.h>\n'
                '#define ALOGE(...) fprintf(stderr, __VA_ARGS__)\n'
            )
            (directory / "cutils/properties.h").write_text(
                '#pragma once\nint property_set(const char*, const char*);\n'
            )
            executable = directory / "createns_test"
            subprocess.run([
                "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-I", str(directory), str(ROOT / "tests/createns_test.cpp"),
                "-o", str(executable),
            ], check=True)
            for mode in ("success", "zero", "error", "rename", "property"):
                with self.subTest(mode=mode):
                    target = directory / mode
                    target.mkdir()
                    subprocess.run([str(executable), str(target), mode], check=True,
                                   capture_output=True, text=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
