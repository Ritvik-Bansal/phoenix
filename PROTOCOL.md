# Phoenix BLE Protocol — Custom Assistant Service

This document defines the custom GATT service and wire format used on the
assistant path between the iOS app and the glasses:

```
NOTIFICATIONS:  iPhone --ANCS over BLE--> glasses          (Apple-defined, used as-is)
CLOCK:          iPhone --CTS over BLE--> glasses           (Apple-defined, used as-is)
ASSISTANT:      earbuds -> iPhone app (STT -> LLM) --Phoenix service--> glasses
```

ANCS and CTS are Apple specifications and are consumed unmodified by the
firmware; they are **not** described here. Everything below applies only to the
Phoenix service.

Protocol version: **1** (`phoenix::kProtocolVersion`).

---

## 1. GATT model

One primary service with two characteristics. The 128-bit UUIDs share a base
(generated with `uuidgen`, RFC 4122 v4); the third and fourth hex digits of the
first group are a 16-bit slot number, following the same convention as Nordic's
NUS.

| Entity | UUID | Properties | Direction |
|---|---|---|---|
| Phoenix service | `CD310001-0101-4B2F-9456-982A27ED3560` | primary | — |
| `TX` characteristic | `CD310002-0101-4B2F-9456-982A27ED3560` | Write Without Response, Write | phone → glasses |
| `RX` characteristic | `CD310003-0101-4B2F-9456-982A27ED3560` | Notify | glasses → phone |

- The glasses advertise the service UUID; the app scans and connects by it.
- `TX` carries assistant text, navigation updates, and control messages.
- `RX` carries button events, battery status, and acknowledgements. The phone
  must enable notifications (write the CCCD) before any glasses→phone traffic
  flows.
- Both characteristics require an **encrypted (bonded) link**. ANCS forces
  bonding anyway; the Phoenix service rides the same security level.

### MTU, chunking, reassembly

iOS owns the connection parameters. An accessory can *request* a larger ATT
MTU (the firmware requests 247) but **iOS decides**, and the app learns the
usable payload per write from
`maximumWriteValueLength(for: .withoutResponse)`.

Therefore the wire format assumes **nothing** about MTU:

- The byte stream on each characteristic is a simple concatenation of frames
  (§2). A single BLE write/notification may contain a partial frame, exactly
  one frame, several frames, or the tail of one frame followed by the head of
  the next.
- Senders SHOULD slice outgoing bytes into chunks of at most (ATT_MTU − 3)
  bytes but MAY use any chunk size ≥ 1.
- Receivers MUST reassemble by feeding every received byte, in order, into the
  frame decoder (§4). Per-characteristic ordering is guaranteed by BLE;
  ordering *between* the two characteristics is not, and no message relies on
  it.

---

## 2. Frame format

All multi-byte integers are **little-endian**. One frame:

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `START` | Constant `0xA5` |
| 1 | 1 | `TYPE` | Message type (§5) |
| 2 | 1 | `SEQ` | Per-sender sequence number, increments by 1 per frame, wraps 255 → 0 |
| 3 | 1 | `FLAGS` | Bit field (§3) |
| 4 | 2 | `LEN` | Payload length in bytes, u16 LE, `0 … 512` |
| 6 | `LEN` | `PAYLOAD` | Message-type-specific (§5) |
| 6+`LEN` | 2 | `CRC` | CRC-16 (§2.1), u16 LE |

Maximum payload is **512 bytes**; maximum frame is therefore 520 bytes. A
frame whose `LEN` exceeds 512 is invalid by construction (§4).

### 2.1 CRC

CRC-16/CCITT-FALSE: polynomial `0x1021`, initial value `0xFFFF`, no input or
output reflection, no final XOR. Check value: CRC over the ASCII bytes of
`"123456789"` is `0x29B1`.

The CRC is computed over bytes 1 through 6+`LEN`−1 inclusive — i.e. `TYPE`,
`SEQ`, `FLAGS`, `LEN`, and `PAYLOAD`; the `START` byte is excluded. It is
transmitted little-endian (low byte first).

### 2.2 Sequence numbers

Each sender (phone, glasses) keeps its own independent `SEQ` counter across
all message types on its transmit characteristic. Receivers use `SEQ` to
populate `ACK` frames and MAY log gaps; a gap is not an error and triggers no
recovery — BLE already provides reliable, ordered delivery per characteristic,
so gaps only ever indicate sender restarts.

---

## 3. Flags

