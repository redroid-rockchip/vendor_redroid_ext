#!/vendor/bin/sh

# Check if VirtIO Wi-Fi is enabled. If so, create a mac80211_hwsim radio
# and run the DHCP client
wifi_virtio=`getprop ro.boot.redroid_wifi`
case "$wifi_virtio" in
    1) wifi_mac_prefix=`getprop vendor.net.wifi_mac_prefix`
      /vendor/bin/create_radios2 2 `expr $RANDOM % 65535` || exit 1

      /system/bin/ip link add name br0 type bridge
      /system/bin/ip addr add 10.11.12.1 dev br0
      /system/bin/ip link set br0 mtu 1400
      /system/bin/ip link set br0 up

      unset sed_args
      for i in $(seq 1 10); do sed_args="$sed_args -e 's/<bssid$i>/00:$(echo $RANDOM | md5sum | sed 's/../&:/g' | cut -c 1-14)/'"; done
      echo sed $sed_args /vendor/etc/hostapd.conf | sh > /data/vendor/wifi/hostapd/redroid_hostapd.conf
      chown wifi:wifi /data/vendor/wifi/hostapd/redroid_hostapd.conf
      chmod 660 /data/vendor/wifi/hostapd/redroid_hostapd.conf

      # Start hostapd, the access point software
      setprop ctl.start emu_hostapd

      setprop ctl.start dhcpclient_wifi
      ;;
esac

# set up the second interface (for inter-emulator connections)
# if required
my_ip=`getprop vendor.net.shared_net_ip`
case "$my_ip" in
    "")
    ;;
    *) ifconfig eth1 "$my_ip" netmask 255.255.255.0 up
    ;;
esac
