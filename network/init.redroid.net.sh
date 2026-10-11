#!/vendor/bin/sh

# Do all the setup required for WiFi.
# The kernel driver mac80211_hwsim has already created two virtual wifi devices
# us. These devices are connected so that everything that's sent on one device
# is recieved on the other and vice versa. This allows us to create a fake
# WiFi network with an access point running inside the guest. Here is the setup
# for that and the basics of how it works.
#
# Create a namespace named router and move eth0 to it. Create a virtual ethernet
# pair of devices and move both one virtual ethernet interface and one virtual
# wifi interface into the router namespace. Then set up NAT networking for those
# interfaces so that traffic flowing through them reach eth0 and eventually the
# host and the internet. The main network namespace will now only see the other
# ends of those pipes and send traffic on them depending on if WiFi or radio is
# used.  Finally run hostapd in the network namespace to create an access point
# for the guest to connect to and dnsmasq to serve as a DHCP server for the WiFi
# connection.
#
#          main namespace                     router namespace
#       -------       ----------   |    ---------------
#       | ril |<----->| radio0 |<--+--->| radio0-peer |<-------+
#       -------       ----------   |    ---------------        |
#                                  |            ^              |
#                                  |            |              |
#                                  |            v              v
#                                  |      *************     --------
#                                  |      * ipv6proxy *<--->| eth0 |<--+
#                                  |      *************     --------   |
#                                  |            ^              ^       |
#                                  |            |              |       |
#                                  |            v              |       |
# ------------------   ---------   |        ---------          |       |
# | wpa_supplicant |<->| wlan0 |<--+------->| wlan1 |<---------+       |
# ------------------   ---------   |        ---------                  |
#                                  |         ^     ^                   |
#                                  |         |     |                   v
#                                  |         v     v                --------
#                                  | ***********  ***********       | host |
#                                  | * hostapd *  * dnsmasq *       --------
#                                  | ***********  ***********
#

create_router_ns() {
  local pid_path="/data/vendor/var/run/netns/$1.pid"
  setprop vendor.qemu.networknamespace "" || return 1
  rm -f "$pid_path" || return 1
  setprop ctl.start redroid_router_ns || return 1
  local remaining=20
  while [ "$(getprop vendor.qemu.networknamespace)" != "ready" ]; do
    if [ "$remaining" -eq "0" ]; then
      echo "failed to create $1 namespace: timed out waiting for readiness" >&2
      return 1
    fi
    sleep 1s || return 1
    remaining=$((remaining - 1))
  done
  local pid
  pid=$(cat "$pid_path") || return 1
  case "$pid" in
    ""|0*|*[!0-9]*)
      echo "failed to create $1 namespace: invalid pid '$pid'" >&2
      return 1
      ;;
  esac
  echo "$pid"
}

init_wlan() {
  local router_ns=$1
  local router_pid=$2
  /vendor/bin/create_radios2 2 `expr $RANDOM % 65535` || return 1
  local wifi_gateway=`getprop ro.boot.redroid_wifi_gateway`
  if [ ! -n "$wifi_gateway" ]; then
    wifi_gateway='7.7.7.1/24'
  fi

  echo "init wlan0 in main, wlan1 in $router_ns"
  /vendor/bin/iw phy phy$(/vendor/bin/iw dev wlan1 info | awk -F 'wiphy +' '{print $2}' | awk NF) set netns ${router_pid} || return 1
  /vendor/bin/execns2 ${router_ns} /system/bin/ip link set wlan1 up || return 1
  /vendor/bin/execns2 ${router_ns} /system/bin/ip link add name br0 type bridge || return 1
  /vendor/bin/execns2 ${router_ns} /system/bin/ip addr add ${wifi_gateway} dev br0 || return 1
  /vendor/bin/execns2 ${router_ns} /system/bin/ip link set br0 mtu 1400 || return 1
  /vendor/bin/execns2 ${router_ns} /system/bin/ip link set br0 up || return 1

  if [ "$router_ns" == "router" ]; then
    /vendor/bin/execns2 ${router_ns} /system/bin/iptables -w -W 50000 -t nat -A POSTROUTING -s ${wifi_gateway} -o eth0 -j MASQUERADE || return 1
  fi

  # Copy the hostapd configuration file to the data partition
  local sed_args=""
  for i in $(seq 1 10); do
    sed_args="$sed_args -e 's/<bssid$i>/00:$(echo $RANDOM | md5sum | sed 's/../&:/g' | cut -c 1-14)/'"
  done
  echo sed $sed_args /vendor/etc/hostapd.conf | sh > /data/vendor/wifi/hostapd/redroid_hostapd.conf || return 1
  chown wifi:wifi /data/vendor/wifi/hostapd/redroid_hostapd.conf || return 1
  chmod 660 /data/vendor/wifi/hostapd/redroid_hostapd.conf || return 1
}