| Bit | Name | Meaning |
|---|---|---|
| 0 | `ACK_REQ` | Receiver must answer with an `ACK` frame referencing this frame's `SEQ`. |
| 1 | `STREAM_FINAL` | On `ASSISTANT_STREAM_CHUNK`: this chunk ends the reply. |
| 2–7 | reserved | Must be sent as 0, must be ignored on receive. |

---

## 4. Decoder requirements (hostile input)

The decoder parses bytes off a radio and MUST be safe against arbitrary
garbage. Normative rules, all covered by the fuzz suite in
`core/tests/test_codec.cpp`:

1. Bytes before a `START` byte are discarded.
2. After a `START`, the decoder waits for the 6-byte header. If `LEN` > 512,
   the frame is invalid: discard the `START` byte only, count a resync, and
   rescan from the next byte (a real frame may begin inside the garbage).
3. With a complete header, wait until `LEN` + 2 further bytes arrive, then
   verify the CRC. On mismatch: count a CRC error, discard the `START` byte
   only, rescan from the next byte. On match: emit the frame and consume it.
4. Unknown `TYPE` values decode structurally (header + CRC) and are then
   dropped at dispatch, answered with `ACK` status `2` if `ACK_REQ` was set.
   Unknown types are how future protocol versions stay compatible.
5. Internal buffering is bounded (implementation cap 4096 bytes — larger than
   any legal frame). Overflow discards the oldest buffered bytes and counts a
   resync. The decoder never reads out of bounds and never crashes, for any
   input, in any fragmentation.

---

## 5. Message types

| Value | Name | Direction | Payload |
|---|---|---|---|
| `0x01` | `ASSISTANT_TEXT` | phone → glasses | UTF-8 text |
| `0x02` | `ASSISTANT_STREAM_CHUNK` | phone → glasses | UTF-8 text fragment |
| `0x03` | `NAV_UPDATE` | phone → glasses | maneuver, distance, street |
| `0x04` | `CLEAR` | phone → glasses | target bitmask |
| `0x05` | `SET_BRIGHTNESS` | phone → glasses | u8 level |
| `0x06` | `PING` | phone → glasses | u8 protocol version |
| `0x40` | `ACK` | both | acked seq + status |
| `0x41` | `BUTTON_EVENT` | glasses → phone | button id + action |
| `0x42` | `BATTERY_STATUS` | glasses → phone | percent + millivolts |

### 0x01 `ASSISTANT_TEXT`

Complete assistant reply as UTF-8 (§6). Replaces any assistant content
currently shown and restarts assistant paging. An empty payload clears the
assistant screen (equivalent to `CLEAR` mask `0x01`).

### 0x02 `ASSISTANT_STREAM_CHUNK`

UTF-8 fragment appended to the in-progress assistant reply, so text starts
appearing on the glasses before the LLM finishes. Rules:

- The first chunk after (a) a `STREAM_FINAL`, (b) an `ASSISTANT_TEXT`, or
  (c) a `CLEAR` covering the assistant starts a new reply.
- `STREAM_FINAL` (flag bit 1) marks the last chunk. An empty-payload chunk
  with `STREAM_FINAL` set is a valid pure end-marker.
- Chunk boundaries MAY split a UTF-8 code point; the display sanitizer holds
  incomplete trailing sequences until the next chunk. Senders SHOULD split on
  code-point boundaries anyway.
- While streaming, the display follows the newest text (last page); when the
  final chunk arrives, paging restarts from page 1.

### 0x03 `NAV_UPDATE`

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | Maneuver (enum below) |
| 1 | 2 | Distance to maneuver in meters, u16 LE (`0xFFFF` = unknown/hide) |
| 3 | rest | Street / instruction, UTF-8 |

Maneuver enum: `0` STRAIGHT, `1` LEFT, `2` RIGHT, `3` SLIGHT_LEFT,
`4` SLIGHT_RIGHT, `5` SHARP_LEFT, `6` SHARP_RIGHT, `7` UTURN, `8` ARRIVE.
Unknown values render as STRAIGHT (forward arrow).

A `NAV_UPDATE` creates or updates the navigation screen in place (no screen
transition). Navigation persists until `CLEAR` mask `0x02` or an `ARRIVE`
update times out.

### 0x04 `CLEAR`

Payload: 1 byte bitmask of what to drop. `0x01` assistant reply, `0x02`
navigation, `0x04` locally dismiss the currently shown ANCS notification
(the phone cannot un-post iPhone notifications; this only clears the glasses
display), `0xFF` everything.

