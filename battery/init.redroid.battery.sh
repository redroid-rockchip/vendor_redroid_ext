#!/vendor/bin/sh

SRC_PATH=/vendor/etc/init/hw/battery/power_supply
DEST_PATH=/data/vendor/battery/power_supply

copy_files() {
    if [ -f $DEST_PATH/battery/v ]; then
        ret=$(cmp -s $SRC_PATH/battery/v $DEST_PATH/battery/v || echo -n 1)
        if [ "$ret" != "1" ]; then
            return
        fi
    fi
    rm -rf $DEST_PATH
    cp -r $SRC_PATH $DEST_PATH
    echo `expr 80 + $RANDOM % 20` > $DEST_PATH/battery/capacity
    chmod -R 777 $DEST_PATH
}

copy_files

mount --bind $DEST_PATH /sys/class/power_supply
chmod 755 /sys/class/power_supply
