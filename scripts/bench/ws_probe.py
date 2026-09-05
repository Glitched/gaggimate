#!/usr/bin/env -S uv run --quiet --with websockets python
"""Open the display's WebSocket and count pushed frames. Usage: ws_probe.py [host] [seconds]"""
import asyncio, json, sys, time
import websockets

async def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "gaggimate.local"
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 30
    t0 = time.time(); n = 0; types = {}
    try:
        async with websockets.connect(f"ws://{host}/ws", open_timeout=10, ping_interval=None) as ws:
            print("connected in %.2fs" % (time.time() - t0))
            while time.time() - t0 < seconds:
                try:
                    msg = await asyncio.wait_for(ws.recv(), timeout=3)
                except asyncio.TimeoutError:
                    print("  no message for 3 s"); continue
                n += 1
                try: tp = json.loads(msg).get("tp")
                except Exception: tp = "?"
                types[tp] = types.get(tp, 0) + 1
    except Exception as e:
        print("closed/error after %.1fs: %r" % (time.time() - t0, e))
    print("messages in %.0fs: %d %s" % (time.time() - t0, n, types))

asyncio.run(main())
