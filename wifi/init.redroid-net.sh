#!/vendor/bin/sh

# Check if VirtIO Wi-Fi is enabled. If so, create a mac80211_hwsim radio
# and run the DHCP client
wifi_virtio=`getprop ro.boot.redroid_virtiowifi`
case "$wifi_virtio" in
    1) wifi_mac_prefix=`getprop ro.boot.redroid_wifi_mac_prefix`
      if [ ! -n "$wifi_mac_prefix" ]; then
          wifi_mac_prefix=5555
      fi
      /vendor/bin/mac80211_create_radios 2 $wifi_mac_prefix || exit 1
      setprop ctl.start dhcpclient_wifi
      ;;
esac

# Check if WiFi with mac80211_hwsim is enabled. If so, run the WiFi init script. If not we just
# have to run the DHCP client in the default namespace and that will set up
# all the networking.
wifi_hwsim=`getprop ro.boot.redroid_virtiowifi`
case "$wifi_hwsim" in
    1) /vendor/bin/init.wifi.sh
       ;;
    *) setprop ctl.start dhcpclient_def
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
