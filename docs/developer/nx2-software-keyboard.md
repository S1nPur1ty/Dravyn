# Switch 2 foreground software keyboard

The foreground applet ID is `0x01020011`. It is explicitly dispatched to the existing
software-keyboard frontend; unrelated extended IDs are not masked into legacy IDs.
The modeless applet (`0x01010011`) is a separate protocol and is not added here.
See https://switchbrew.org/wiki/AM_services#AppletId for the applet identifiers.

Revision `0x140010` sends a `0x530`-byte configuration. This is not a legacy
`0x4C8`-byte configuration with a longer tail: several fields move. `SwkbdConfigNx2`
converts the wire layout to the existing common and extended keyboard parameters.
The UI and text-check/retry/cancel protocol are reused unchanged.

## Layout evidence

Local SDK build ID `cc7da00d672399efe7958790d17fae396` was inspected read-only.
`nn::swkbd::ShowKeyboard` at module offset `0x54e700` constructs revision `0x140010`
and applet ID `0x01020011`, then pushes `0x530` bytes. Its field copies establish:

| Field | Configuration offset |
| --- | --- |
| Return key enabled | `0x1B` |
| UTF-8 output / background blur | `0x3AA` / `0x3AB` |
| Maximum / minimum length | `0x3AC` / `0x3B0` |
| Initial string offset / UTF-16 length | `0x3BC` / `0x3C0` |
| User dictionary offset / count | `0x3C4` / `0x3C8` |
| Customized dictionary entries | `0x3CC` |
| Customized dictionary count | `0x48C` |
| Text checking / cancel disabled | `0x48D` / `0x48E` |
| Trigger | `0x48F` |
| Text grouping | `0x490` |

The title/subtitle/guide and length fields retain their earlier offsets. The
remaining tail is not interpreted. The SDK validation callback at `0x54e1f0`
reads the existing 64-bit byte count followed by text and sends the existing
`0x7D8`-byte text-check response. No game data or validation callbacks are patched.

The saved freeze log showed the generic stub returning empty text and receiving
22,060 rejection responses in about five seconds. The old diagnostic truncated
IDs to eight bits, obscuring the dispatch failure; it now prints all 32 bits.

Regression tests construct the wire bytes by SDK offsets, check translated
strings, lengths, flags and initial text/dictionary offsets, and distinguish the
legacy layout. They do not substitute for a user test of the native keyboard UI.

## Audio cleanup

The Switch 2 audio user-client creates temporary IPC audio-output managers.
Their former embedded manager could be destroyed while streams and the audio
callback still referenced it. The audio core now owns one output manager for its
whole lifetime, including all IPC sessions. The audio manager thread is joined
before that object is destroyed. Streams finalize before their buffer event and
process reference are closed; device finalization is idempotent.

This fixes a source-level lifetime defect consistent with the saved audio-thread
termination, but the exact failing exception was not retained in the crash report.
Gameplay and interactive shutdown verification remain with the user.
