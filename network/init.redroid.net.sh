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

init_wlan() {
  /vendor/bin/create_radios2 2 `expr $RANDOM % 65535`
  if [ "$?" -eq "0" ]; then
    # create a bridge
    local wifi_gateway=`getprop ro.boot.redroid_wifi_gateway`
    if [ ! -n "$wifi_gateway" ]; then
      wifi_gateway='7.7.7.1/24'
    fi
    /system/bin/ip link add name br0 type bridge
    /system/bin/ip addr add ${wifi_gateway} dev br0
    /system/bin/ip link set br0 mtu 1400
    /system/bin/ip link set br0 up
    /system/bin/ip link set wlan1 name tap0

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
  /system/bin/ip link add name radio0 type bridge
  /system/bin/ip addr add 7.8.8.2/16 dev radio0
  /system/bin/ip link set radio0 up

  echo "start redroid_vlte"
  setprop ctl.start redroid_vlte
  # echo "restart vendor.ril-daemon"
  # setprop ctl.stop vendor.ril-daemon
  # setprop ctl.stop redroid_vlte
  # setprop ctl.start redroid_vlte
  # setprop ctl.start vendor.ril-daemon
}

redroid_wifi=`getprop ro.boot.redroid_wifi`
redroid_radio=`getprop ro.boot.redroid_radio`
if [ "$redroid_wifi" -eq "1" -o "$redroid_radio" -eq "1" ]; then

  if [ "$redroid_wifi" -eq "1" ]; then
    echo "init wlan"
    init_wlan
  fi
  if [ "$redroid_radio" -eq "1" ]; then
    echo "init radio"
    init_radio
  fi

  echo "update ip rule"
  /system/bin/ip rule add from all lookup main pref 5000

  local eth0_addr=""
  local eth0_gw=""
  for i in $(seq 1 20); do
    if [ -z "$eth0_addr" ]; then
      eth0_addr=$(/system/bin/ip addr show dev eth0 | grep 'inet ' | awk '{print $2,$3,$4}')
    fi
    if [ -z "$eth0_gw" ]; then
      eth0_gw=$(/system/bin/ip route get 8.8.8.8 | head -n 1 | awk '{print $3}')
    fi
    if [ -n "$eth0_addr" -a -n "$eth0_gw" ]; then
      break
    fi
    sleep 1s
  done
  echo "find eth0 addr: ${eth0_addr}, gw: ${eth0_gw}"

  echo "rename eth0 to veth0"
  /system/bin/ip link set eth0 down
  /system/bin/ip link set eth0 name veth0

  echo "init veth0"
  /system/bin/ip addr add ${eth0_addr} dev veth0
  /system/bin/ip link set veth0 up
  /system/bin/ip route add default via ${eth0_gw} dev veth0

  for i in $(seq 1 120); do
    echo "update ip rule: ${i}"
    /system/bin/ip rule add from all lookup main pref 5000 2>/dev/null
    /system/bin/ip route add default via ${eth0_gw} dev veth0 2>/dev/null
    sleep 5s
  done

fi
