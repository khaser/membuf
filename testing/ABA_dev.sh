#!/usr/bin/env -S sudo bash

echo "20" > /sys/class/membuf/dev_cnt
sleep 1
fd=
exec {fd}<> <(:)
( sleep 1; echo some text; ) > /dev/membuf7 2>&$fd &
write_pid=$!
echo "1" > /sys/class/membuf/dev_cnt
wait $write_pid || grep -q "No such device" <&$fd && exit 0
exit 1
