# Shot notes

Shot notes (rating, dose in/out, grind setting, taste balance, free text) are
stored on the device as `/h/<id>.json` next to the shot's `.slog` recording and
are read and written over plain HTTP:

- `GET /api/history/{id}.json` — the notes for a shot (404 when it has none).
- `PUT /api/history/{id}.json` — save the notes; also updates the shot index's
  rating and, when `doseOut` is given, its recorded volume.
- `DELETE /api/history/{id}` — delete the shot together with its notes.

The routes are specified in [`http-api.yaml`](http-api.yaml) and the document
shape in [`schema/shot_notes.json`](../schema/shot_notes.json).

The `req:history:*` / `res:history:*` WebSocket messages this page used to
describe were removed on 2026-08-31; the socket now only pushes `evt:*` events
(see [`websocket-api.yaml`](websocket-api.yaml)).
