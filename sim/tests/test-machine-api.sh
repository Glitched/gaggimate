#!/bin/bash
# HTTP command surface: the REST equivalents of the req:* WebSocket messages.
# Runs against the simulator (see README.md). Avoids /api/ota/start and
# /api/autotune's long-running effects; exercises everything else.
B=http://localhost:8080/api
pass=0; fail=0
check() { if [ "$2" == "$3" ]; then pass=$((pass+1)); echo "PASS: $1"; else fail=$((fail+1)); echo "FAIL: $1 (expected [$2], got [$3])"; fi; }
code() { curl -s -o /dev/null -w "%{http_code}" "$@"; }
body() { curl -s "$@"; }
jget() { python3 -c "import sys,json; d=json.load(sys.stdin); print(d$1)" 2>/dev/null; }
J='-H Content-Type:application/json'

echo "--- mode"
check "mode by name -> 200"      200 "$(code -X POST $J -d '{"mode":"brew"}' $B/mode)"
check "status reports brew (1)"  1   "$(body $B/status | jget "['mode']")"
check "mode by number -> 200"    200 "$(code -X POST $J -d '{"mode":0}' $B/mode)"
check "status reports standby"   0   "$(body $B/status | jget "['mode']")"
check "bad mode -> 400"          400 "$(code -X POST $J -d '{"mode":"espresso"}' $B/mode)"
check "GET /api/mode -> 405"     405 "$(code $B/mode)"

echo "--- process / grind / flush"
curl -s -o /dev/null -X POST $J -d '{"mode":"brew"}' $B/mode
check "process activate"         200 "$(code -X POST $B/process/activate)"
check "process deactivate"       200 "$(code -X POST $B/process/deactivate)"
check "process clear"            200 "$(code -X POST $B/process/clear)"
check "unknown process action"   404 "$(code -X POST $B/process/explode)"
check "grind activate"           200 "$(code -X POST $B/grind/activate)"
check "grind deactivate"         200 "$(code -X POST $B/grind/deactivate)"
check "flush"                    200 "$(code -X POST $B/flush)"
check "flush via GET -> 405"     405 "$(code $B/flush)"
curl -s -o /dev/null -X POST $B/process/deactivate

echo "--- targets"
check "set temperature 92"       200 "$(code -X PUT $J -d '{"value":92}' $B/targets/temperature)"
check "status tt == 92"          92  "$(body $B/status | jget "['tt']" | sed 's/\.0$//')"
check "raise temperature"        200 "$(code -X POST $B/targets/temperature/raise)"
check "status tt == 93"          93  "$(body $B/status | jget "['tt']" | sed 's/\.0$//')"
check "lower temperature"        200 "$(code -X POST $B/targets/temperature/lower)"
check "status tt == 92 again"    92  "$(body $B/status | jget "['tt']" | sed 's/\.0$//')"
check "temperature clamps"       200 "$(code -X PUT $J -d '{"value":999}' $B/targets/temperature)"
check "clamped to 160"           160 "$(body $B/status | jget "['tt']" | sed 's/\.0$//')"
curl -s -o /dev/null -X PUT $J -d '{"value":93}' $B/targets/temperature
check "non-numeric value -> 400" 400 "$(code -X PUT $J -d '{"value":"hot"}' $B/targets/temperature)"
check "brew raise"               200 "$(code -X POST $B/targets/brew/raise)"
check "brew lower"               200 "$(code -X POST $B/targets/brew/lower)"
check "brew absolute -> 404"     404 "$(code -X PUT $J -d '{"value":36}' $B/targets/brew)"
check "grind absolute 18"        200 "$(code -X PUT $J -d '{"value":18}' $B/targets/grind)"
check "grind zero -> 400"        400 "$(code -X PUT $J -d '{"value":0}' $B/targets/grind)"
check "unknown target -> 404"    404 "$(code -X POST $B/targets/pressure/raise)"
check "volumetric mode on"       True "$(body -X PUT $J -d '{"volumetric":true}' $B/targets/mode | jget "['volumetric']")"
check "volumetric mode off"      False "$(body -X PUT $J -d '{"volumetric":false}' $B/targets/mode | jget "['volumetric']")"
check "mode needs a boolean"     400 "$(code -X PUT $J -d '{"volumetric":"yes"}' $B/targets/mode)"

echo "--- ota"
check "GET /api/ota has version" yes "$(body $B/ota | jget "['displayVersion']" | grep -q . && echo yes)"
check "POST channel nightly -> 202" 202 "$(code -X POST $J -d '{"channel":"nightly"}' $B/ota)"
check "channel is nightly"       nightly "$(body $B/ota | jget "['channel']")"
curl -s -o /dev/null -X POST $J -d '{"channel":"latest"}' $B/ota
check "channel back to latest"   latest "$(body $B/ota | jget "['channel']")"
check "DELETE /api/ota -> 405"   405 "$(code -X DELETE $B/ota)"

echo "--- history"
check "rebuild -> 202"           202 "$(code -X POST $B/history/rebuild)"
check "notes PUT on bogus id"    200 "$(code -X PUT $J -d '{"rating":3,"doseOut":"36.0"}' $B/history/999999.json)"
check "notes readable back"      3   "$(body $B/history/999999.json | jget "['rating']")"
check "DELETE bogus id"          200 "$(code -X DELETE $B/history/999999)"
check "notes gone"               404 "$(code $B/history/999999.json)"
check "non-numeric id -> 400"    400 "$(code -X DELETE $B/history/abc)"

echo "--- profiles reorder"
IDS=$(body "$B/profiles?minimal=1" | python3 -c "import sys,json; ids=[p['id'] for p in json.load(sys.stdin)['profiles']]; print(json.dumps(ids[::-1]))")
check "reorder -> 200"           200 "$(code -X POST $J -d "{\"ids\":$IDS}" $B/profiles/reorder)"
FIRST=$(echo "$IDS" | python3 -c "import sys,json; print(json.load(sys.stdin)[0])")
check "list follows new order"   "$FIRST" "$(body "$B/profiles?minimal=1" | jget "['profiles'][0]['id']")"
check "reorder without ids -> 400" 400 "$(code -X POST $J -d '{}' $B/profiles/reorder)"
echo "--- profile list cache invalidates on favourite changes"
FAVID=$(body "$B/profiles?minimal=1" | python3 -c "import sys,json; print([p['id'] for p in json.load(sys.stdin)['profiles'] if p['favorite']][0])")
curl -s -o /dev/null -X POST $B/profiles/$FAVID/unfavorite
check "unfavourite shows in cached list" False "$(body "$B/profiles?minimal=1" | python3 -c "import sys,json; print([p['favorite'] for p in json.load(sys.stdin)['profiles'] if p['id']=='$FAVID'][0])")"
curl -s -o /dev/null -X POST $B/profiles/$FAVID/favorite
check "re-favourite shows in cached list" True "$(body "$B/profiles?minimal=1" | python3 -c "import sys,json; print([p['favorite'] for p in json.load(sys.stdin)['profiles'] if p['id']=='$FAVID'][0])")"
check "GET /api/ota reports psram" yes "$(body $B/ota | jget "['psramFree']" | grep -q . && echo yes)"
ORIG=$(echo "$IDS" | python3 -c "import sys,json; print(json.dumps(json.load(sys.stdin)[::-1]))")
curl -s -o /dev/null -X POST $J -d "{\"ids\":$ORIG}" $B/profiles/reorder

echo; echo "$pass passed, $fail failed"; [ "$fail" -eq 0 ]
