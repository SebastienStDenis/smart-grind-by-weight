# MTA Gateway

Small Docker service that polls the MTA's public subway GTFS-realtime feeds and
exposes a minimal arrivals list for the grinder's trains screensaver. The ESP32
can't parse protobuf feeds itself, so this runs on a desktop on the same network.

The image is published automatically to GHCR whenever `gateway/` changes on `main`.

## Run

```bash
docker run -d --name mta-gateway --restart unless-stopped \
  -p 8600:8600 -v mta-gateway:/data \
  ghcr.io/sebastienstdenis/mta-gateway:latest
```

Open `http://localhost:8600` to configure which trains to watch: search a
station, pick a line, pick a direction (shown with friendly labels like
"→ Manhattan"). Each watch also takes an optional walk time - how many minutes
it takes to walk to that platform. When set, countdowns carry the same tiny
catch dot as on the grinder: yellow when only a rushed walk still makes the
train, red when it can't be caught, no dot when it is reachable at a normal pace.
Drag a watch by its grip to reorder the list (or focus the grip and use the
arrow keys); that order is what the grinder's grouped screensaver page renders.
The page follows the OS light/dark setting and works on phones. Watches persist
in the `/data` volume.

## API

`GET /api/arrivals` — what the grinder polls:

```json
{
  "age_s": 12,
  "stale": false,
  "items": [
    {"route": "N", "color": "FCCC0A", "text_color": "000000",
     "station": "Queensboro Plaza", "direction": "Manhattan",
     "walk_min": 7, "mins": [3, 9, 15]}
  ]
}
```

`mins` are minutes until arrival at the watched stop (up to 4 per watch).
`walk_min` is the user-entered walk time to the platform in minutes (null when
no estimate is set). `stale` is true when the last successful MTA fetch is too
old to trust.

Also: `GET /api/health`, `GET /api/stations?q=`, `GET/POST /api/watches`,
`PATCH /api/watches/{index}` (set/clear `walk_min`),
`POST /api/watches/{index}/move` (`{"to": n}`, reorders the list),
`DELETE /api/watches/{index}`.

## Development

```bash
cd gateway
uv venv --python 3.12 .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/uvicorn app.main:app --port 8600 --reload
```

The watch list is reordered with [SortableJS](https://sortablejs.github.io/Sortable/),
vendored as `static/vendor/sortable.min.js` (MIT) so the page needs no CDN on a
local network. Refresh it by downloading the build for a newer version:

```bash
curl -fsSL -o static/vendor/sortable.min.js \
  https://cdn.jsdelivr.net/npm/sortablejs@1.15.7/Sortable.min.js
```

Station directory (`app/data/stations.json`) is a committed snapshot of the
[MTA Subway Stations dataset](https://data.ny.gov/Transportation/MTA-Subway-Stations/39hk-dx4f);
regenerate it with `python3 scripts/refresh_stations.py`.
