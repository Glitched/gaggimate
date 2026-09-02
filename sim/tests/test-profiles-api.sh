#!/bin/bash
B=http://localhost:8080/api/profiles
pass=0; fail=0
check() { if [ "$2" == "$3" ]; then pass=$((pass+1)); echo "PASS: $1"; else fail=$((fail+1)); echo "FAIL: $1 (expected [$2], got [$3])"; fi; }
jqf() { python3 -c "import sys,json; d=json.load(sys.stdin); print(json.dumps(d$1))" 2>/dev/null; }

# Wait for the simulator's HTTP server (up to 20 s) rather than sleeping a fixed time.
for i in $(seq 1 40); do
  curl -sf -o /dev/null http://localhost:8080/api/status && break
  if [ "$i" -eq 40 ]; then echo "simulator not reachable on localhost:8080 after 20 s" >&2; exit 1; fi
  sleep 0.5
done

# Leave the sim as we found it: remember the selected profile now, and on exit
# unfavourite and delete the profile this suite creates and re-select the original.
ORIG_SELECTED=$(curl -s "$B?minimal=1" | python3 -c "import sys,json; print(next((p['id'] for p in json.load(sys.stdin)['profiles'] if p.get('selected')), ''))" 2>/dev/null)
ID=
cleanup() {
  if [ -n "$ID" ]; then curl -s -o /dev/null -X POST "$B/$ID/unfavorite"; fi
  if [ -n "$ORIG_SELECTED" ]; then curl -s -o /dev/null -X POST "$B/$ORIG_SELECTED/select"; fi
  if [ -n "$ID" ]; then curl -s -o /dev/null -X DELETE "$B/$ID"; fi
}
trap cleanup EXIT

VALID='{"label":"API Test","type":"pro","description":"made by test","temperature":93.5,"phases":[{"name":"Pre-Infusion","phase":"preinfusion","valve":1,"duration":8,"pump":{"target":"pressure","pressure":3,"flow":0},"transition":{"type":"instant","target":"time","duration":0,"adaptive":false},"targets":[]},{"name":"Brew","phase":"brew","valve":1,"duration":25,"pump":{"target":"pressure","pressure":9,"flow":0},"transition":{"type":"linear","target":"time","duration":4,"adaptive":true},"targets":[{"type":"volumetric","operator":"gte","value":36}]}]}'

echo "--- 1. List works"
r=$(curl -s $B)
n=$(echo "$r" | python3 -c "import sys,json; print(len(json.load(sys.stdin)['profiles']))")
check "list returns profiles array" "yes" "$([ "$n" -ge 0 ] && echo yes)"
rmin=$(curl -s "$B?minimal=1")
haslabel=$(echo "$rmin" | python3 -c "import sys,json; p=json.load(sys.stdin)['profiles']; print('phases' in p[0] if p else False)")
check "minimal list omits phases" "False" "$haslabel"
hasfav=$(echo "$rmin" | python3 -c "import sys,json; p=json.load(sys.stdin)['profiles']; print('favorite' in p[0] and 'selected' in p[0] if p else False)")
check "minimal list carries favorite + selected" "True" "$hasfav"

echo "--- 2. Create via POST returns 201 with generated id"
code_body=$(curl -s -w "\n%{http_code}" -X POST -H 'Content-Type: application/json' -d "$VALID" $B)
code=$(echo "$code_body" | tail -1); r=$(echo "$code_body" | sed '$d')
check "create returns 201" '201' "$code"
ID=$(echo "$r" | jqf "['id']" | tr -d '"')
check "id assigned" "yes" "$([ -n "$ID" ] && echo yes)"
check "echo has 2 phases" '2' "$(echo "$r" | python3 -c "import sys,json; print(len(json.load(sys.stdin)['phases']))")"

echo "--- 3. GET by id"
r=$(curl -s $B/$ID)
check "load by id" '"API Test"' "$(echo "$r" | jqf "['label']")"

