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
  requests on the advertised function list, the four listening modes, and
  committing the staged listening flags before the switch is sent rather than
  after). Plain upstream will not drive this device correctly.
- Bugs found here that live in the protocol belong in the SonyHeadphonesClient
  checkout (`~/git/SonyHeadphonesClient`), not in `libmdr/upstream/` - fix them
  there, then refresh the copy. Editing the copy directly makes the next refresh
  a merge.
- That checkout's `tests/` holds **recorded device traffic**, including
  `WF-LC900-2.0.3-listening`, which is this device's family switching between all
  of its listening modes. Frames can be replayed through the C ABI offline with
  the mock transport in `tests/Replay.cpp` - roughly 100 lines to stand up - which
  answers "what does the app actually see" without a phone in hand. That is how
  the listening-mode flicker below was pinned down; guessing at it twice first
  cost more than writing the harness would have.

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
`bluez5-libs-devel` by default nor QtBluetooth at all (there is no
`Qt5Bluetooth.pc` — only `KF5BluezQt`). Harbour compliance was a second argument
against `KF5BluezQt` and no longer applies, but the Profile1 route needs nothing
beyond bluetoothd, which is already the dependency, so there is no reason to
revisit it.

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
app/qml/cover/CoverPage       the cover: status at a glance, mode and distance actions
app/images/                   cover artwork and cover-action icons (SVG)
rpm/harbour-lauscher.spec
```

Lauscher's own sources — everything under `app/src` and `app/qml` — carry a
GPLv3-or-later header, and the spec says `License: GPLv3+`. New files there get
the same block. `libmdr/upstream/` (MIT) and `libmdr/3rdparty/fmt/` keep the
licences they came with: never put a GPL header on vendored code, and never edit
their existing ones.

## Building

Out of tree, the way Qt Creator and the Sailfish SDK do it: hand `sfdk` a path
to the sources and it shadow-builds into the current directory.

```sh
mkdir -p ../build-harbour-lauscher-aarch64
cd ../build-harbour-lauscher-aarch64
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build ../harbour-lauscher
```

The RPMs land in `RPMS/` **inside the build directory**. Opening
`harbour-lauscher.pro` in Qt Creator does the same thing — its default build
directory is a sibling `build-harbour-lauscher-<target>-<config>/`, one per
target and configuration.

- **Nothing may land in the source tree.** The `.pro` files are written for
  this: `$$PWD` addresses sources, `$$OUT_PWD` the build tree, and the app finds
  `libmdr.a` under `$$OUT_PWD/../libmdr`. The one exception is
  `app/translations/harbour-lauscher.ts`, which `sailfishapp_i18n` regenerates
  with lupdate at install time; it is gitignored for that reason.
- One build directory per target means **switching targets needs no cleaning**.
  The old in-tree recipe (`sfdk build` with no path) did: those `Makefile`s
  carry the arch's qmake path (`lib64` vs `lib`), and a stale one kills the next
  build with `Error 127`.
- `%prep` is skipped either way, and `--prepare` is not available for shadow
  builds at all. No loss here — `%prep` only unpacks the source tarball.
- sfdk derives the package version from git tags unless told otherwise. This
  repository has no tags, so it falls back to the spec's `0.1`. Once tags
  exist they either follow the spec version or builds need
  `-c no-fix-version`.
- rpmbuild still builds **in-tree** when it unpacks a source tarball
  (`sfdk package`, OBS). That path is no longer structurally blocked now that
  libmdr is vendored inside the project, but it has never been tried;
  `.gitignore` covers the leftovers it would drop into a checkout.

## Dependencies

`rpm/harbour-lauscher.spec` declares what rpmbuild cannot work out on its own.
Everything the binary links — Qt5Core/Gui/Qml/Quick/DBus, libsailfishapp,
libstdc++ — is found by the ELF dependency generator and must **not** be listed
by hand. What is listed:

| Requires | why it is not auto-detected |
|---|---|
| `sailfishsilica-qt5 >= 0.10.9` | `import Sailfish.Silica 1.0`, and `X-Nemo-Application-Type=silica-qt5` |
| `qt5-qtdeclarative-import-qtquick2plugin` | `import QtQuick 2.2`; Silica pulls it in too, but the app imports it directly |
| `bluez5` | bluetoothd owns `org.bluez` — the SDP lookup and the whole RFCOMM transport. A D-Bus peer leaves no trace in the ELF header |
| `sailjail-permissions` | `harbour-lauscher.desktop` says `Permissions=Bluetooth`, resolved against `/etc/sailjail/permissions/Bluetooth.permission` |

`BuildRequires: qt5-qttools-linguist` is there because `CONFIG +=
sailfishapp_i18n` shells out to lupdate and lrelease during `%install`; without
it the build only works by accident, on a target that happens to have them.

**This package is deliberately not harbour-compliant.** `sfdk check` runs fine
and its Dependencies, Sandboxing, RPATH, Architecture and Vendor suites pass,
but the Requires suite rejects `bluez5`,
`qt5-qtdeclarative-import-qtquick2plugin` and `sailjail-permissions` as
dependencies the Jolla Store does not allow, so `check` exits non-zero:

```sh
mkdir -p ../build-harbour-lauscher-i486 && cd ../build-harbour-lauscher-i486
sfdk -c target=SailfishOS-5.1.0.11-i486 build ../harbour-lauscher
sfdk -c target=SailfishOS-5.1.0.11-i486 check   # exits 1 on Requires
```

That is the accepted trade: the app cannot run without bluetoothd, so the
dependency gets declared. Do not paper over it with `__requires_exclude`, and do
not drop the entries to make `check` green — the earlier version of this file
told the next reader to keep `bluez5` out, and that advice is now withdrawn.

## The cover

`app/images/` holds hand-written SVGs; `app/app.pro` installs the directory
because `sailfishapp.prf` deploys `qml/` and nothing else. `CoverPage.qml`
reaches them as `../../images/...`, which resolves both in the source tree and
under `/usr/share/harbour-lauscher`.

- **Everything drawn comes in two colours.** `-white` is for a dark ambience,
  `-black` for a light one, picked with `Theme.colorScheme ? "black" : "white"`
  — `Theme.LightOnDark` is 0, so the pale artwork is the falsy case.
- **The cover-action icons are 48×48**, which is what the device's theme ships
  in `silica/z1.5/icons-monochrome`, in the style the stock `icon-cover-*` icons
  use. **Each one exists in both colours.** Nothing tints a cover action's icon
  — confirmed on the phone, where a white-only set stayed white on a light
  ambience — so `-white.svg` and `-black.svg` are picked off `Theme.colorScheme`
  exactly like the backdrop. Only the fill colour differs between the two; they
  are otherwise the same file.
- The backdrop goes through `app/qml/components/BackgroundImage.qml`, a copy of
  harbour-fernschreiber's component, anchored on the cover the way that app
  anchors it: a square as wide as the cover is tall, hung off the bottom right,
  so it overflows the cover and reads large.
- **Two `CoverActionList`s, not one with a hidden action.** Lipstick takes the
  first enabled list whole, so the distance action only exists while background
  music is the active mode. harbour-tasklist switches its cover actions the same
  way.
- The actions step to the next option rather than opening anything — a cover
  cannot show a menu — and the icon says where that landed. The mode rotation
  only contains what the device advertises, so it matches `DevicePage`'s picker.
- **Volume is named, not numbered.** The headset's 0..30 scale means nothing at a
  glance, so `volumeText()` maps it to seven steps from *off* to *maximum*.

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

**A battery reading of 0 % means nothing on its own.** A bud in the case reports
0 %, and so does one that is genuinely empty: the frame carries a level and a
charging status per side (`PowerRetStatusLeftRightBattery`) and nothing that
separates the two. `MDRBattery.present` is not that flag either - libmdr
hardcodes it to true for every advertised part. Attempts to read it out of the
charging status and the update threshold did not survive contact with the device,
so `DevicePage` and `CoverPage` both just disable the row at 0 %: still visible,
plainly not a measurement. `enabled` propagates down the item tree, which is what
dims the name and the level together.

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

**The device does not switch modes in one step, and the app has to cover for
that.** Asked for a listening mode while another is active, it reports every mode
off first and the new one 0.2-0.4 s later. Since the modes are exclusive flags,
every mode off is not a state the protocol marks as transitional - it is exactly
what Standard looks like, so the picker and the cover followed the device through
it and flicked back and forth. Switching *to* Standard never showed it: there the
device has only the one step to make.

`refreshListening()` therefore holds a mode the user asked for against a reading
of Standard for `kListeningSettleMs` (2 s). Three things about it are load-bearing:

- **Only the window ends the hold**, never a reading that agrees with the request.
  libmdr takes a staged value as current the moment the change is sent, so it
  reports the requested mode straight away and the device's confirmation cannot be
  told from our own echo. Disarming on agreement disarms instantly and the hold
  does nothing.
- A reading of some *other* mode does end it - that is the device saying it did
  something we did not ask for.
- `tick()` gives up on an unanswered request itself. The suppressed events are the
  only thing that would have re-read the mode, so without that a request the
  device never answered would leave the UI on an optimistic value for good.

This is deliberately **not** in libmdr: what the device reports is what libmdr
should report, and the timeout that makes the suppression safe needs a clock,
which a poll-driven protocol library has no business owning. The related fix that
*did* belong there - committing the staged flags before the sends instead of
after - closes a second, shorter window of the same shape, where our own
two-frame switch (deactivate, then activate) left no mode set in between.

### Confirmed on hardware, 2026-08-30

The "reasoned through but never observed" caveat this section used to carry is
retired. Against the LinkBuds Clip on the phone, all of this behaves:

- The BlueZ Profile1 handshake and the MDR session — bluetoothd hands over the
  RFCOMM fd and libmdr initializes over it, exactly as designed here.
- Track names, the transport controls and the volume slider.
- Switching between all four listening modes.

Earlier, and still true:
- Builds clean for `aarch64` and `i486` on Sailfish 5.1.0.11.
- Passes every `sfdk check` suite except harbour's Requires, which the declared
  dependencies fail on purpose (see Dependencies).
- Runs on the emulator under Sailjail.

### Confirmed on hardware, 2026-08-31

Everything in this file is now backed by a run on the phone. `0.1` was deployed
to the Jolla Phone (2026) on Sailfish OS **5.2.0.17** — note that is a newer OS
than the 5.1.0.11 target it was built against, which is the supported direction
and gave no trouble. The app starts and runs smoothly, so the QML all parses:
worth saying because the licence headers had just been added to every `.qml`
file and a parse error there is silent (see QML gotchas).

**The background-music distance picker works.** It was the last control never
exercised on hardware; the distances are audibly different from one another. So
every listening-mode path has now been driven on the device, including the one
that can reach `setListeningMode()`'s `MDR_ROOM_UNKNOWN` fallback.

### Confirmed on hardware, 2026-09-03

The listening-mode switch is clean on the phone: picking any mode while another
one is active lands on it directly, with no pass through Standard in the picker or
on the cover. Both halves of that - the libmdr commit ordering and the hold in
`refreshListening()` - are in.

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
