# harbour-lauscher

A Sailfish OS app that controls Sony headphones (battery, device info, listening
modes, playback) by speaking Sony's MDR protocol. The protocol library is
`libmdr` from
[SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient), vendored
under `libmdr/upstream/` rather than reimplemented.

Target device for the proof of concept: **Sony LinkBuds Clip** (LinkBuds series,
MDR **V2** protocol).

### Relationship to SonyHeadphonesClient

This app started inside a SonyHeadphonesClient checkout and was split out on
2026-08-30 so that the two can be developed and contributed independently. What
that means in practice:

- The protocol sources here are a **copy**, pinned to a state known to work with
  the LinkBuds Clip. [libmdr/UPSTREAM.md](libmdr/UPSTREAM.md) records which
  commit, what was added on top, and how to refresh it.
- The pinned state includes protocol fixes that are **not upstream yet** (the
  transmit sequence number, the pre-handshake frame guard, gating V2 init
  requests on the advertised function list, the four listening modes). Plain
  upstream will not drive this device correctly.
- Bugs found here that live in the protocol belong in the SonyHeadphonesClient
  checkout (`~/git/SonyHeadphonesClient`), not in `libmdr/upstream/` - fix them
  there, then refresh the copy. Editing the copy directly makes the next refresh
  a merge.

---

## How the Sony MDR protocol works

Everything below was read out of `libmdr/upstream`. Read this before touching
protocol code — the wire format is not documented anywhere else in the tree except
`libmdr/upstream/AGENTS.md` (which covers payload struct conventions) and the
source itself.

### Transport

A single **Bluetooth RFCOMM** channel. Which service UUID answers tells you which
protocol family the device speaks (`libmdr/include/mdr-c/Base.h`):

| UUID | Devices | Family |
|---|---|---|
| `956C7B26-D49A-4BA8-B03F-B17D393CB6E2` | WF/WH-1000XM5 and newer, LinkBuds | V2 |
| `96CC203E-5068-46AD-B32D-E316F5E069BA` | XM4 and older | V1 |
| `5B833E20-6BC7-4802-8E9A-723CECA4BD8F` | BLE GATT (TANDEM_OVER_BLE_HPC) | V2 |

### Framing

`libmdr/upstream/src/Command.cpp`, `libmdr/upstream/include/mdr/Command.hpp`:

```
<0x3E> ESCAPE( <DATA_TYPE:u8> <SEQ:u8> <PAYLOAD_LEN:u32be> <PAYLOAD...> <CHECKSUM:u8> ) <0x3C>
```

- Checksum is a plain 8-bit sum over type + seq + len + payload.
- Escaping happens *after* the checksum is computed, and covers the three bytes
  that would otherwise collide with the markers:
  `0x3C → 0x3D 0x2C`, `0x3D → 0x3D 0x2D`, `0x3E → 0x3D 0x2E`.
- `PAYLOAD_LEN` counts **unescaped** bytes.
- Max packet 2048 bytes.
- `SEQ` alternates 0/1. Every `DATA_*` frame is answered with an `ACK` frame
  carrying the same sequence number; the sender must not send the next command
  until the ACK arrives.

`DATA_TYPE` values that matter: `ACK=1`, `DATA_MDR=12`, `DATA_MDR_NO2=14`.
V2 devices split their command surface across two "tables": table 1 rides on
`DATA_MDR`, table 2 on `DATA_MDR_NO2`. Which tables exist is announced in
`CONNECT_RET_PROTOCOL_INFO`.

### Payloads

First payload byte is always a `Command` enum. Structs are `#pragma pack(1)`,
big-endian scalars via `Int16BE`/`Int24BE`/`Int32BE`, and either memcpy-
serialized (`MDR_DEFINE_TRIVIAL_SERIALIZATION`) or generated
(`MDR_DEFINE_EXTERN_SERIALIZATION`, implementations in
`libmdr/upstream/src/Generated/`). The generated sources are **checked in**
upstream and came along with the copy, so the LLVM-based codegen in the
SonyHeadphonesClient checkout is not needed to build.

### Session lifecycle

