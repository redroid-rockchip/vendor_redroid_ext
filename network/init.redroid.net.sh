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
  rm "$pid_path"
  setprop ctl.start redroid_router_ns
  while [ 1 ]; do
    if [ -f "$pid_path" ]; then
      break
    else
      sleep 1s
    fi
  done
  cat $pid_path
}

init_wlan() {
  local router_ns=$1
  local router_pid=$2
  /vendor/bin/create_radios2 2 `expr $RANDOM % 65535`
  if [ "$?" -eq "0" ]; then
    local wifi_gateway=`getprop ro.boot.redroid_wifi_gateway`
    if [ ! -n "$wifi_gateway" ]; then
      wifi_gateway='7.7.7.1/24'
    fi

    echo "init wlan0 in main, wlan1 in router"
    /vendor/bin/iw phy phy$(/vendor/bin/iw dev wlan1 info | awk -F 'wiphy +' '{print $2}' | awk NF) set netns ${router_pid}
    /vendor/bin/execns2 ${router_ns} /system/bin/ip link set wlan1 up
    /vendor/bin/execns2 ${router_ns} /system/bin/ip link add name br0 type bridge
    /vendor/bin/execns2 ${router_ns} /system/bin/ip addr add ${wifi_gateway} dev br0
    /vendor/bin/execns2 ${router_ns} /system/bin/ip link set br0 mtu 1400
    /vendor/bin/execns2 ${router_ns} /system/bin/ip link set br0 up
    /vendor/bin/execns2 ${router_ns} /system/bin/iptables -w -W 50000 -t nat -A POSTROUTING -s ${wifi_gateway} -o eth0 -j MASQUERADE

    # Copy the hostapd configuration file to the data partition
    local sed_args=""
    for i in $(seq 1 10); do
      sed_args="$sed_args -e 's/<bssid$i>/00:$(echo $RANDOM | md5sum | sed 's/../&:/g' | cut -c 1-14)/'"
    done
    echo sed $sed_args /vendor/etc/hostapd.conf | sh > /data/vendor/wifi/hostapd/redroid_hostapd.conf
    chown wifi:wifi /data/vendor/wifi/hostapd/redroid_hostapd.conf
    chmod 660 /data/vendor/wifi/hostapd/redroid_hostapd.conf

    # Start hostapd, the access point software
    echo "start hostapd, dhcpserver"
    setprop ctl.start redroid_hostapd
    setprop ctl.start redroid_dhcpserver
  fi
}

init_radio() {
  local router_ns=$1
  local router_pid=$2

  echo "init radio0 in main, radio0-peer in router"
  /system/bin/ip link add radio0 type veth peer name radio0-peer netns ${router_pid}
  /system/bin/ip link set radio0 up
  /system/bin/ip addr add 7.8.8.2/24 dev radio0
  # Enable privacy addresses for radio0, this is done by the framework for wlan0
  sysctl -wq net.ipv6.conf.radio0.use_tempaddr=2

  /vendor/bin/execns2 ${router_ns} /system/bin/ip addr add 7.8.8.1/24 dev radio0-peer
  /vendor/bin/execns2 ${router_ns} /system/bin/ip link set radio0-peer up
  /vendor/bin/execns2 ${router_ns} sysctl -wq net.ipv6.conf.all.forwarding=1
  /vendor/bin/execns2 ${router_ns} /system/bin/iptables -w -W 50000 -t nat -A POSTROUTING -s 7.8.8.0/24 -o eth0 -j MASQUERADE

  ifconfig radio0 -multicast

  echo "start redroid_vlte"
  setprop ctl.start redroid_vlte
}

redroid_wifi=`getprop ro.boot.redroid_wifi`
redroid_radio=`getprop ro.boot.redroid_radio`
if [ "$redroid_wifi" -eq "1" -o "$redroid_radio" -eq "1" ]; then

  local router_ns="router"
  local router_pid=$(create_router_ns $router_ns)

  if [ "$redroid_wifi" -eq "1" ]; then
    init_wlan $router_ns $router_pid
  fi
  if [ "$redroid_radio" -eq "1" ]; then
    init_radio $router_ns $router_pid
  fi

  local eth0_addr=""
  local eth0_gw=""
  for i in $(seq 1 20); do
    eth0_addr=$(/system/bin/ip addr show dev eth0 | grep 'inet ' | awk '{print $2,$3,$4}')
    eth0_gw=$(/system/bin/ip route get 8.8.8.8 | head -n 1 | awk '{print $3}')
    if [ -z "$eth0_addr" -o -z "$eth0_gw" ]; then
      echo "failed to find eth0 addr and gw, retry 1s later"
      sleep 1s
    else
      echo "find eth0 addr: ${eth0_addr}, gw: ${eth0_gw}"
      break
    fi
  done
  if [ -z "$eth0_addr" -o -z "$eth0_gw" ]; then
    echo "failed to find eth0 addr and gw, exit"
    exit
  fi

  # Start the ADB daemon in the router namespace
  setprop ctl.start adbd_proxy

  echo "init eth0 in router"
  /system/bin/ip link set eth0 netns ${router_pid}
  /vendor/bin/execns2 ${router_ns} /system/bin/ip link set eth0 up
  /vendor/bin/execns2 ${router_ns} /system/bin/ip addr add ${eth0_addr} dev eth0
  /vendor/bin/execns2 ${router_ns} /system/bin/ip route add default via ${eth0_gw} dev eth0

  # Start the IPv6 proxy that will enable use of IPv6 in the main namespace
  setprop ctl.start redroid_ipv6proxy

  # TODO: fix ril bug
  if [ "$redroid_radio" -eq "1" ]; then
    # /system/bin/ip rule add from all lookup main pref 5000
    # /system/bin/ip route add default via 7.8.8.1 dev radio0 table main
    # /system/bin/ip rule del from all lookup main
    for i in $(seq 1 5); do
      sleep 10s
      local radio_rule=$(/system/bin/ip rule | grep radio0)
      if [ -z "$radio_rule" ]; then
        echo "not found radio0 rule, restart ril-daemon"
        setprop ctl.restart vendor.ril-daemon
      else
        echo "found radio0 rule"
        break
      fi
    done
  fi

fi
