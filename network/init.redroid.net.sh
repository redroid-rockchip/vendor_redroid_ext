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

REDROID_WIFI=`getprop ro.boot.redroid_wifi`
REDROID_RADIO=`getprop ro.boot.redroid_radio`
if [ "$REDROID_WIFI" -ne "1" -a "$REDROID_RADIO" -ne "1" ]; then
  echo "No need to initialize the network environment, skip"
  exit
fi

create_router_ns() {
  local PID_PATH="/data/vendor/var/run/netns/$1.pid"
  rm "$PID_PATH"
  setprop ctl.start redroid_router_ns
  while [ 1 ]; do
    if [ -f "$PID_PATH" ]; then
      break
    else
      sleep 1s
    fi
  done
  cat $PID_PATH
}

init_eth() {
  local NAMESPACE=$1
  local NAMESPACE_PID=$2
  local ETH0_ADDR=$(/system/bin/ip addr show dev eth0 | grep 'inet ' | awk '{print $2,$3,$4}')
  local ETH0_GW=$(/system/bin/ip route get 8.8.8.8 | head -n 1 | awk '{print $3}')
  echo "Get eth0 addr: ${ETH0_ADDR}, gateway: ${ETH0_GW}"
  /system/bin/ip link set eth0 netns ${NAMESPACE_PID}
  /vendor/bin/execns2 ${NAMESPACE} /system/bin/ip link set eth0 up
  /vendor/bin/execns2 ${NAMESPACE} /system/bin/ip addr add ${ETH0_ADDR} dev eth0
  /vendor/bin/execns2 ${NAMESPACE} /system/bin/ip route add default via ${ETH0_GW} dev eth0
}

init_wlan() {
  local NAMESPACE=$1
  local NAMESPACE_PID=$2
  local WIFI_GATEWAY=`getprop ro.boot.redroid_wifi_gateway`
  if [ ! -n "$WIFI_GATEWAY" ]; then
    WIFI_GATEWAY='7.7.7.1/24'
  fi
  /vendor/bin/create_radios2 2 `expr $RANDOM % 65535`
  if [ "$?" -eq "0" ]; then
    /vendor/bin/iw phy phy$(/vendor/bin/iw dev wlan1 info | awk -F 'wiphy +' '{print $2}' | awk NF) set netns ${PID}
    /vendor/bin/execns2 ${NAMESPACE} /system/bin/ip link set wlan1 up
    /vendor/bin/execns2 ${NAMESPACE} /system/bin/ip link add name br0 type bridge
    /vendor/bin/execns2 ${NAMESPACE} /system/bin/ip addr add ${WIFI_GATEWAY} dev br0
    /vendor/bin/execns2 ${NAMESPACE} /system/bin/ip link set br0 mtu 1400
    /vendor/bin/execns2 ${NAMESPACE} /system/bin/ip link set br0 up

    /vendor/bin/execns2 ${NAMESPACE} /system/bin/iptables -w -W 50000 -t nat -A POSTROUTING -s ${WIFI_GATEWAY} -o eth0 -j MASQUERADE

    # Copy the hostapd configuration file to the data partition
    local sed_args=""
    for i in $(seq 1 10); do
      sed_args="$sed_args -e 's/<bssid$i>/00:$(echo $RANDOM | md5sum | sed 's/../&:/g' | cut -c 1-14)/'"
    done
    echo sed $sed_args /vendor/etc/hostapd.conf | sh > /data/vendor/wifi/hostapd/redroid_hostapd.conf
    chown wifi:wifi /data/vendor/wifi/hostapd/redroid_hostapd.conf
    chmod 660 /data/vendor/wifi/hostapd/redroid_hostapd.conf

    # Start hostapd, the access point software
    setprop ctl.start redroid_hostapd
    setprop ctl.start redroid_dhcpserver
  fi
}

init_radio() {
  local NAMESPACE=$1
  local NAMESPACE_PID=$2
  /system/bin/ip link add radio0 type veth peer name radio0-peer netns ${NAMESPACE_PID}
  /system/bin/ip addr add 7.8.8.2/24 dev radio0
  /system/bin/ip link set radio0 up
  /system/bin/ip route add default via 7.8.8.1 dev radio0
  # Enable privacy addresses for radio0, this is done by the framework for wlan0
  sysctl -wq net.ipv6.conf.radio0.use_tempaddr=2

  /vendor/bin/execns2 ${NAMESPACE} /system/bin/ip addr add 7.8.8.1/24 dev radio0-peer
  /vendor/bin/execns2 ${NAMESPACE} /system/bin/ip link set radio0-peer up
  /vendor/bin/execns2 ${NAMESPACE} sysctl -wq net.ipv6.conf.all.forwarding=1

  /vendor/bin/execns2 ${NAMESPACE} /system/bin/iptables -w -W 50000 -t nat -A POSTROUTING -s 7.8.8.0/24 -o eth0 -j MASQUERADE

  ifconfig radio0 -multicast
}

NAMESPACE="router"

# createns will have created a file that contains the process id (pid) of a
# process running in the network namespace. This pid is needed for some commands
# to access the namespace.
PID=`create_router_ns $NAMESPACE`

/system/bin/ip rule add from all lookup main pref 5000

init_eth $NAMESPACE $PID

# Start the ADB daemon in the router namespace
setprop ctl.start adbd_proxy

if [ "$REDROID_WIFI" -eq "1" ]; then
  init_wlan $NAMESPACE $PID
fi

if [ "$REDROID_RADIO" -eq "1" ]; then
  init_radio $NAMESPACE $PID
fi

# Start the IPv6 proxy that will enable use of IPv6 in the main namespace
setprop ctl.start redroid_ipv6proxy
