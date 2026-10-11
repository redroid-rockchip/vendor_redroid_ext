#!/usr/bin/env python3

import json
import os
from pathlib import Path
import re
import sys


def main() -> int:
    path = Path(os.environ["REDROID_TEST_STATE"])
    state = json.loads(path.read_text())
    command = Path(sys.argv[0]).name
    arguments = sys.argv[1:]
    namespace = "main"
    if command == "execns2":
        namespace, command = arguments[0], Path(arguments[1]).name
        arguments = arguments[2:]
        if namespace == "router" and not state["router"]:
            namespace = "main"
    event = " ".join([command, *arguments])
    state["events"].append(event)
    result = 0

    if state.get("fail") and event.startswith(state["fail"]):
        result = 1
    elif command == "getprop":
        name = arguments[0]
        if name == "vendor.qemu.networknamespace" and state.get("pending"):
            state["polls"] += 1
            if not state.get("timeout") and state["polls"] >= 3:
                pid = Path(state["pid_path"])
                temporary = pid.with_suffix(".tmp")
                temporary.write_text(state.get("pid", "42"))
                temporary.replace(pid)
                state["properties"][name] = "ready"
                state["pending"] = False
        print(state["properties"].get(name, ""))
    elif command == "setprop":
        name, value = arguments
        if name in ("ctl.start", "ctl.restart"):
            if value == "redroid_router_ns":
                state["pending"] = True
            if value in ("redroid_hostapd", "redroid_dhcpserver", "redroid_vlte"):
                state["started"].append(value)
                if not state.get("configured"):
                    state["violations"].append(value + " started before uplink")
            if value == "redroid_dhcpserver":
                argument = state["dhcp_argument"]
                interface = re.sub(
                    r"\$\{([^}]+)\}",
                    lambda match: state["properties"].get(match.group(1), ""),
                    argument,
                )
                state["dhcp_interfaces"].append(interface)
                if interface != state["interface"]:
                    state["violations"].append("DHCP interface does not exist")
        else:
            state["properties"][name] = value
    elif command == "iptables" and "-L" in arguments:
        result = 0 if state["router"] else 1
    elif command == "iw" and arguments[:3] == ["dev", "wlan1", "info"]:
        print("wiphy 1")
    elif command == "ip":
        if arguments == ["addr", "show", "dev", "eth0"]:
            print("    inet 192.168.40.2/24 brd 192.168.40.255 scope global eth0")
        elif arguments[:2] == ["route", "get"]:
            print("8.8.8.8 from 192.168.40.2 via 192.168.40.1 dev eth0")
        elif arguments == ["link", "set", "eth0", "name", "veth0"]:
            state["interface"] = "veth0"
        elif arguments[:4] == ["link", "set", "eth0", "netns"]:
            if arguments[4:] != ["42"]:
                state["violations"].append("invalid namespace pid")
        elif arguments[:1] == ["route"] and "default" in arguments:
            expected_namespace = "router" if state["router"] else "main"
            if namespace != expected_namespace or arguments[-1] != state["interface"]:
                state["violations"].append("uplink configured in wrong namespace")
            state["configured"] = True

    path.write_text(json.dumps(state))
    if result and command != "iptables":
        print("injected failure: " + event, file=sys.stderr)
    return result


if __name__ == "__main__":
    sys.exit(main())
