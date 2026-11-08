#!/bin/sh
# pressif.sh <press args...>: press only while the test game is running (pid file from run.sh).
P=$(cat /tmp/es-prof/pid 2>/dev/null) && [ -n "$P" ] && kill -0 "$P" 2>/dev/null && exec /tmp/es-prof/press "$@"
echo "game not running; nothing pressed" >&2
exit 1