Driven by coroutines in `libmdr/upstream/src/Headphones*.cpp`:

1. `CONNECT_GET_PROTOCOL_INFO` → decides V1 vs V2 and which tables exist.
2. `CONNECT_GET_CAPABILITY_INFO` / `CONNECT_GET_DEVICE_INFO` → model name,
   firmware, serial, colour.
3. `CONNECT_GET_SUPPORT_FUNCTION` → the feature bitmap. **Everything the UI
   shows must be gated on this**; LinkBuds Clip advertises a very different set
   than an XM5.
4. Per-feature `*_GET_CAPABILITY` / `*_GET_PARAM` / `*_GET_STATUS`.
5. Afterwards the device pushes `*_NTFY_PARAM` frames unprompted (battery,
   noise mode changes made on the device itself).

---

## Why the app is built the way it is

### Reuse boundary: the C ABI, not the C++

`libmdr` exposes a stable C ABI in `libmdr/upstream/include/mdr-c/`
(`mdrHeadphonesCreate`, `mdrHeadphonesPoll`, `mdrHeadphonesGetBatteries`, …).
The app uses **only** that. This is deliberate and load-bearing:

> `libmdr` needs **C++20** (coroutines, concepts). Sailfish's Qt is **5.6.3**,
> whose headers are not C++20-clean (reversed `operator==` candidates in
> particular). Two qmake subprojects keep them apart, and since the seam is pure
> C, no C++ ABI ever crosses it.

The Sailfish 5.1 target ships **GCC 13.4**, so C++20 itself is not a problem.

### The libmdr sources are vendored, not referenced

`libmdr/libmdr.pro` compiles `upstream/src/*.cpp` from the copy in this
repository. That is what makes the app buildable on its own and what makes a
source-tarball build (`sfdk package`, OBS) possible at all — the earlier
in-place reference to a sibling checkout was outside the packaged tree.

The copy is verbatim, so refreshing it is a file copy plus a `diff -r` check;
[libmdr/UPSTREAM.md](libmdr/UPSTREAM.md) has the recipe. `SOURCES` lists
translation units explicitly, so a new `.cpp` upstream has to be added by hand.

### fmt is vendored

`libmdr` uses fmt for `mdr::Format` (error strings and hex dumps — 4 call sites).
Upstream fetches it with CMake `FetchContent`, which is no good here: Sailfish
has no `fmt` package and RPM builds should not reach the network. So
`libmdr/3rdparty/fmt/` holds fmt **12.1.0** (`base.h`, `format.h`,
`format-inl.h`, `src/format.cc` — the subset `mdr::Format` needs).

Upstream's `contrib/fmt.patch` is **not** applied and is not needed: it only
matters when building with `FMT_USE_LOCALE=0`, which follows from upstream's
`MDR_NO_EXCEPTIONS=ON`. This build keeps exceptions **on** (libmdr never throws
anyway) so stock fmt drops in unmodified. `-fno-rtti` is kept, matching upstream.

### Bluetooth: BlueZ Profile1 over D-Bus

`app/src/BluezTransport.{h,cpp}` implements the `MDRConnection` vtable.

The upstream Linux backend (`libmdr-bt/src/Linux/` over in SonyHeadphonesClient,
not vendored here) is **not** reusable: it
needs libbluetooth's SDP API, and the Sailfish target ships neither
`bluez5-libs-devel` by default nor QtBluetooth at all (there is no `Qt5Bluetooth.pc`
— only `KF5BluezQt`, which is not harbour-allowed either).

Instead bluetoothd does the SDP lookup for us:

1. Export an `org.bluez.Profile1` object (`Profile1Adaptor`).
2. `ProfileManager1.RegisterProfile(path, MDR_UUID, {Role: "client", …})`.
3. `Device1.ConnectProfile(MDR_UUID)` — async, takes seconds.
4. BlueZ calls back `Profile1.NewConnection(device, fd, props)` with the
   connected RFCOMM socket as a **passed unix file descriptor**. We `dup()` it
   (the `QDBusUnixFileDescriptor` only owns it for the duration of the call),
   set `O_NONBLOCK`, and from there it is plain `recv`/`send`/`poll`.

