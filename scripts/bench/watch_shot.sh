#!/bin/zsh
# Poll the display through a shot. Usage: watch_shot.sh [host] [seconds] [interval]
# /api/debug/heap and /api/status are cheap; /api/ota walks LittleFS and its panel figures are a 10 s window,
# so it is read only every OTA_EVERY seconds.
H=${1:-gaggimate.local}; T=${2:-240}; I=${3:-3}; OTA_EVERY=12; end=$(( $(date +%s) + T )); lastota=0
printf "%-8s %-7s %-9s %-9s %-9s %-7s %-5s %-6s %-6s %s\n" time mode heapFree largest heapMin under late vsync fps checkpoints
while [ $(date +%s) -lt $end ]; do
  now=$(date +%s); ota=""
  if [ $(( now - lastota )) -ge $OTA_EVERY ]; then ota=$(curl -s -m 3 "http://$H/api/ota"); lastota=$now; fi
  h=$(curl -s -m 3 "http://$H/api/debug/heap") && st=$(curl -s -m 3 "http://$H/api/status") && python3 - "$h" "$st" "$ota" <<'PY'
import sys, json, time
h=json.loads(sys.argv[1]); s=json.loads(sys.argv[2]); o=json.loads(sys.argv[3]) if sys.argv[3] else None
n=h["now"]; cps=[c for c in h.get("checkpoints",[]) if c["label"].startswith("brew")]
cp=" ".join("%s@%ds=%d" % (c["label"], c["t"]//1000, c["free"]) for c in cps[-2:])
if o: print("%-8s %-7s %-9d %-9d %-9d %-7d %-5d %-6.2f %-6.1f %s" % (time.strftime('%H:%M:%S'), s.get('mode'), n["free"], n["largest"], n["minFree"], o["panelUnderruns"], o["panelLateVsyncs"], o["panelVsyncHz"], o["uiFps"], cp))
else: print("%-8s %-7s %-9d %-9d %-9d %-7s %-5s %-6s %-6s %s" % (time.strftime('%H:%M:%S'), s.get('mode'), n["free"], n["largest"], n["minFree"], "", "", "", "", cp))
PY
  sleep $I
done
