#!/bin/bash
B=http://localhost:8080/api/settings
pass=0; fail=0
check() { # name expected actual
  if [ "$2" == "$3" ]; then pass=$((pass+1)); echo "PASS: $1"; else fail=$((fail+1)); echo "FAIL: $1 (expected $2, got $3)"; fi
}
jqf() { python3 -c "import sys,json; d=json.load(sys.stdin); print(json.dumps(d.get('$1')))"; }

# Wait for the simulator's HTTP server (up to 20 s) rather than sleeping a fixed time.
for i in $(seq 1 40); do
  curl -sf -o /dev/null http://localhost:8080/api/status && break
  if [ "$i" -eq 40 ]; then echo "simulator not reachable on localhost:8080 after 20 s" >&2; exit 1; fi
  sleep 0.5
done

# Leave the sim as we found it: snapshot the keys this suite changes and POST them back on exit.
KEYS="homekit targetSteamTemp targetWaterTemp homeAssistant pumpModelCoeffs brewDelay"
SNAP=$(curl -s $B)
restore() {
  [ -n "$SNAP" ] || return 0
  echo "$SNAP" | python3 -c "import sys,json; d=json.load(sys.stdin); print(json.dumps({k: d[k] for k in '$KEYS'.split() if k in d}))" \
    | curl -s -o /dev/null -X POST -H 'Content-Type: application/json' -d @- $B
}
trap restore EXIT

echo "--- 1. JSON partial update sets values"
r=$(curl -s -X POST -H 'Content-Type: application/json' -d '{"homekit": true, "targetSteamTemp": 152}' $B)
check "homekit set true via JSON" 'true' "$(echo "$r" | jqf homekit)"
check "steam temp set via JSON" '152' "$(echo "$r" | jqf targetSteamTemp)"

echo "--- 2. Omitted keys are untouched (the old contract cleared all booleans here)"
r=$(curl -s -X POST -H 'Content-Type: application/json' -d '{"targetWaterTemp": 88}' $B)
check "homekit survives unrelated JSON post" 'true' "$(echo "$r" | jqf homekit)"
check "water temp set" '88' "$(echo "$r" | jqf targetWaterTemp)"

echo "--- 3. Legacy form post is also partial now"
r=$(curl -s -X POST -d 'targetSteamTemp=149' $B)
check "homekit survives form post" 'true' "$(echo "$r" | jqf homekit)"
check "steam temp set via form" '149' "$(echo "$r" | jqf targetSteamTemp)"

echo "--- 4. Explicit boolean off works in both encodings"
r=$(curl -s -X POST -d 'homekit=0' $B)
check "form homekit=0 clears" 'false' "$(echo "$r" | jqf homekit)"
r=$(curl -s -X POST -H 'Content-Type: application/json' -d '{"homekit": true}' $B)
check "JSON re-enable" 'true' "$(echo "$r" | jqf homekit)"
r=$(curl -s -X POST -H 'Content-Type: application/json' -d '{"homekit": false}' $B)
check "JSON false clears" 'false' "$(echo "$r" | jqf homekit)"

echo "--- 5. Pump calibration coefficient post touches nothing else"
r=$(curl -s -X POST -H 'Content-Type: application/json' -d '{"homeAssistant": true}' $B)
check "HA enabled" 'true' "$(echo "$r" | jqf homeAssistant)"
r=$(curl -s -X POST -H 'Content-Type: application/json' -d '{"pumpModelCoeffs": "1.5,2.5"}' $B)
check "coeffs set" '"1.5,2.5"' "$(echo "$r" | jqf pumpModelCoeffs)"
check "HA survives coeff post" 'true' "$(echo "$r" | jqf homeAssistant)"

echo "--- 6. Malformed JSON is rejected"
code=$(curl -s -o /dev/null -w "%{http_code}" -X POST -H 'Content-Type: application/json' -d '{oops' $B)
check "bad JSON returns 400" '400' "$code"
code=$(curl -s -o /dev/null -w "%{http_code}" -X POST -H 'Content-Type: application/json' -d '[1,2]' $B)
check "non-object JSON returns 400" '400' "$code"

echo "--- 7. String-typed numbers in JSON (web form state) parse"
r=$(curl -s -X POST -H 'Content-Type: application/json' -d '{"targetSteamTemp": "154", "brewDelay": "1.25"}' $B)
check "string int parsed" '154' "$(echo "$r" | jqf targetSteamTemp)"
check "string double parsed" '1.25' "$(echo "$r" | jqf brewDelay)"

echo "--- 8. Values persist on GET"
r=$(curl -s $B)
check "GET reflects steam temp" '154' "$(echo "$r" | jqf targetSteamTemp)"
check "GET reflects coeffs" '"1.5,2.5"' "$(echo "$r" | jqf pumpModelCoeffs)"

echo; echo "RESULT: $pass passed, $fail failed"
[ $fail -eq 0 ]
