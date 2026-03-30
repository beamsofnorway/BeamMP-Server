# BeamMP HTTP API

## Overview

- Base URL: `http://<host>:<port>`
- Descriptor endpoints: `GET /` and `GET /api`
- Health endpoint: `GET /health`
- Authenticated endpoints: all other `/api/*` routes
- Response shape:

```json
{
  "ok": true,
  "data": {}
}
```

or:

```json
{
  "ok": false,
  "error": "message"
}
```

## Configuration

Configure the API in `ServerConfig.toml`:

```toml
[HttpApi]
Enabled = true
Host = "127.0.0.1"
Port = 30815
Token = ""
```

- `Enabled`: enables or disables the HTTP API server
- `Host`: bind address; default keeps the API loopback-only
- `Port`: listen port; defaults to `General.Port + 1`
- `Token`: optional bearer token; if empty, authenticated routes are limited to loopback clients

Environment overrides:

- `BEAMMP_HTTP_API_ENABLED`
- `BEAMMP_HTTP_API_HOST`
- `BEAMMP_HTTP_API_PORT`
- `BEAMMP_HTTP_API_TOKEN`

## Authentication

- `GET /health` is public
- `GET /` is public and returns the API descriptor
- All authenticated routes require one of:
  - loopback access when `HttpApi.Token` is empty
  - `Authorization: Bearer <token>` when `HttpApi.Token` is set

## Read Endpoints

- `GET /api/actions`
- `GET /api/logs/recent?limit=50&after_sequence=123`
- `GET /api/events/recent?limit=50&after_sequence=123`
- `GET /api/server/status`
- `GET /api/server/version`
- `GET /api/server/subsystems`
- `GET /api/lua/states`
- `GET /api/players?include_vehicles=true`
- `GET /api/players/find?name=<name>&prefix_match=true&limit=25`
- `GET /api/players/get?id=<id>`
- `GET /api/players/get?name=<name>&prefix_match=true`
- `GET /api/players/vehicles?id=<id>`
- `GET /api/players/vehicle-positions?id=<id>&include_parsed=true`
- `GET /api/players/vehicle-position?player_id=<id>&vehicle_id=<id>`
- `GET /api/players/vehicle-position/raw?player_id=<id>&vehicle_id=<id>`
- `GET /api/players/vehicle-position/parsed?player_id=<id>&vehicle_id=<id>`
- `GET /api/vehicles`
- `GET /api/vehicles/positions`
- `GET /api/settings`
- `GET /api/settings/get?category=General&key=Name`
- `GET /api/mods`

## Write Endpoints

- `POST /api/chat/send`
- `POST /api/players/kick`
- `POST /api/players/disconnect`
- `POST /api/settings/set`
- `POST /api/mods/reload`
- `POST /api/mods/protection`
- `POST /api/notifications/send`
- `POST /api/dialogs/confirmation`
- `POST /api/events/trigger-client`
- `POST /api/spatial/teleport`
- `POST /api/spatial/rebase`
- `POST /api/vehicles/remove`

## Spatial Requests

The HTTP branch adds helper endpoints that wrap client events:

- `POST /api/spatial/teleport`
  - sends `BeamMPSpatialTeleport`
  - defaults `reply_event_name` to `BeamMPSpatialTeleportApplied`
- `POST /api/spatial/rebase`
  - sends `BeamMPSpatialRebase`
  - defaults `reply_event_name` to `BeamMPSpatialRebaseApplied`

Example teleport body:

```json
{
  "target_id": 0,
  "data": {
    "x": 100.0,
    "y": 200.0,
    "z": 10.0
  }
}
```

## Notes

- `GET /api/players/vehicle-position` returns both the player snapshot and the selected vehicle snapshot
- `GET /api/players/vehicle-position/raw` returns the raw packet payload for controller-side parsing
- Recent log and event endpoints are designed for incremental polling
- This branch does not yet include the later spatial offset endpoints added with the rebase work
