#!/bin/sh
if ! pgrep -x Xvfb >/dev/null; then (Xvfb :99 -screen 0 1280x720x24 >/tmp/xvfb.log 2>&1 &); sleep 1.5; fi
pgrep -x Xvfb >/dev/null && echo "Xvfb ok" || { echo "Xvfb falhou"; cat /tmp/xvfb.log; }
