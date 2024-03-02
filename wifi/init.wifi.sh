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

NAMESPACE="router"

# createns will have created a file that contains the process id (pid) of a
# process running in the network namespace. This pid is needed for some commands
# to access the namespace.
PID=$(</data/vendor/var/run/netns/${NAMESPACE}.pid)

ETH0_ADDR=$(/vendor/bin/ip addr show dev eth0 | grep 'inet ' | awk '{print $2,$3,$4}')
ETH0_GW=$(/vendor/bin/ip route get 8.8.8.8 | head -n 1 | awk '{print $3}')
WLAN1_PHY=phy$(/vendor/bin/iw dev wlan1 info | awk -F 'wiphy +' '{print $2}' | awk NF)

/vendor/bin/ip link set eth0 netns ${PID}
/vendor/bin/execns ${NAMESPACE} /vendor/bin/ip link set eth0 up
/vendor/bin/execns ${NAMESPACE} /vendor/bin/ip addr add ${ETH0_ADDR} dev eth0
/vendor/bin/execns ${NAMESPACE} /vendor/bin/ip route add default via ${ETH0_GW} dev eth0

/vendor/bin/ip link add radio0 type veth peer name radio0-peer netns ${PID}

# Enable privacy addresses for radio0, this is done by the framework for wlan0
sysctl -wq net.ipv6.conf.radio0.use_tempaddr=2

/vendor/bin/ip addr add 192.168.200.2/24 broadcast 192.168.200.255 dev radio0
/vendor/bin/execns ${NAMESPACE} /vendor/bin/ip addr add 192.168.200.1/24 dev radio0-peer
/vendor/bin/execns ${NAMESPACE} sysctl -wq net.ipv6.conf.all.forwarding=1
/vendor/bin/execns ${NAMESPACE} /vendor/bin/ip link set radio0-peer up

/vendor/bin/execns ${NAMESPACE} /vendor/bin/ip link set eth0 up

/vendor/bin/execns ${NAMESPACE} /system/bin/iptables -w -W 50000 -t nat -A POSTROUTING -s 192.168.232.0/21 -o eth0 -j MASQUERADE
/vendor/bin/execns ${NAMESPACE} /system/bin/iptables -w -W 50000 -t nat -A POSTROUTING -s 192.168.200.0/24 -o eth0 -j MASQUERADE
/vendor/bin/ip link set radio0 up

/vendor/bin/iw phy ${WLAN1_PHY} set netns ${PID}
/vendor/bin/execns ${NAMESPACE} /vendor/bin/ip addr add 192.168.232.1/21 dev wlan1
/vendor/bin/execns ${NAMESPACE} /vendor/bin/ip link set wlan1 mtu 1400
/vendor/bin/execns ${NAMESPACE} /vendor/bin/ip link set wlan1 up

# setprop ctl.start netmgr

# setprop ctl.start wifi_forwarder

# If this is a clean boot we need to copy the hostapd configuration file to the
# data partition where netmgr can change it if needed. If it already exists we
# need to preserve the existing settings.
if [ ! -f /data/vendor/wifi/hostapd/hostapd.conf ]; then
    cp /vendor/etc/simulated_hostapd.conf /data/vendor/wifi/hostapd/hostapd.conf
    chown wifi:wifi /data/vendor/wifi/hostapd/hostapd.conf
    chmod 660 /data/vendor/wifi/hostapd/hostapd.conf
fi

# Start hostapd, the access point software
setprop ctl.start emu_hostapd

setprop ctl.start dhcpserver

ifconfig radio0 -multicast