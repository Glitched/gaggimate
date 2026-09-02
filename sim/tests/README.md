# HTTP API end-to-end tests

Black-box tests for the display firmware's HTTP API (`docs/http-api.yaml`),
run against the simulator, which serves the real `WebUIPlugin` code.

```shell
pio run -e display-sim
SDL_VIDEODRIVER=dummy ./.pio/build/display-sim/program &   # headless
./sim/tests/test-settings-api.sh
./sim/tests/test-profiles-api.sh
./sim/tests/test-machine-api.sh     # mode, process, targets, ota, history, reorder
```

Each script prints PASS/FAIL per assertion and exits non-zero on any failure.
They mutate the simulator's persisted state (`sim_data/`), so start from a
fresh directory when determinism matters. `test-profiles-api.sh` needs `node`
(>=22, for the built-in WebSocket client) and `python3` for JSON assertions.