init_radio() {
  local router_ns=$1
  local router_pid=$2

  if [ "$router_ns" == "router" ]; then
    echo "init radio0 in main, radio0-peer in $router_ns"
    /system/bin/ip link add radio0 type veth peer name radio0-peer netns ${router_pid} || return 1
    /system/bin/ip link set radio0 up || return 1
    /system/bin/ip addr add 7.8.8.2/16 dev radio0 || return 1
    # Enable privacy addresses for radio0, this is done by the framework for wlan0
    sysctl -wq net.ipv6.conf.radio0.use_tempaddr=2

    /vendor/bin/execns2 ${router_ns} /system/bin/ip addr add 7.8.8.1/24 dev radio0-peer || return 1
    /vendor/bin/execns2 ${router_ns} /system/bin/ip link set radio0-peer up || return 1
    /vendor/bin/execns2 ${router_ns} sysctl -wq net.ipv6.conf.all.forwarding=1
    /vendor/bin/execns2 ${router_ns} /system/bin/iptables -w -W 50000 -t nat -A POSTROUTING -s 7.8.8.0/24 -o eth0 -j MASQUERADE || return 1

    ifconfig radio0 -multicast
  else
    echo "init radio0 in main"
    /system/bin/ip link add name radio0 type bridge || return 1
    /system/bin/ip addr add 7.8.8.2/16 dev radio0 || return 1
    /system/bin/ip link set radio0 up || return 1
    # Enable privacy addresses for radio0, this is done by the framework for wlan0
    sysctl -wq net.ipv6.conf.radio0.use_tempaddr=2
  fi

  return 0
}

