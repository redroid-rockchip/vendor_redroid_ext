# 准备工作

宿主机需要开启mac80211_hwsim内核模块
```
CONFIG_MAC80211_HWSIM=y
```

宿主机需切换到iptables-legacy，然后重启
```
update-alternatives --set iptables /usr/sbin/iptables-legacy
update-alternatives --set ip6tables /usr/sbin/ip6tables-legacy
```
