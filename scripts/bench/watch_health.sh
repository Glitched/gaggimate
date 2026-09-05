#!/bin/zsh
# One line a minute for hours. Usage: watch_health.sh [host] [minutes] >> health.log
H=${1:-gaggimate.local}; T=${2:-720}; end=$(( $(date +%s) + T*60 ))
printf "%-8s %-7s %-7s %-7s %-4s %-4s %-4s %-4s %-5s %-4s %-6s %s\n" time free largest min ws skip drop rssi retx tx httpS topcpu
while [ $(date +%s) -lt $end ]; do
  t0=$(date +%s.%N); o=$(curl -s -m 8 "http://$H/api/ota"); t1=$(date +%s.%N)
  h=$(curl -s -m 8 "http://$H/api/debug/heap?cpu=1") # arms CPU sampling: one shifted frame per 10 s while this runs
  python3 - "$o" "$h" "$t0" "$t1" <<'PY'
import sys, json, time
try:
    o=json.loads(sys.argv[1]); h=json.loads(sys.argv[2]); n=h["now"]
except Exception:
    print(time.strftime('%H:%M:%S'), "unreachable"); sys.exit(0)
ts=[t for t in h.get("tasks",[]) if "cpuPct" in t and not t["name"].startswith("IDLE")]
top=max(ts, key=lambda t:t["cpuPct"]) if ts else None
print("%-8s %-7d %-7d %-7d %-4d %-4d %-4d %-4d %-5d %-4d %-6.2f %s" % (time.strftime('%H:%M:%S'), n["free"], n["largest"], n["minFree"], o["wsClients"], o["wsSkippedFrames"], o["wsDisconnects"], o["wifiRssi"], o["link"]["retransmitsWindow"], o["link"]["txFramesWindow"], float(sys.argv[4])-float(sys.argv[3]), ("%s %.1f%%" % (top["name"], top["cpuPct"])) if top else "-"))
PY
  sleep 60
done