### 0x05 `SET_BRIGHTNESS`

Payload: 1 byte, `0`–`255`, mapped by the firmware to the SSD1306 contrast
register. `0` is minimum visible brightness, not display-off.

### 0x06 `PING`

Payload: 1 byte, the sender's protocol version. Always answered with an `ACK`
(as if `ACK_REQ` were set): status `0` if the version is supported, `1` if
not. Used as a liveness check and version handshake after connect.

### 0x40 `ACK`

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `SEQ` of the frame being acknowledged |
| 1 | 1 | Status: `0` OK · `1` unsupported protocol version · `2` unsupported message type |

Sent in response to `PING` and to any frame carrying `ACK_REQ`. `ACK` frames
themselves must never carry `ACK_REQ`.

### 0x41 `BUTTON_EVENT`

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | Button: `1` A (dismiss) · `2` B (next page / accept) · `3` C (aux) |
| 1 | 1 | Action: `0` short press · `1` long press |

Buttons act locally on the glasses first (dismiss, paging, call actions via
ANCS); the event is also reported to the phone for the debug console and
future app-side features.

### 0x42 `BATTERY_STATUS`

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | Charge percent `0`–`100` (LiPo discharge-curve estimate) |
| 1 | 2 | Battery voltage in millivolts, u16 LE |

Sent on connect, then every 60 s, and whenever the percentage changes by ≥ 5.

---

## 6. Strings: UTF-8 on the wire, fallback on the glass

All strings are UTF-8, no BOM, no NUL terminator (lengths come from framing).
The renderer's glyph set is printable ASCII (`0x20`–`0x7E`) plus a dedicated
fallback glyph (a small hollow box). The display sanitizer applies, in order:

1. **Invalid bytes** (bad continuation, overlong form, lone surrogate,
   > U+10FFFF): consume one byte, emit the fallback glyph, continue. Never
   crash, never read past the buffer.
2. **Transliteration** of common typography: curly single/double quotes →
   `'`/`"`; en/em/horizontal-bar dashes and bullet → `-`; ellipsis → `...`;
   NBSP → space; `×` → `x`, `÷` → `/`, `°` → `*`.
3. **Latin letters with diacritics** (Latin-1 Supplement) → base ASCII letter
   (`é`→`e`, `Ä`→`A`, `ñ`→`n`, `ß`→`ss`, `Æ`→`AE`, `ç`→`c`, …).
4. **Dropped entirely** (width 0): combining marks U+0300–U+036F, zero-width
   space/joiners U+200B–U+200D, variation selectors U+FE00–U+FE0F, emoji
   skin-tone modifiers U+1F3FB–U+1F3FF.
5. **Everything else** (emoji, CJK, symbols): one fallback glyph per code
   point. An emoji in a notification renders as a box; it must not and does
   not crash anything.

Control characters: `\n` is honored as a line break, `\t` becomes a space,
all other C0/C1 controls are dropped.

---

## 7. Flow examples

Assistant round trip (happy path):

```
phone   TX: PING v1                      glasses RX: ACK(seq, 0)
phone   TX: ASSISTANT_STREAM_CHUNK "The wea"
phone   TX: ASSISTANT_STREAM_CHUNK "ther is sunny, 22C."  [STREAM_FINAL]
glasses     ... renders pages, auto-advances ...
glasses RX: BUTTON_EVENT A short         (user dismissed; screen falls back to clock)
```

Navigation:

```
phone   TX: NAV_UPDATE LEFT 250m "Market St"
phone   TX: NAV_UPDATE LEFT 80m  "Market St"     (updates in place)
phone   TX: NAV_UPDATE ARRIVE 0m "Destination"
phone   TX: CLEAR 0x02
```

Wire example, fully worked — `SET_BRIGHTNESS(128)`, seq 7, no flags:

```
A5 05 07 00 01 00 80 7D 8C
│  │  │  │  │──┤  │  └──┴─ CRC-16 = 0x8C7D, sent LE
│  │  │  │  │  │  └─ payload: 128
│  │  │  │  └──┴─ LEN = 1
│  │  │  └─ FLAGS = 0
│  │  └─ SEQ = 7
│  └─ TYPE = SET_BRIGHTNESS
└─ START
```

(The CRC value above is produced by `phoenix::proto::crc16` and verified in
`test_codec.cpp` so the document and the code cannot drift.)
