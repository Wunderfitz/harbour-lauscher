# Where `upstream/` comes from

`upstream/` is a copy of the `libmdr/` directory of
[SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient), taken at a
state this app is known to work against:

| | |
|---|---|
| Repository | `https://github.com/mos9527/SonyHeadphonesClient` |
| Base | `965c458d` (branch `v1-compat`) |
| Plus | the WF-LC900 protocol work, branch `mdr-v2-session-correctness-and-listening-modes` on `https://github.com/Wunderfitz/SonyHeadphonesClient`, tip `e8be775` |
| Taken on | 2026-09-10 |

Those extra commits are **not upstream yet**, and four of them are load-bearing for the
LinkBuds Clip:

- `46af5e5` the transmit sequence number, tracked independently of inbound frames -
  which is also what stops a notification arriving mid-handshake from desynchronizing
  the next request.
- `7fff7e8` gating the V2 init requests on the advertised function list.
- `032f581` the four listening modes, each offered only where the device has it.
- `a99ec62` committing the staged listening flags before the switch is sent rather
  than after, so no read lands on the half-applied state that reads as Standard.

`fa348bf` rides along: it reports whether the device will currently act on equalizer
and DSEE changes, which `EqualizerPage` gates on. A vanilla `v1-compat` checkout
will not drive this device correctly.

Two more are the only ones this repository asked for rather than inherited, and both are
load-bearing for the equalizer page:

- `bd26c4d` reads the equalizer preset capability. `EQEBB_GET_CAPABILITY` is what says
  *which* presets a device has, out of the thirty `MDREqualizerPreset` can express. V1 had
  been requesting it and discarding the answer; V2 never asked. Both now do, and the list
  reaches this app as `mdrHeadphonesGetEqualizerPresets`, with
  `MDR_TEXT_EQUALIZER_PRESET_NAME` for the names - see the Equalizer section of
  [../CLAUDE.md](../CLAUDE.md). It is not an ABI break: nothing crossing the boundary changed
  shape, so `MDR_ABI_VERSION` stays 2.
- `988f3d1` stops a preset change writing band steps. Choosing a preset landed the device on
  CUSTOM with a flat curve, because `pending()` caught the device's own report of the new
  curve - which arrives while the same commit pass is still running - and sent the band
  config from before the change. The band write now waits for `dirty()`, the caller's intent.

`7487eac` sits between them with the capture both were found in.

`9ad89b1` is a third, and it came out of the DSEE switch: applying a listening mode now asks
the device for the equalizer and upscaling statuses instead of waiting to be told they went
away. Only an unsolicited notification carried that before, and nothing re-read it -
`RequestSyncV2` included - so one missed frame left a control on screen that the device was
ignoring, with nothing to correct it. That is what the Equalizer button did on the phone
while the DSEE switch beside it greyed out correctly.

`2497040` is the fourth, and it is what makes `SettingsPage` work at all. A device does
not necessarily apply a setting when it receives one: if applying costs it its Bluetooth
links - multipoint and the connection mode both do - it acknowledges the request, holds
it, and asks first, as `ALERT_NTFY_PARAM FIXED_MESSAGE <message> POSITIVE_NEGATIVE`.
Unanswered, the held request is dropped without another word, which is exactly what
multipoint did here and in the desktop client: written, acknowledged, and read back
unchanged. libmdr already reported the question as `MDR_EVENT_ALERT` and had no way to
answer it, so `mdrHeadphonesRespondToAlert` adds one. Nothing crossing the boundary
changed shape - `MDR_ABI_VERSION` stays 2.

  Confirmed against a LinkBuds Clip on 2026-09-10: multipoint written from `SettingsPage`
  now takes, the headset drops its links the way the question warns it will, and the new
  value is what the next session reads. The connection quality needed `e8be775` as well.

`e8be775` is the fifth, and it is why the connection quality went on doing nothing after
the confirmation was answered. `AUDIO_SET_PARAM` carries an inquired type and the write
never set one, so it went out on `AudioSetParamConnection`'s default -
`CONNECTION_MODE_CLASSIC_AUDIO_LE_AUDIO` (0x05) rather than `CONNECTION_MODE` (0x00),
a variant the device had not advertised and one whose payload is a byte longer than what
was sent. A device acknowledges the frame and drops a command it does not implement, so
nothing anywhere said so. Everything else in the exchange already used 0x00: the init
request asks on it and the device answers `AUDIO_RET_PARAM` on it. Replaying the
WF-LC900 capture, the committed write is `e8 00 01` where it was `e8 05 01`, and the
setting applies on the device.

The copy is verbatim - `diff -r` against a checkout's `libmdr/` shows no differences -
except for two files added here from that repository's root:

- `upstream/LICENSE` - the project's MIT licence, which covers this code.
- `upstream/AGENTS.md` - the payload struct conventions, referenced throughout
  [../CLAUDE.md](../CLAUDE.md).

## Refreshing it

```sh
cd <SonyHeadphonesClient checkout>
git ls-files libmdr | while read -r f; do
  install -Dm644 "$f" "<this repo>/libmdr/upstream/${f#libmdr/}"
done
diff -r <SonyHeadphonesClient checkout>/libmdr <this repo>/libmdr/upstream   # only the two extras
git status                                                                  # only real changes
```

`-m644` is not decoration: `install` defaults to 755, and without it every copied
file turns up in `git status` as a mode change with no diff.

Then update the table above, and check `SOURCES` in [libmdr.pro](libmdr.pro): the build
lists translation units explicitly, so a new `.cpp` upstream has to be added by hand.
Upstream's CMake globs `src/*.cpp`, so a file appearing there is easy to miss - and the
link error it causes names a symbol, not the file it is missing.

`src/` and `src/Generated/` carry a `ProtocolV<n>T<n>Serialization.cpp` each, the former
overriding individual payloads the codegen cannot express. Both are compiled, and qmake
names object files after the source's basename alone, so `CONFIG +=
object_parallel_to_source` is what keeps the second of each pair from replacing the
first in `libmdr.a`. Without it the build gets all the way to the link and then fails on
whatever the shadowed file defined.
Generated sources (`upstream/src/Generated/`) are checked in upstream, so the LLVM-based
codegen never has to run.

The build recipe is ours, not upstream's: upstream builds libmdr with CMake and fetches
fmt over the network, neither of which works inside the Sailfish SDK.
