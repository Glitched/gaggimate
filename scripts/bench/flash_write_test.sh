#!/bin/zsh
# Flash-write panel test without a shot: create and delete a throwaway profile N times (LittleFS writes and
# erases) and read the panel window afterwards. Usage: flash_write_test.sh [host] [rounds]
H=${1:-gaggimate.local}; N=${2:-6}
BODY='{"label":"Panel write test","type":"pro","description":"temporary, created by flash_write_test.sh","temperature":93.5,"phases":[{"name":"Pre-Infusion","phase":"preinfusion","valve":1,"duration":8,"pump":{"target":"pressure","pressure":3,"flow":0},"transition":{"type":"instant","target":"time","duration":0,"adaptive":false},"targets":[]},{"name":"Brew","phase":"brew","valve":1,"duration":25,"pump":{"target":"pressure","pressure":9,"flow":0},"transition":{"type":"linear","target":"time","duration":4,"adaptive":true},"targets":[{"type":"volumetric","operator":"gte","value":36}]}]}'
read_win() { curl -s -m 4 "http://$H/api/ota" | python3 -c 'import json,sys; d=json.load(sys.stdin); print("  window: underruns=%d late=%d vsync=%.2f fps=%.1f heapFree=%d" % (d["panelUnderruns"], d["panelLateVsyncs"], d["panelVsyncHz"], d["uiFps"], d["heapFree"]))'; }
echo "before:"; read_win
START=$(date +%s); ids=()
for i in $(seq 1 $N); do
  id=$(curl -s -m 5 -X POST -H 'Content-Type: application/json' -d "$BODY" "http://$H/api/profiles" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("id",""))')
  [ -n "$id" ] && ids+=("$id")
  sleep 1
done
for id in "${ids[@]}"; do curl -s -m 5 -o /dev/null -X DELETE "http://$H/api/profiles/$id"; sleep 1; done
echo "wrote and deleted ${#ids[@]} profiles in $(( $(date +%s) - START )) s"
sleep 2; echo "windows overlapping the writes, then after:"; read_win; sleep 10; read_win; sleep 10; read_win
echo "leftovers named 'Panel write test':"; curl -s -m 5 "http://$H/api/profiles?minimal=1" | python3 -c 'import json,sys; print(sum(1 for p in json.load(sys.stdin)["profiles"] if p.get("label")=="Panel write test"))'