Verified policy facts (Sailfish 5.1.0.11):
- `/usr/share/dbus-1/system.d/bluetooth.conf` has
  `<policy context="default"><allow send_destination="org.bluez"/></policy>`,
  so an unprivileged app may call `RegisterProfile`/`ConnectProfile`.
- `send_interface="org.bluez.Profile1"` is allowed for `root` and `radio`, which
  is the direction that matters — bluetoothd is the sender of `NewConnection`.
- Sailjail's `Bluetooth.permission` grants `dbus-system.talk org.bluez`, which
  covers both directions. The desktop file requests it.

### Poll loop

`libmdr` does no work of its own between `mdrHeadphonesPoll()` calls — every
request is a coroutine that only advances there. `MdrController` ticks it from a
30 ms `QTimer`, translates `MDR_EVENT_*` into Qt property notifications, and
commits staged changes:

```
setNoiseMode() → mdrHeadphonesSetNoiseControl()   // stages, does not send
tick()         → mdrHeadphonesIsDirty() → mdrHeadphonesRequestCommit()
```

---

## Layout

```
harbour-lauscher.pro          TEMPLATE=subdirs, ordered: libmdr then app
libmdr/libmdr.pro             static lib, C++20, compiles libmdr/upstream
libmdr/upstream/              vendored SonyHeadphonesClient/libmdr (UPSTREAM.md)
libmdr/3rdparty/fmt/          vendored fmt 12.1.0
app/app.pro                   CONFIG += sailfishapp, QT += dbus
app/src/BluezTransport.*      MDRConnection vtable over BlueZ Profile1
app/src/MdrController.*       QML facade; owns the poll loop
app/qml/pages/DeviceListPage  paired-device picker
app/qml/pages/DevicePage      battery, playback, ambient sound control, listening mode
rpm/harbour-lauscher.spec
```

## Building

```sh
sfdk -c target=SailfishOS-5.1.0.11-aarch64 -c no-fix-version build
```

- `-c no-fix-version` matters: without it sfdk derives the version from git
  tags. That used to pull SonyHeadphonesClient's tags in (`1.4.4+v1.compat...`
  instead of the spec's `0.1.0`); this repository has no tags yet, so derive
  from them only once it does.
- `sfdk build` builds **in-tree** and skips `%prep`, which is why generated
  `Makefile`s and `.o` files land next to the sources. **Delete `Makefile`,
  `libmdr/Makefile`, `app/Makefile` when switching targets** or the stale ones
  point at the wrong arch's qmake (`lib64` vs `lib`) and the build dies with
  `Error 127`.
- A source-tarball build (`sfdk package`, OBS) is no longer structurally blocked
  now that libmdr is vendored inside the project — but it has never been tried.

Harbour validation passes clean:

```sh
sfdk -c target=SailfishOS-5.1.0.11-i486 check
```

`Requires: bluez5` had to be dropped to get there — harbour does not allow it as
a dependency, and BlueZ is in the base system regardless. Do not add it back.

## QML gotchas already paid for

Two bugs cost real debugging time here; do not reintroduce them.

- **`import "pages"` in `app/qml/harbour-lauscher.qml` is mandatory.** Without it
  `DeviceListPage {}` does not resolve, and the failure mode is *silent*: the app
  starts, stays running, and paints a **uniformly white window**. No QML error
  reaches stderr or the journal under Sailjail. If you ever see a blank white
  app, suspect an unresolved QML type first.
- **`Text.WordWrap`, not `Text.Wordwrap`.** The misspelling is not an error in
  QML — the value is simply undefined and text silently stops wrapping.

Also: Silica's `ComboBox.currentIndex` and `Slider.value` are **written to** by
the controls themselves. Binding them to a `mdr.*` property works exactly once —
the first user interaction destroys the binding and the control then ignores
changes made on the headset itself. `DevicePage.qml` therefore assigns them from
`Component.onCompleted` plus a `Connections` block instead of binding.

## Status / next steps

Proof of concept. Working: paired-device listing, connect, identity, battery,
playback (track names, play/pause/next/previous, volume), ambient sound control
(off / NC / ambient + level + focus-on-voice), listening mode (all four, plus the
background-music distance), cover page.

