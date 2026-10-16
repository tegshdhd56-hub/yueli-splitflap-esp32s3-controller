# Local HTTP API

## Scope

The controller serves HTTP on port 80. These routes were originally built for the bundled browser UI, but they can also be used by local integrations.

Except for `/admin/*`, the current routes are not uniformly authenticated. Keep the controller on a trusted LAN and do not expose it directly to the Internet.

Form endpoints use `application/x-www-form-urlencoded` unless noted. JSON endpoints return `application/json`.

## Display and status

### `GET /settings`

Returns the current display settings used by the UI. The additive `product` and
`website` fields identify the official YUELI project without changing any
existing setting names.

### `GET /status`

Returns official device identity plus the current station Wi-Fi state:

```json
{
  "device": {
    "product": "YUELI Split-Flap",
    "firmware": "R10.1-dev",
    "website": "https://www.yueli.hk/"
  },
  "wifi": {
    "connected": true,
    "ip": "192.168.1.50",
    "ssid": "Example"
  }
}
```

### `POST /`

Updates the display/UI state. Recognized form fields include:

- `deviceMode`: `text`, `countdown`, `clock`, `date` or `carousel`
- `inputText`: text sent to the flap matrix in text mode
- `alignment`: display alignment value used by the UI
- `flapSpeed`: configured speed value
- `countdownDateTimeUnix`: countdown target
- scheduled-message fields retained for backward compatibility

Example:

```bash
curl -i -X POST http://192.168.4.1/ \
  -H "Content-Type: application/x-www-form-urlencoded" \
  --data-urlencode "deviceMode=text" \
  --data-urlencode "inputText=HELLO WORLD"
```

The endpoint redirects to `/` after a successful update. It may return a lock response while startup homing, Developer calibration or a movement confirmation is active.

An accepted HTTP request means that the controller accepted or transmitted work. It does not prove that every mechanical unit completed movement.

## Device startup state

### `GET /device/state`

Returns startup/homing state, counts, remaining quiet-period time, lock state and whether manual confirmation is available.

### `POST /device/confirm-homed`

Ends the settling wait after the operator has observed that all units stopped. This records human observation; it is not a hardware acknowledgement.

## History

### `GET /history`

Returns the stored JSON history document.

### `POST /history`

Replaces the history JSON document. The body is bounded by the firmware's `HISTORY_BODY_LIMIT` and is written through a temporary file before rename.

```bash
curl -X POST http://192.168.4.1/history \
  -H "Content-Type: application/json" \
  --data '["HELLO","WELCOME"]'
```

## Carousel

### `GET /carousel`

Returns:

- `speed`: stored interval value
- `messages`: up to 64 UTF-8 strings
- `revision`: SHA-256 revision used for optimistic concurrency
- `currentIndex`
- `playing`

### `POST /carousel`

Accepts JSON with `speed`, `messages` and the latest `expectedRevision`. The request is limited to 32,768 bytes. Each message is limited to 1,024 UTF-8 bytes.

```json
{
  "speed": 5000,
  "messages": ["WELCOME", "OPEN 9 TO 6"],
  "expectedRevision": "revision-from-get"
}
```

Refresh and retry after `409 carousel_changed`.

## Wi-Fi provisioning

### `GET /wifi/scan`

Reads the current scan state/results.

### `POST /wifi/scan`

Requests a new scan. Scanning is asynchronous.

### `GET /wifi/status`

Returns connection attempt state, requested SSID, current SSID/IP, message and optional `clientRequestId`.

### `POST /wifi/connect`

Form fields:

- `ssid`: 1-32 bytes
- `password`: empty for open networks, 8-63 characters, or 64 hexadecimal digits
- `clientRequestId`: optional visible ASCII correlation value, maximum 64 characters

The route returns `202` when a connection attempt is queued and `409` when another scan or connection is busy.

Do not log or forward the submitted password.

## Scheduled messages

### `DELETE /scheduled-message/remove?id=NUMBER`

Removes a stored scheduled message by numeric identifier.

## Developer calibration API

Developer endpoints use a bearer token returned by login:

```http
Authorization: Bearer <64-hex-character-token>
```

The token is stored only in RAM and expires after approximately 15 minutes of inactivity. Status polling does not extend the session.

### `POST /admin/login`

Form field: `password`. Five failed attempts trigger a temporary throttle.

### `GET /admin/status`

Requires the bearer token. Returns session expiry, startup state, last queued/sent command, receive history and any outstanding unit query.

### `GET /admin/unit?dip=N&page=P`

Requires the bearer token. `dip` is 0-255; `page` is 0-63.

### `POST /admin/unit/read`

Requires the bearer token. Form fields: `dip`, `page`. Queues an offset read when no movement/query lock is active.

### `POST /admin/calibrate`

Requires the bearer token. Supported form actions:

| Action | Additional fields | Effect |
|---|---|---|
| `goto` | `dip`, `page` | Move one unit to a page |
| `fine` | `dip`, `steps` (`-200..200`, not 0) | Fine movement |
| `learn` | `dip`, `page` | Store the current page calibration |
| `offset` | `dip`, `offset` (`-360000..360000`) | Store global offset |
| `clear` | `dip` | Clear the unit calibration table |
| `confirm_stopped` | — | Operator confirms observed stop |

The PY32 protocol lacks reliable movement and calibration-write acknowledgements. A `202` response means queued, not mechanically completed or persisted.

### `POST /admin/logout`

Requires the bearer token. Ends the Developer session and cancels queued commands.

## Raw TCP service

The controller also listens on TCP port 8888 for row clients. Clients are associated with rows using the final IP octet (`101` for row 0, `102` for row 1, and so on). This is a legacy local transport, not a secure public API.

## RS485/PY32 protocol

The exact PY32 protocol material is not redistributed in this public repository;
obtain it through the authorized hardware/vendor channel. Integration work must
still respect these controller-side limitations:

- Send one command at a time.
- Do not assume motion-time command queues are lossless.
- `CMD-OFGET` reads global offset, not live angle or per-page calibration.
- Only the addressed activated unit should answer `CMD-OFGET`.
- Duplicate DIP addresses can cause bus collisions.
