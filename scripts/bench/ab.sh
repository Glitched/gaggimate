#!/bin/zsh
# Interleaved A/B over USB: flash A, probe twice, flash B, probe twice, flash A, probe twice.
# Usage: ab.sh <imageA.bin> <imageB.bin> [host]
A=$1; B=$2; H=${3:-gaggimate.local}; D=$(dirname "$0")
wait_up() { local start=$(date +%s); sleep 20; until curl -s -m 3 "http://$H/api/ota" 2>/dev/null | grep -q displayVersion; do [ $(( $(date +%s) - start )) -gt 240 ] && { echo "no comeback"; return 1; }; sleep 3; done; sleep 20; }
run() { echo "##### flashing $2 $(date +%H:%M:%S)"; zsh "$D/usb_flash.sh" "$1"; wait_up || return 1; zsh "$D/probe.sh" "$H"; sleep 90; zsh "$D/probe.sh" "$H"; }
run "$A" "A: $(basename $A)"; run "$B" "B: $(basename $B)"; run "$A" "A again"
echo "AB DONE $(date +%H:%M:%S)"
