# Phone link protocol (v1)

The game talks to `tools/devlink.py` on the PC (spec 16.10). Plain HTTP/1.1 on the local
network. There is no authentication: it carries logs and tuning values only, never secrets,
and only on the owner's own Wi-Fi.

## Finding the PC

| Build | How |
| --- | --- |
| Windows (any) | `http://127.0.0.1:47147` directly |
| iOS | Bonjour browse for `_ofs-devlink._tcp` (`NWBrowser`, `gdext/ios/bonjour_browser.mm`), resolve to an IPv4 address and port |

The service type stays within RFC 6335's 15-character limit, which zeroconf and
mDNSResponder enforce.

`devlink.py` listens on port **47147** on all IPv4 interfaces and advertises
`_ofs-devlink._tcp` with TXT record `v=1`. Info.plist carries
`NSBonjourServices = ["_ofs-devlink._tcp"]` and `NSLocalNetworkUsageDescription`.

## Endpoints

### `GET /health`
`200 {"ok": true, "v": 1}`. The game calls it once after discovery; success logs
`devlink.connected`.

### `POST /logs`
- Body: JSONL, one log line per line (the canonical `to_jsonl` format, spec 16.10), UTF-8,
  `Content-Type: application/x-ndjson`. At most 512 lines or 1 MiB per request.
- Each line is appended to `logs/<YYYY-MM-DD>/<session>.jsonl`, the date taken from the
  line's `ts` (UTC) and the session from its `session` field. Lines from several sessions
  in one request are fine.
- `204` when every line was stored.
- `411` without Content-Length, `413` over 1 MiB or 512 lines (nothing stored), `404` for an
  unknown path. The game drops a batch the server will never accept (400's bad lines, 413,
  404) instead of retrying it, logs `devlink.upload_rejected`, and splits a batch rather than
  sending one over the limits; a single line over 1 MiB is dropped and counted.
- The server keeps every rejected line with its reason in `logs/<date>/rejected.jsonl`.
- `400 {"stored": n, "bad_lines": [i, ...]}` when some lines were not valid log lines; the
  valid ones are stored anyway. The game drops the bad lines (they would fail again) and
  logs `devlink.upload_rejected` with status 400.
- On any other failure (timeout, connection refused, 5xx, 408, 429) the acknowledged offset
  stays where it was and the game retries with backoff: 1 s, 2 s, 4 s, ... capped at 30 s.

### `GET /params`
- `200 {"hash": "<16 hex>", "files": {"table_params": {...}}}`. `files` holds the parsed
  contents of every parameter file the server serves (M0: `core/data/table_params.json`;
  later milestones add AI level files under their own keys). The server re-reads them from
  disk on every request.
- `hash` is `content_hash` (`core/include/ofs/params.hpp`) of the `files` object.
- The game sends `If-None-Match: "<hash it last applied>"`; the server answers `304` when
  unchanged.
- The game checks every 3 s while connected. New parameters are applied at the next shot,
  never mid-shot (M0 has no shots: it applies at once), and logged as `params.applied`.

## What gets uploaded

The game uploads from its session files in `user://logs`, not from memory: every session
file, the current one and any earlier ones a crash or a session away from the PC left
behind, is sent from the byte offset last acknowledged by a 2xx (kept in
`user://logs/upload_state.json` as `{"v": 1, "offsets": {"<file name>": <bytes>}}`, written to
a temp file and renamed) up to its last complete line. So nothing is lost to a
crash, a quit, or playing without the PC, and nothing is sent twice. On connect the game
logs `devlink.backlog` when earlier sessions are waiting. `suppressed_since_last` in
`devlink.upload_rejected` counts rejections that weren't logged because of the rate limit. Fully uploaded files are pruned
by the normal size cap only.

## Served parameters are archived

Every distinct parameter set `devlink.py` serves is saved as `logs/params/<hash>.json`, so a
hash in a `params.applied` line can always be traced back to its values.

## Canonical JSON and the hash

`canonical` = keys sorted, no whitespace, UTF-8 without escaping non-ASCII, strings escaped
as JSON requires (control characters as lowercase `\u00xx`). Numbers: a JSON integer
token is written in decimal; any other number is written with C's `%.17g`, with `.0`
appended if the result has no `.`, `e` or `n`. NaN and infinities, integers outside the int64/uint64 range (nlohmann would turn them into
doubles) and strings with lone surrogates are refused: the CLI prints `error`, devlink.py
answers 500. Both sides
implement this rule explicitly (C++ `snprintf`, Python `'%.17g' % x`); neither relies on its
JSON library's number output, which differs between nlohmann and Python for some doubles.
`content_hash` = FNV-1a 64 (offset basis `0xcbf29ce484222325`, prime `0x100000001b3`) over
those bytes, as 16 lowercase hex digits.

`tools/test_devlink.py` also runs `ofs_params_hash` (built by the gate) over thousands of
random doubles and requires identical hashes. Test vectors (both
`core/tests/test_params.cpp` and `tools/test_devlink.py` assert these; recompute any that
contain non-integer numbers under the rule above):

| Input | Canonical | Hash |
| --- | --- | --- |
| empty bytes (fnv1a64_hex) | | `cbf29ce484222325` |
| `"a"` bytes (fnv1a64_hex) | | `af63dc4c8601ec8c` |
| `{"b":1,"a":[true,null,"x"]}` | `{"a":[true,null,"x"],"b":1}` | `8efc364b3052045e` |
| `{"values":{},"schema_version":1}` | `{"schema_version":1,"values":{}}` | `b99af9e0a04c6e42` |
