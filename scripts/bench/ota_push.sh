#!/bin/zsh
# Raw-body OTA upload, then wait for the device. Usage: ota_push.sh <firmware.bin> [host] [--token-file FILE]
# The token is the device's otaUploadToken setting: $GM_OTA_TOKEN or --token-file.
IMG=$1; H=${2:-gaggimate.local}; TOKEN=${GM_OTA_TOKEN:-}
[ "$3" = "--token-file" ] && TOKEN=$(cat "$4")
[ -z "$TOKEN" ] && { echo "no token: set GM_OTA_TOKEN or pass --token-file"; exit 1; }
START=$(date +%s)
R=$(curl -s -m 600 -X POST -H "X-OTA-Token: $TOKEN" -H "Expect:" -H "Content-Type: application/octet-stream" --data-binary @"$IMG" "http://$H/api/ota/upload")
echo "upload: ${R:-<no response: the link stalled and the device aborted after 30 s>} in $(( $(date +%s) - START )) s"
echo "$R" | grep -q '"ok"' || exit 1
sleep 15
until curl -s -m 3 "http://$H/api/ota" 2>/dev/null | grep -q displayVersion; do [ $(( $(date +%s) - START )) -gt 400 ] && { echo "device did not come back"; exit 1; }; sleep 3; done
curl -s -m 5 "http://$H/api/ota" | python3 -c 'import json,sys; d=json.load(sys.stdin); print("back:", d["displayVersion"], "| heapFree", d["heapFree"])'