redroid_wifi=`getprop ro.boot.redroid_wifi`
redroid_radio=`getprop ro.boot.redroid_radio`
if [ "$redroid_wifi" -eq "1" -o "$redroid_radio" -eq "1" ]; then

  router_ns="main"
  router_pid=1
  /system/bin/iptables -t nat -L # 1>/dev/null 2>&1
  if [ "$?" -eq "0" ]; then
    echo "iptables is available, enable router namespace"
    router_ns="router"
    router_pid=$(create_router_ns $router_ns) || exit 1
    echo "start the ADB daemon in the router namespace"
    setprop ctl.start adbd_proxy || echo "failed to start ADB proxy" >&2
  else
    echo "iptables is not available, disable router namespace"
  fi

  wifi_ready=0
  radio_ready=0
  if [ "$redroid_wifi" -eq "1" ]; then
    if init_wlan "$router_ns" "$router_pid"; then
      wifi_ready=1
    else
      echo "failed to initialize Wi-Fi; continuing other transports" >&2
    fi
  fi
  if [ "$redroid_radio" -eq "1" ]; then
    if init_radio "$router_ns" "$router_pid"; then
      radio_ready=1
    else
      echo "failed to initialize radio; continuing other transports" >&2
    fi
  fi

  if [ "$wifi_ready" -eq 0 -a "$radio_ready" -eq 0 ]; then
    echo "failed to initialize any requested network transport" >&2
    exit 1
  fi

  eth0_addr=""
  eth0_gw=""
  for i in $(seq 1 20); do
    eth0_addr=$(/system/bin/ip addr show dev eth0 | grep 'inet ' | awk '{print $2,$3,$4}')
    if [ -z "$eth0_addr" ]; then
      echo "failed to find eth0 addr, retry 1s later"
      sleep 1s
      continue
    fi
    eth0_addr_array=($eth0_addr)
    eth0_gw=$(/system/bin/ip route get 8.8.8.8 from ${eth0_addr_array[0]} | head -n 1 | awk '{print $5}')
    if [ -z "$eth0_gw" ]; then
      echo "failed to find eth0 gateway, retry 1s later"
      sleep 1s
      continue
    fi
    break
  done
  if [ -z "$eth0_addr" -o -z "$eth0_gw" ]; then
    echo "failed to find eth0 addr and gw, exit"
    exit 1
  else
    echo "find eth0 addr: ${eth0_addr}, gw: ${eth0_gw}"
  fi

  if [ "$router_ns" == "router" ]; then
    network_interface="eth0"
    echo "init eth0 in router"
    /system/bin/ip link set eth0 netns ${router_pid} || exit 1
    /vendor/bin/execns2 ${router_ns} /system/bin/ip link set eth0 up || exit 1
    /vendor/bin/execns2 ${router_ns} /system/bin/ip addr add ${eth0_addr} dev eth0 || exit 1
    /vendor/bin/execns2 ${router_ns} /system/bin/ip route add default via ${eth0_gw} dev eth0 || exit 1
    # Start the IPv6 proxy that will enable use of IPv6 in the main namespace
    setprop ctl.start redroid_ipv6proxy || echo "failed to start IPv6 proxy" >&2
  else
    network_interface="veth0"
    echo "rename eth0 to veth0 in main"
    /system/bin/ip link set eth0 down || exit 1
    /system/bin/ip link set eth0 name veth0 || exit 1
    echo "init veth0 in main"
    /system/bin/ip addr replace ${eth0_addr} dev veth0 || exit 1
    /system/bin/ip link set veth0 up || exit 1
    /system/bin/ip rule add from all lookup main pref 5000 || exit 1
    /system/bin/ip route replace default via ${eth0_gw} dev veth0 || exit 1
  fi

  if [ "$wifi_ready" -eq 1 ]; then
    if ! setprop vendor.redroid.net.interface "$network_interface"; then
      echo "failed to publish DHCP upstream interface" >&2
      wifi_ready=0
    elif ! setprop ctl.start redroid_hostapd; then
      echo "failed to start hostapd" >&2
      wifi_ready=0
    elif ! setprop ctl.start redroid_dhcpserver; then
      echo "failed to start DHCP server" >&2
      wifi_ready=0
    fi
  fi
  if [ "$radio_ready" -eq 1 ]; then
    if ! setprop ctl.start redroid_vlte; then
      echo "failed to start virtual radio" >&2
      radio_ready=0
    fi
  fi
  if [ "$wifi_ready" -eq 0 -a "$radio_ready" -eq 0 ]; then
    echo "failed to start any requested network transport" >&2
    exit 1
  fi

  if [ "$router_ns" == "main" ]; then
    for i in $(seq 2 24); do
      sleep 5s
      echo "update ip rule: ${i}"
      /system/bin/ip rule add from all lookup main pref 5000 2>/dev/null
      /system/bin/ip route add default via ${eth0_gw} dev veth0 2>/dev/null
    done
  fi

#  if [ "$redroid_radio" -eq "1" ]; then
#    for i in $(seq 1 5); do
#      sleep 10s
#      local radio_rule=$(/system/bin/ip rule | grep radio0)
#      if [ -z "$radio_rule" ]; then
#        echo "not found radio0 rule, restart ril-daemon"
#        setprop ctl.restart vendor.ril-daemon
#      else
#        echo "found radio0 rule"
#        break
#      fi
#    done
#  fi

fi
