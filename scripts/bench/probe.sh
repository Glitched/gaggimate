#!/bin/zsh
# Wi-Fi path probes. Usage: probe.sh [host]
H=${1:-gaggimate.local}; D=$(dirname "$0")
echo "=== probe $H $(date +%H:%M:%S) version=$(curl -s -m 5 http://$H/api/ota | python3 -c 'import json,sys; print(json.load(sys.stdin).get("displayVersion","?"))' 2>/dev/null)"
A=$(curl -s -m 10 --compressed "http://$H/" | grep -o -E "/assets/[A-Za-z0-9_.-]+\.js" | head -1); [ -z "$A" ] && A=/assets/app.js
for i in 1 2 3; do curl -s -m 60 -o /dev/null -w "download $A: %{size_download} B at %{speed_download} B/s (%{time_total}s)\n" "http://$H$A"; done
perl -e 'alarm 25; exec @ARGV' -- ping -c 10 -i 0.4 $H 2>&1 | tail -1 | sed 's/^/ping: /'
BLOB=$(mktemp); head -c 524288 /dev/urandom > "$BLOB"
curl -s -m 60 -o /dev/null -w "upload probe (unauthenticated, rejected after the body): %{speed_upload} B/s (%{time_total}s, http %{http_code})\n" -X POST -H "Content-Type: application/octet-stream" --data-binary @"$BLOB" "http://$H/api/ota/upload"
rm -f "$BLOB"
for i in 1 2 3; do curl -s -m 15 -o /dev/null -w "status: %{time_total}s\n" "http://$H/api/status"; done
uv run --quiet --with websockets python "$D/ws_probe.py" "$H" 30 2>&1 | tail -1 | sed 's/^/ws: /'
curl -s -m 5 http://$H/api/ota | python3 -c 'import json,sys; d=json.load(sys.stdin); l=d.get("link",{}); print("link: retx", l.get("retransmits"), "/ tx", l.get("txFrames"), "| window retx", l.get("retransmitsWindow"), "/", l.get("txFramesWindow"), "| rssi", d.get("wifiRssi","n/a"))' 2>/dev/null