Everything under Playback rides on one event. Volume, play/pause status and the
track names all report `MDR_EVENT_PLAYBACK_CHANGED`, so `refreshPlayback()` reads
all three, and all of them arrive unprompted as well as on request.

- **Volume** is the headset's own 0..30 scale, not a percentage —
  `mdrHeadphonesSetPlayback` rejects anything above 30. The struct it takes carries
  the play/pause status too, and libmdr refuses one that asks for a state change, so
  the setter reads the current struct and puts the status back unaltered.
- **Track names come from the phone, not the headset.** They are whatever the source
  device pushed over AVRCP, so all three being empty is normal, not a fault — the
  block hides itself in that case. Watch for this when testing: a silent Now-playing
  section usually means the phone is not pushing metadata, not that the app is broken.
- **The status is the source device's**, which is why the play/pause button reflects
  what the phone reports rather than what was last tapped, and why
  `sendPlaybackAction()` deliberately does not update anything locally. Whether music
  actually starts is the media player's decision, not the headset's.
- Transport commands stage `mPlayControl` like any other setting and go out on the
  next `RequestCommit`; libmdr resets the property to `KEY_OFF` afterwards so tapping
  the same button twice sends it twice.

The listening-mode picker offers only the modes the device advertises. Each one
is a separate `MDR_FEATURE_LISTENING_*` bit — `MDR_FEATURE_LISTENING_MODE` only
says the device groups them into one exclusive setting — so the menu keeps a
fixed item per mode and hides the ones this device lacks. That is deliberate:
Silica numbers menu items whether or not they are visible
(`ContextMenu._foreachMenuItem`, `ComboBoxController._updateCurrent`), so hiding
one does not shift the others, whereas a `Repeater`-built menu would, and its
items would arrive only after `ComboBoxController` had given up looking for
them. `MdrController.listeningModes` carries the advertised set for the `visible`
bindings.

### Confirmed on hardware, 2026-08-30

The "reasoned through but never observed" caveat this section used to carry is
retired. Against the LinkBuds Clip on the phone, all of this behaves:

- The BlueZ Profile1 handshake and the MDR session — bluetoothd hands over the
  RFCOMM fd and libmdr initializes over it, exactly as designed here.
- Track names, the transport controls and the volume slider.
- Switching between all four listening modes.

Earlier, and still true:
- Builds clean for `aarch64` and `i486` on Sailfish 5.1.0.11.
- Passes `sfdk check` (harbour suite) in full.
- Runs on the emulator under Sailjail.

**Still never run on hardware: the background-music distance picker.** It is the
one listening-mode control not exercised on the phone, and the only path that can
reach `setListeningMode()`'s `MDR_ROOM_UNKNOWN` fallback. Try it first next time.

Known gaps:
- No reconnect-on-wake; leaving `DevicePage` drops the RFCOMM channel on
  purpose (the headset allows one control session at a time).
- LinkBuds Clip is open-ear and, as the desktop client confirmed on hardware,
  reports no NC/ASM function of any kind — the ambient sound control section
  simply will not appear. It offers background music, voice boost and sound
  leakage reduction, but no cinema.
- The device switches the equalizer and DSEE off while any listening mode other
  than Standard is active (`MDREqualizer.available` / `.dsee_available`). Neither
  has a UI here yet; whichever gets one has to gate on those, not on
  `MDR_FEATURE_EQUALIZER` / `MDR_FEATURE_DSEE`.
- V1 (XM4 and older) is compiled in and the UUID fallback exists, but untested.
- Equalizer, touch controls, multipoint, speak-to-chat, DSEE are all reachable
  through the C ABI already; only the UI is missing.
- `sfdk check` has not been re-run since the listening-mode and playback work;
  neither added an import or a dependency, so it should still pass.
- Nothing has been rebuilt since the split out of the SonyHeadphonesClient tree.
  The protocol sources are byte-identical to what was on the phone, but the
  include paths in `libmdr/libmdr.pro` and `app/app.pro` changed, so the first
  `sfdk build` from this repository is the one that proves the move.
