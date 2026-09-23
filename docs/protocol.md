# HLK-ZW111 protocol notes

The ZW111 speaks the `0xEF01` packet protocol shared by the ZhianTec / Synochip
family -- the same one the R305, R307, ZW101 and a long tail of clones use. If
you have written against an R307 before, this will look familiar.

The implementation is in
[`firmware/lib/zw111_protocol/`](../firmware/lib/zw111_protocol/), deliberately
free of any Arduino dependency so it can be tested on a host.

It has never been run against a physical module. Everything below comes from the
protocol family's documentation, and the tests check this code against that
description -- not against a ZW111 on a desk. See
[validation.md](validation.md).

## Frame layout

```
 0  1   2  3  4  5   6    7  8      9 ...            n-2 n-1
+--+--+ +--+--+--+--+ +--+ +--+--+ +----------------+ +--+--+
|EF|01| | address   | |pid| |length| |    payload   | |chksum|
+--+--+ +--+--+--+--+ +--+ +--+--+ +----------------+ +--+--+
```

| Field | Size | Notes |
| --- | --- | --- |
| header | 2 | Always `EF 01` |
| address | 4 | Big endian. `FFFFFFFF` until you change it |
| pid | 1 | `01` command, `02` data, `07` ack, `08` end of data |
| length | 2 | Big endian, **payload + 2**. The checksum bytes count; the header, address and pid do not |
| payload | n | First byte is the instruction code (commands) or the confirmation code (acks) |
| checksum | 2 | Big endian sum of pid, both length bytes and the payload |

Two details cause most of the wasted time:

- **The length field includes the checksum bytes.** A `GetImage` command has a
  one-byte payload and a length field of `0x0003`.
- **The checksum covers the length field itself**, not just the payload.

A bare `GetImage`, in full:

```
EF 01 FF FF FF FF 01 00 03 01 00 05
                  ^^ ^^^^^ ^^ ^^^^^
                  |    |    |    checksum: 01 + 00 + 03 + 01
                  |    |    instruction
                  |    length = payload(1) + 2
                  pid = command
```

`test_encode_get_image_matches_datasheet` in
[`firmware/test/test_protocol/`](../firmware/test/test_protocol/) asserts exactly
those bytes.

## Commands this firmware uses

| Code | Name | Arguments | Ack payload after the confirmation code |
| --- | --- | --- | --- |
| `01` | GetImage | -- | -- |
| `02` | GenChar | buffer (1 byte) | -- |
| `04` | Search | buffer, start page (2), count (2) | page id (2), score (2) |
| `05` | RegModel | -- | -- |
| `06` | StoreChar | buffer, page id (2) | -- |
| `0C` | DeleteChar | page id (2), count (2) | -- |
| `0D` | Empty | -- | -- |
| `13` | VerifyPassword | password (4) | -- |
| `1D` | TemplateNum | -- | count (2) |
| `33` | Sleep | -- | -- |
| `35` | AuraLedConfig | control, speed, colour, cycles | -- |

The ZW series also offers `AutoEnroll` (`31`) and `AutoIdentify` (`32`), which
collapse a whole sequence into one command and stream progress acks back. They
are convenient but not universal across the family, so this firmware uses the
classic sequence instead:

```
identify:  GetImage -> GenChar(1) -> Search(1, 0, capacity)
enrol:     (GetImage -> GenChar(n)) x4 -> RegModel -> StoreChar(1, page)
```

## Confirmation codes

Every ack starts with one. The ones you will actually meet:

| Code | Meaning | What it usually is |
| --- | --- | --- |
| `00` | OK | |
| `01` | Packet receive error | Checksum or framing -- suspect the wiring |
| `02` | No finger | Normal. The platen is empty |
| `03` | Image capture failed | Finger moved, or too dry |
| `06` | Image too noisy | Dirty platen |
| `07` | Too few feature points | Partial press |
| `09` | Not found in library | A real non-match |
| `0A` | Could not merge captures | The enrolment captures were of different fingers |
| `13` | Wrong module password | `kSensorPassword` does not match the module |
| `18` | Flash error | Store or erase failed |
| `27` | Duplicate | Already enrolled (AutoEnroll only) |

`02` is not an error and the firmware treats it as such: it is how you poll for a
finger without keeping the imaging sensor awake.

## Parser behaviour

The parser is a byte-at-a-time state machine rather than a blocking read, for
three reasons that all came from the datasheet's silence on them:

- **It resynchronises.** If the ESP32 resets while the module is mid-reply, the
  UART buffer opens with a partial frame. The parser walks forward until it
  finds a real `EF 01`.
- **A run of `EF` bytes does not false-start.** Only `EF 01` is a header, but
  `EF EF 01` still has to parse -- so on a mismatch the parser holds position
  when the byte it saw was itself `EF`.
- **The declared length is bounded before it is trusted.** It comes off the wire,
  and a frame claiming 65533 bytes of payload must be refused, not buffered.

All three have tests, because all three fail silently.
