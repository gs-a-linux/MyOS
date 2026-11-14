# 1. Create and mount the USB FunctionFS directory required by adbd
mkdir -p /dev/usb-ffs/adb
mount -t functionfs adb /dev/usb-ffs/adb

# 2. Set necessary environment variables for the daemon
export ANDROID_SERIAL=TBX306X
export ADBD_ACCESS_CONTROL_DISCONNECTED=1

# 3. Launch the adbd daemon in the background
adbd &
