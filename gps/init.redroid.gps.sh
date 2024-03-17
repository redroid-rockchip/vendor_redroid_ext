#!/vendor/bin/sh

if [ ! -f /data/vendor/gps/gnss ]; then
    echo "LatitudeDegrees=30.281026818001678" > /data/vendor/gps/gnss
    echo "LongitudeDegrees=120.01934876982831" >> /data/vendor/gps/gnss
    echo "AltitudeMeters=1.60062531" >> /data/vendor/gps/gnss
    echo "BearingDegrees=0" >> /data/vendor/gps/gnss
    echo "SpeedMetersPerSec=0" >> /data/vendor/gps/gnss
    chmod 777 /data/vendor/gps/gnss
fi
