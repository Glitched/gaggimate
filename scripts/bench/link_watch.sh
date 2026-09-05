#!/bin/zsh
# Per-window controller-link and Wi-Fi figures every 20 s. Usage: link_watch.sh [host] [seconds]
H=${1:-gaggimate.local}; end=$(( $(date +%s) + ${2:-300} ))
while [ $(date +%s) -lt $end ]; do
  curl -s -m 6 "http://$H/api/ota" | python3 -c 'import json,sys,time; d=json.load(sys.stdin); l=d["link"]; print("%s tx %3d retx %3d (%3.0f%%) rtt %4s rssi %d ws %d skip %d drop %d" % (time.strftime("%H:%M:%S"), l["txFramesWindow"], l["retransmitsWindow"], 100.0*l["retransmitsWindow"]/max(1,l["txFramesWindow"]), l["rttMs"], d["wifiRssi"], d["wsClients"], d["wsSkippedFrames"], d["wsDisconnects"]))' 2>/dev/null
  sleep 20
done
