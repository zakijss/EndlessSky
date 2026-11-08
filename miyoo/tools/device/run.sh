#!/bin/sh
# run.sh "<test name>"|- [seconds] ("-" plays normally, no test): one rendered test with ES_PROFILE and sound (Onion's padsp
# shim, as the launcher uses), MainUI paused meanwhile; memory sampled every 10 s into mem.log.
# CONF=<folder> starts from a copy of that config instead of the test one.
G="/mnt/SDCARD/Roms/PORTS/Games/Endless Sky"; D=/tmp/es-prof; GOV=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
T="$1"; SECS=${2:-150}
M=$(pgrep MainUI); OLD=$(cat $GOV)
kill -STOP $M; trap 'kill -CONT $M; echo $OLD > $GOV' EXIT
echo performance > $GOV
rm -rf $D/c && mkdir -p $D/c && cp -r ${CONF:-/tmp/es-tests/config}/* $D/c/
cd "$G" || exit 1
if [ "$T" = "-" ]; then
  ES_GRAB_INPUT=1 ES_PROFILE=1 LD_LIBRARY_PATH=/mnt/SDCARD/miyoo/lib:/config/lib:/lib LD_PRELOAD=/mnt/SDCARD/miyoo/lib/libpadsp.so HOME=$D $D/endless-sky --resources . --config $D/c > $D/log 2>&1 &
else
  ES_GRAB_INPUT=1 ES_PROFILE=1 LD_LIBRARY_PATH=/mnt/SDCARD/miyoo/lib:/config/lib:/lib LD_PRELOAD=/mnt/SDCARD/miyoo/lib/libpadsp.so HOME=$D $D/endless-sky --resources . --test "$T" --config $D/c --debug > $D/log 2>&1 &
fi
P=$!; echo $P > $D/pid; i=0; : > $D/mem.log
while kill -0 $P 2>/dev/null && [ $i -lt $SECS ]; do
  sleep 1; i=$((i+1))
  [ $((i % 10)) = 0 ] && echo "$i $(grep -E 'VmRSS|VmSwap' /proc/$P/status | tr -s ' ' | tr '\n' ' ')" >> $D/mem.log
done
kill $P 2>/dev/null; wait $P; echo "exit $? after ${i}s" >> $D/log; rm -f $D/pid