echo "--- 4. PUT full-document update (path id wins)"
UPDATED=$(echo "$VALID" | python3 -c "import sys,json; d=json.load(sys.stdin); d['label']='API Test v2'; d['id']='some-other-id'; d['temperature']=94.0; print(json.dumps(d))")
code_body=$(curl -s -w "\n%{http_code}" -X PUT -H 'Content-Type: application/json' -d "$UPDATED" $B/$ID)
code=$(echo "$code_body" | tail -1); r=$(echo "$code_body" | sed '$d')
check "PUT returns 200" '200' "$code"
check "label updated" '"API Test v2"' "$(echo "$r" | jqf "['label']")"
check "path id wins over body id" "\"$ID\"" "$(echo "$r" | jqf "['id']")"

echo "--- 5. Validation: invalid profile is refused, nothing stored"
code=$(curl -s -o /dev/null -w "%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"label":"broken"}' $B)
check "missing type/phases -> 422" '422' "$code"
code=$(curl -s -o /dev/null -w "%{http_code}" -X POST -H 'Content-Type: application/json' -d '{"label":"broken","type":"pro","phases":[]}' $B)
check "empty phases -> 422" '422' "$code"
code=$(curl -s -o /dev/null -w "%{http_code}" -X POST -H 'Content-Type: application/json' -d 'not json' $B)
check "malformed body -> 400" '400' "$code"
code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H 'Content-Type: application/json' -d '{"label":"x"}' $B/$ID)
check "invalid PUT -> 422" '422' "$code"
r=$(curl -s $B/$ID)
check "stored profile untouched after invalid PUT" '"API Test v2"' "$(echo "$r" | jqf "['label']")"

echo "--- 6. Conflict and not-found"
DUP=$(echo "$VALID" | python3 -c "import sys,json; d=json.load(sys.stdin); d['id']='$ID'; print(json.dumps(d))")
code=$(curl -s -o /dev/null -w "%{http_code}" -X POST -H 'Content-Type: application/json' -d "$DUP" $B)
check "POST with existing id -> 409" '409' "$code"
code=$(curl -s -o /dev/null -w "%{http_code}" $B/nope99)
check "GET unknown id -> 404" '404' "$code"
code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H 'Content-Type: application/json' -d "$VALID" $B/nope99)
check "PUT unknown id -> 404" '404' "$code"
code=$(curl -s -o /dev/null -w "%{http_code}" -X PATCH $B/$ID)
check "unsupported method -> 405" '405' "$code"

echo "--- 7. Select and favorite actions"
code=$(curl -s -o /dev/null -w "%{http_code}" -X POST $B/$ID/select)
check "select -> 200" '200' "$code"
r=$(curl -s $B/$ID)
check "profile now selected" 'true' "$(echo "$r" | jqf "['selected']")"
code=$(curl -s -o /dev/null -w "%{http_code}" -X POST $B/$ID/favorite)
check "favorite -> 200" '200' "$code"
code=$(curl -s -o /dev/null -w "%{http_code}" -X POST $B/$ID/dance)
check "unknown action -> 404" '404' "$code"

echo "--- 8. Delete"
DELID_R=$(curl -s -X POST -H 'Content-Type: application/json' -d "$VALID" $B)
DELID=$(echo "$DELID_R" | jqf "['id']" | tr -d '"')
code=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE $B/$DELID)
check "delete -> 200" '200' "$code"
code=$(curl -s -o /dev/null -w "%{http_code}" $B/$DELID)
check "deleted profile 404s" '404' "$code"

echo "--- 9. The socket is push-only: a req:* frame gets no response"
node - <<'NODE'
const ws = new WebSocket('ws://localhost:8080/ws');
ws.addEventListener('open', () => ws.send(JSON.stringify({ tp: 'req:profiles:save', rid: 't1', profile: { label: 'broken only' } })));
ws.addEventListener('message', e => {
  const m = JSON.parse(e.data);
  if (typeof m.tp === 'string' && m.tp.startsWith('res:')) {
    console.log('FAIL: socket answered a request (' + m.tp + '); commands belong on HTTP');
    ws.close(); process.exit(1);
  }
});
setTimeout(() => { console.log('PASS: no res:* within 3 s -- socket ignores requests'); process.exit(0); }, 3000);
NODE
[ $? -eq 0 ] && pass=$((pass+1)) || fail=$((fail+1))

echo; echo "RESULT: $pass passed, $fail failed"
[ $fail -eq 0 ]
