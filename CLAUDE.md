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
  transmit sequence number, which is also the pre-handshake frame guard; gating
  V2 init requests on the advertised function list; the four listening modes; and
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

0. `mdrHeadphonesCreate()` is told which family to speak — `MDR_PROTOCOL_V1` or
   `MDR_PROTOCOL_V2`. It is not discovered: the RFCOMM service UUID that answered
   already says which one it is, so `MdrController`'s `kServices` table pairs each
   UUID with its family and hands over the entry that connected.
1. `CONNECT_GET_PROTOCOL_INFO` → confirms it and says which tables exist.
2. `CONNECT_GET_CAPABILITY_INFO` / `CONNECT_GET_DEVICE_INFO` → model name,
   firmware, serial, colour.
3. `CONNECT_GET_SUPPORT_FUNCTION` → the feature bitmap. **Everything the UI
   shows must be gated on this**; LinkBuds Clip advertises a very different set
   than an XM5.
4. Per-feature `*_GET_CAPABILITY` / `*_GET_PARAM` / `*_GET_STATUS`.
5. Afterwards the device pushes `*_NTFY_PARAM` frames unprompted (battery,
   noise mode changes made on the device itself).

Some of those pushes carry only their discriminator and no value — V1 announces
the track names that way. libmdr reports those as `MDR_EVENT_NEED_SYNC`, which
means "ask for it": `pumpDevice()` answers with `mdrHeadphonesRequestSync()`, the
same call the app makes once initialization completes.

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

**The picker lists only devices the app can drive**, and the test is the BlueZ
`UUIDs` property carrying `MDR_SERVICE_UUID_XM5` or `MDR_SERVICE_UUID_LEGACY` —
the very UUIDs `MdrController` connects on. That list is bluetoothd's cache of
what the device has offered, over BR/EDR *and* LE, so it is the best signal
available for a picker but not a promise that the record is live right now (see
`br-connection-not-supported` below). Phones, speakers, keyboards and car
kits never appear.

**That test has to be the only one.** bluetoothd builds its per-device service
objects from the same cached list when `RegisterProfile` runs, so a device the
list does not cover has nothing for `ConnectProfile` to reach and answers
`br-connection-not-supported`. An earlier version of this filter fell back to
`Icon` (BlueZ's own reading of the class of device — `audio-headset`,
`audio-headphones`) for a device it held no UUIDs for at all; that put entries
in the picker that cannot connect, which is the opposite of the point, and cost
a debugging round on the phone. `looksLikeHeadset()` survives for one purpose
only: logging why a device that plainly is a headset was left out. A headset
missing from the picker means bluetoothd has no MDR record for it, and
reconnecting it once in the Bluetooth settings is what re-runs the SDP lookup —
the empty-list hint on `DeviceListPage` says so.

**No MAC address is shown anywhere in the UI.** Addresses are passed through as
identifiers (`connectToDevice()`, and the paired-device commands, which libmdr
validates against the headset's 17-character form) and nothing more. BlueZ
invents an `Alias` for a device that never sent a name, and what it invents is
the address with dashes for colons, so `pairedDevices()` drops that spelling of
one rather than passing it off as a name; the QML shows "Unnamed device"
instead, on both the picker and the connected-devices list.

### What `br-connection-not-supported` means

`ConnectProfile` answering `org.bluez.Error.Failed: br-connection-not-supported`
does **not** mean the app asked for the wrong thing. bluetoothd says it in its
own log as `src/profile.c:record_cb() No SDP records found for Lauscher MDR`:
it brought the BR/EDR link up, searched the headset's SDP database for the MDR
service, and got nothing back. There is no channel to hand over, so the
registered `Profile1` is never called.

**The headset does not publish that record at all times.** Browsed with
`sdptool browse <addr>` while the LinkBuds Clip sat idle, its record set was 12
entries — Handsfree, the GATT bits and vendor records (BTNOTIFYR, Airoha\_APP,
GSOUND\_BT\_CONTROL, BTFASTPAIR, BT\_SAR\_\*) — with **no MDR record and no A2DP
record either**, though bluetoothd's cached `UUIDs` for the same device listed
both. So the cache is a record of what the headset *has* offered, not of what it
offers right now, and a device can be correctly listed in the picker and still
refuse the profile a second later.

Two consequences worth keeping:

- **Reproduce transport failures outside the app.** A ~50-line Python
  `Profile1` client (`RegisterProfile` with the same options, then
  `ConnectProfile`) fails identically when the app does, which separates "the
  app's handshake is wrong" from "the headset is not offering the service" in
  one run. `journalctl -u bluetooth` then gives bluetoothd's own reason, which
  the D-Bus error keyword only hints at.
- **`ConnectProfile` leaves the ACL link up** when it fails this way
  (`Connected` goes true, `ServicesResolved` stays false). That is bluetoothd
  connecting in order to run the SDP search, not a socket the app leaked, and a
  second attempt over the standing link fails exactly the same way. The app
  must not "clean it up" — the link is the user's audio connection.

`BluezTransport::explainConnectFailure()` turns these keywords into sentences,
because the raw one on screen sends the reader looking in the wrong place.

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

The `Set*` calls take a whole struct, and libmdr validates every field in it, so
each setter reads the current one with the matching `Get*` and changes only what
it means to. That is not just tidiness: `MDRNoiseControl` grew a
`changing_asm_level` field, and a struct filled in from scratch would have been
refused outright for the field nobody remembered to set.

### Losing the headset, and getting it back

Buds going into their case take the RFCOMM channel with them, which is not a
fault and must not read as one. Three things follow from that.

- **The message is the app's, not libmdr's.** libmdr words the drop as the API
  call that noticed it — "Unable to poll the connection (no connection has been
  established)" — which sends the reader looking for a bug. `pumpDevice()`
  therefore branches on the **result code** rather than passing the text on:
  `MDR_RESULT_ERROR_NO_CONNECTION` and `MDR_RESULT_ERROR_NET` are the link going
  away and get `handleLinkLost()`; everything else is still reported as libmdr
  worded it, because it is a protocol fault the user did not cause. The transport
  says the same thing its own way — `handleRequestDisconnection()` raises
  `linkLost()` with no message attached, and the controller supplies the words.
- **The pulley entry says what there is to do.** `MdrController::connected` is
  every state but `Idle` and `Error`, and `DevicePage`'s second menu item reads
  Disconnect or Connect off it. Disconnect keeps its old meaning — pop the page,
  which is what drops the channel — while Connect calls `reconnectDevice()` and
  stays put, so a headset that went away can be picked up without a trip through
  the list.
- **The app reconnects by itself, and BlueZ says when.** Asking for a device arms
  it: `connectToDevice()` sets `m_autoReconnect` and has the transport subscribe
  to that one device's `PropertiesChanged` (`watchDevice()`). `ServicesResolved`
  or `Connected` going true means the headset is back on the phone, and a
  `kReconnectSettleMs` delay covers the gap between the link coming up and the
  MDR record being published. Only leaving the page or disconnecting from the
  menu disarms it. That subscription is the only thing that restarts the machine
  once it has settled into waiting, so a subscription that could not be
  established leaves the pulley's Connect entry as the only way back —
  `watchDevice()` says so in the log.

Two rules keep that from becoming a loop, and both were learned on the phone.

**Nothing is tried while BlueZ has no link to the headset.** `attemptReconnect()`
asks `isDeviceConnected()` first, and a device that is not there costs no attempt
and schedules nothing — the watch is what wakes it up again. Without that check
the timer fired into the void: buds in the case, and every couple of seconds the
notice was replaced by "Connecting…" and then by a failure, over a headset that
bluetoothd could not even run an SDP search against. `Connected` is the property
to ask, not `ServicesResolved`: a failed `ConnectProfile` leaves the ACL link up
with the services unresolved, and that is precisely the state a retry is for.

**A failure is only shown once there is nothing left to try.**
`connectAttemptFailed()` takes the reason, logs it, and leaves the page saying
what `pendingStatus()` says; only when the attempts run out does it hand the
reason to `fail()`, which is the verdict. On the phone the other way round read
as a malfunction — "The headset is not offering its control channel", gone six
seconds later, back again — rather than as an app being patient. The two
sentences `pendingStatus()` picks between follow from BlueZ, not from our own
attempts, so the message holds still across a whole run of them.

Attempts are capped at `kReconnectAttempts` and spaced `kReconnectRetryMs` apart;
the count resets only when the device turns up again or a session opens, so a
headset that is awake but silent is waited for rather than chased. The
`reconnecting` property is that whole wait, which is why the notice on
`DevicePage` drops out of `Theme.errorColor` while it holds — the app is not
reporting a failure, it is saying what it is doing. The spinner does **not**
follow it: it runs on `Connecting`/`Initializing` only, because a spinner that
turns for as long as the buds are in their case says stuck, not patient.

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
app/qml/pages/EqualizerPage   preset, band steps and clear bass
app/qml/pages/SettingsPage    the device's own settings: its booleans, link quality
app/qml/pages/AboutPage       logo, what to know about the app, credits
app/qml/components/           small shared QML: the cover backdrop, about-page bits,
                              the equalizer's vertical band
app/qml/cover/CoverPage       the cover: status at a glance, mode and distance actions
app/icons/                    the rendered app icon, one PNG per launcher size
app/images/                   app icon master, cover artwork, cover-action icons (SVG)
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

### Reading libmdr's own log

`MDR_ENABLE_LOG` and `MDR_DEBUG` are **two separate macros**, and
[libmdr/libmdr.pro](libmdr/libmdr.pro) sets only the first. `MDR_LOG` writes to stderr
when `MDR_ENABLE_LOG` is on, but `MDR_LOG_DEBUG` — which is where the per-frame
traffic and the unhandled-command lines are — expands to nothing unless `MDR_DEBUG`
is defined as well. So a stock build's journal carries the app's own warnings and
none of the protocol.

```sh
# in libmdr/libmdr.pro, for a debugging build only
DEFINES += MDR_DEBUG
```

That is deliberately not on by default: it is loud, and the frames carry Bluetooth
addresses and media metadata. It is also the first thing to reach for when a device
behaves in a way the app cannot explain — it is how pull request #2's author found
the voice-guidance fault on a WH-1000XM4, and it would have cost them an hour less
had this been written down.

### Nightlies

`.github/workflows/nightly.yml` builds an RPM per architecture on every push and
attaches them to the run as a downloadable artifact, so a fix can be handed to
whoever reported it without cutting a release. It uses
`coderus/github-sfos-build`, which runs `mb2` inside
`coderus/sailfishos-platform-sdk` — the same route harbour-fernschreiber uses,
and an **in-tree** build, which is safe there only because each architecture gets
a fresh container.

- **The release tag is 5.1.0.11 and cannot go lower.** libmdr needs C++20, and
  that arrived in the Sailfish toolchain with this target's GCC 13.4.
- **Push is the only trigger**, deliberately. Tags are what a release would be
  cut from and pull requests carry code from outside the repository; neither
  should produce something that looks like a nightly.
- **The workflow rewrites `Release` before building**, to
  `0.<UTC timestamp>.git<commit>`. RPM compares that field piecewise, so a
  nightly sorts above the previous release and below the one its `Version`
  names: a tester on a nightly is upgraded by the real 0.2, not blocked by it.
  `Version` is left alone — that is the spec's own statement.
- `sfdk check` is not run there, and should not be: this package fails harbour's
  Requires suite on purpose (see Dependencies).

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
- **A `CoverActionList` per combination, not one with hidden actions.** Lipstick
  takes an enabled list whole, so what a device does not have has to be absent
  from the list rather than hidden inside it. harbour-tasklist switches its cover
  actions the same way. The four lists' conditions are mutually exclusive, so
  which one lipstick finds first does not decide anything.
- **The cover covers both of the headset's mode settings**, because a device may
  have either. The listening mode is one action and the ambient sound control is
  another; a closed-back headset advertises the ambient sound control and, by the
  WF-C700N report this came from, none of the listening modes, so gating the whole
  cover on those left it with no actions whatsoever — which is what "the cover
  actions do not work" turned out to mean, and a WF-C700N confirmed both halves of
  the fix.
- **Two actions is lipstick's ceiling, so there is a priority.** A headset with
  both settings and background music playing has three things to step through.
  The distance takes the second slot then, because it applies only while that one
  mode is active while the other two are there for the rest of the session.
- The actions step to the next option rather than opening anything — a cover
  cannot show a menu — and the icon says where that landed. Both rotations only
  contain what the device advertises, so they match `DevicePage`'s pickers:
  `MdrController.listeningModes` for the one and `noiseModes` for the other,
  the latter built the same way and for the same reason — libmdr validates the
  struct rather than the mode against the feature list, so an unadvertised mode
  would be staged and quietly ignored.
- **Volume is a percentage here, as it is on `DevicePage`.** The headset's 0..30
  scale means nothing at a glance, so the cover reads `MdrController.volumePercent`
  — the same conversion the slider's `valueText` runs through `volumeToPercent()`.
- **The equalizer preset is deliberately not here.** Issue #3 asked for it in both
  places and got the `DevicePage` button, which now reads "Equalizer (Bass)"; the
  cover was declined as too much for the space. The string exists and is one
  `Label` away — `MdrController.equalizerPresetLabel`, the same property the button
  uses — so this is a decision about what the cover is for, not a gap. What is on
  it is what can be read at a glance and acted on from the two actions below it,
  and the preset is neither.

## The app icon

`app/images/harbour-lauscher.svg` is the master and `app/icons/<size>/` holds
what it renders to; `SAILFISHAPP_ICONS` installs those four PNGs and nothing
else. The master sits in `images/` rather than beside them because that
directory is installed as a whole (see The cover), which is what lets
`AboutPage` show the icon as its logo, drawn from the SVG at whatever size the
screen gives it. It follows Jolla's [Apps icon
story](https://sailfishos.org/content/uploads/2018/11/48_SAILFISH-APPS-ICON-STORY.pdf):

- **86 units, filled edge to edge.** The guide asks for an 86 px icon with no
  padding beyond a 0.3-unit gap on each side, for a continuous anti-aliased
  outline. The viewBox is that canvas, so every number in the file is a guide
  unit.
- **The base shape is the arch**, one of the sixteen the guide builds by merging
  a circle with a rounded rectangle: a half circle of radius 42.7 on top of a
  body whose bottom corners round by **3.5**. That radius is measured off the
  guide's own artwork rather than guessed — the shapes are drawn there at
  exactly 86 pt, so the numbers transfer one to one.
- **The metaphor is the cover artwork's headphone**, scaled by 0.48 and centred.
  That lands it 12 units in from each side, which is exactly the inset outline
  the guide draws to keep metaphors inside the shape.
- **The gradient is diagonal, dark to light**, matching the guide's own green
  arch, which runs its fill corner to corner rather than straight down.

Re-render after editing the SVG — one command per installed size:

```sh
for s in 86 108 128 172; do
    rsvg-convert -w $s -h $s app/images/harbour-lauscher.svg \
        -o app/icons/${s}x${s}/harbour-lauscher.png
done
```

Inkscape does as well. ImageMagick alone does **not**: its internal SVG renderer
silently drops both the gradient and the stroked headband, leaving a black
silhouette, and the `rsvg-convert` delegate it would otherwise use is a separate
package.

## QML gotchas already paid for

Four bugs cost real debugging time here; do not reintroduce them.

The host's own `qmllint` runs over these files without a device or an emulator —
it only parses, so the Silica imports it cannot resolve do not bother it:

```sh
find app/qml -name '*.qml' -exec qmllint {} +
```

Worth running, but know what it is worth: it catches **syntax** and nothing
else. A file with `Text.Wordwrap`, an undeclared property and an undefined type
passes it clean — which is to say it would have caught none of the three bugs
below. It is a guard against a stray brace, not against any of this.

For the names it cannot check, read them off the target in
`~/SailfishOS/mersdk/targets/SailfishOS-<version>-<arch>.default/usr/lib*/qt5/qml/Sailfish/Silica`:
the QML sources are there, and `plugins.qmltypes` covers what is implemented in
C++ — that is where `Separator.horizontalAlignment` turns out to come from
`Underline`.

- **`import "pages"` in `app/qml/harbour-lauscher.qml` is mandatory.** Without it
  `DeviceListPage {}` does not resolve, and the failure mode is *silent*: the app
  starts, stays running, and paints a **uniformly white window**. No QML error
  reaches stderr or the journal under Sailjail. If you ever see a blank white
  app, suspect an unresolved QML type first.
- **`Text.WordWrap`, not `Text.Wordwrap`.** The misspelling is not an error in
  QML — the value is simply undefined and text silently stops wrapping.
- **`PageStatus.Deactivating` does not mean the user left the page.** Silica
  sets it when a page is pushed on *top* of one, too - that is `PageStack.qml`'s
  `pushExit()`. `DevicePage` drops the RFCOMM channel when it is left, and
  hanging that on the status meant opening the about page from its pulley menu
  disconnected the headset. It uses `Component.onDestruction` instead: PageStack
  owns pages pushed by URL and destroys them when they are popped, so that fires
  only on the way out for good. Anything else that must survive a pushed page
  needs the same distinction.
- **`allowedOrientations` on `ApplicationWindow` rotates nothing by itself.** Silica
  gives every `Page` its own `allowedOrientations`, defaulted to
  `ApplicationWindow._defaultPageOrientations`, which is `Orientation.Portrait` — so
  the window saying `defaultAllowedOrientations` is overruled page by page, and the
  app simply does not turn. Every page here sets `Orientation.All`; `Page` intersects
  that with the window's, so it still means "whatever the device permits" rather than
  overriding the device's own configuration. A page added later needs the line too.

Also: Silica's `ComboBox.currentIndex`, `Slider.value` and `TextSwitch.checked`
are **written to** by the controls themselves. Binding them to a `mdr.*` property
works exactly once — the first user interaction destroys the binding and the
control then ignores changes made on the headset itself. `DevicePage.qml`
therefore assigns the first two from `Component.onCompleted` plus a `Connections`
block instead of binding.

`TextSwitch` (and `Switch`) has a way out that the other two lack:
**`automaticCheck: false`** stops it assigning `checked` on click, so the binding
survives and the switch shows what the device reports rather than what was
tapped. `onClicked` then fires while `checked` still holds the old value, which
is why the handlers send `!checked`. Both switches on the page do it this way —
"Focus on voice" had the plain binding and would have gone deaf to the headset
after its first tap.

## Status / next steps

Proof of concept. Working: paired-device listing, connect, identity, battery,
playback (track names, play/pause/next/previous, volume), ambient sound control
(off / NC / ambient + level + focus-on-voice), listening mode (all four, plus the
background-music distance), the headset's own connected devices (which one plays,
connect and disconnect, and whether the headset may move playback itself), the
equalizer (preset, band steps, clear bass), DSEE, cover page.

**The device's own settings read and write, as of 2026-09-10.** `SettingsPage` shows
multipoint and the Bluetooth connection quality, agrees with Sound Connect, follows a
change made there, and both write from here — confirmed on the LinkBuds Clip. Getting
there took two unrelated fixes; the section below has them.

Everything under Playback rides on one event. Volume, play/pause status and the
track names all report `MDR_EVENT_PLAYBACK_CHANGED`, so `refreshPlayback()` reads
all three, and all of them arrive unprompted as well as on request.

- **Volume is 0..30 on the wire and 0..100 % in the UI.**
  `mdrHeadphonesSetPlayback` rejects anything above 30, so `setVolume()` and the
  slider both step in the device's own 31 steps — every slider position is one the
  headset has — and only the readout is converted, by `MdrController::volumeToPercent()`.
  `DevicePage` and `CoverPage` (via the `volumePercent` property) share it, so the two
  never disagree; `maximumVolume` keeps the 30 out of the QML. The struct
  `mdrHeadphonesSetPlayback` takes carries the play/pause status too, and libmdr refuses
  one that asks for a state change, so the setter reads the current struct and puts the
  status back unaltered.
- **Track names come from the phone, not the headset.** They are whatever the source
  device pushed over AVRCP, so all three being empty is normal, not a fault — the
  block hides itself in that case. Watch for this when testing: a silent Now-playing
  section usually means the phone is not pushing metadata, not that the app is broken.
- **A V1 headset has to be asked for them.** A WH-1000XM4 says nothing on the control
  link when the phone moves to the next track, so the name froze at whatever was playing
  on connect (pull request #2). While connected to V1, `resyncState()` therefore reads the
  playback state and calls `mdrHeadphonesRequestSync()` every `kResyncIntervalMs` (3 s).
  A V2 headset pushes `PLAY_NTFY_PARAM` with the new name by itself, so the timer is never
  started for it - a permanent beat on the control link of a device that does not need
  one is exactly what this avoids.
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
one does not shift the others, whereas on this page a `Repeater`-built menu would
have been filled in only after the last `currentIndex` assignment had already run
against an empty menu — `DevicePage` is built while the device is still being
read. `MdrController.listeningModes` carries the advertised set for the `visible`
bindings.

**That is a rule about timing, not about `Repeater`.** `ComboBoxController`
resolves `currentIndex` against the menu items that exist at the moment it is
assigned: `_updateCurrent()` walks `_contentColumn.children` on every assignment,
and `onCurrentIndexChanged` calls it again. So a `Repeater`-built menu works
wherever the assignment is repeated after the items arrive, which is what
`EqualizerPage`'s preset picker does — see Equalizer.

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

### Ambient sound on V1

The C ABI does not describe a V1 device's noise control the way it describes V2's.
`mdrHeadphonesGetNoiseControl` reports a single "on", `MDR_NOISE_MODE_V1_ON` - the same
value as `MDR_NOISE_MODE_CANCELLING` - and puts the mode in `ambient_level`: -1 (`0xFF`) is
noise cancelling, 0 wind noise reduction, 1..20 ambient sound at that level. Read as V2,
ambient sound shows as noise cancelling and can never be picked.

- **Reading:** on V1, `refreshNoiseControl()` turns a level of 1..20 into `AmbientSound`
  and anything below into `NoiseCancelling`. The UI has no wind noise reduction, and noise
  cancelling is the nearer of the two. The last ambient level is kept through a spell of
  noise cancelling, which reports none.
- **Writing:** `setNoiseMode()` sends `0xFF` for noise cancelling and the kept level for
  ambient sound, 20 if none has been seen; `setAmbientLevel()` stays at 1 or above, since
  0 would be wind noise reduction.
- Confirmed on a WH-1000XM4 on 2026-09-11: off, noise cancelling and ambient sound from
  the app, the level and focus on voice, and the headset's own button, each answered by
  the matching `NCASM_NTFY_PARAM`.

### DSEE

Sony's upscaling. It rides in `MDREqualizer` and arrives on the same
`MDR_EVENT_EQUALIZER_CHANGED`, but it is its own feature bit and its own switch, so
`refreshEqualizer()` compares it separately and reports it on `dseeChanged` — the
equalizer page rebuilds a picker on `equalizerChanged`, and a switch flip has no
business doing that.

- **It sits under the Equalizer button on `DevicePage`**, not on the equalizer page.
  It is one switch, and the device gates it on exactly the terms it gates the
  equalizer, so it belongs where the reader can see both go grey together.
- **Two gates again**: `MDR_FEATURE_DSEE` for having it,
  `MDREqualizer.dsee_available` for acting on it now. The second goes false while a
  listening mode other than Standard is active, which the WF-LC900 does over
  `AUDIO_NTFY_STATUS UPSCALING DISABLE`.
- **Both gates depend on the device having said so**, and until the libmdr refresh below
  the only thing that ever said it was an unsolicited notification. Nothing re-read either
  status — not even `mdrHeadphonesRequestSync()` — so a client that missed one frame kept
  a stale answer for the rest of the session. Applying a listening mode now asks for both
  statuses, which is what makes the Equalizer button and this switch grey out together and
  reliably. A mode changed on the headset itself still arrives as a notification; that path
  is unchanged.
- **One note for both controls.** The listening mode takes the equalizer and DSEE away
  together and on the same terms; two sentences saying so side by side read as a
  stutter, so the label under the switch names whichever of the two this device has.
- **On is the device's automatic mode.** libmdr stages the upscaling setting as `AUTO`,
  not as a fixed strength — the headset decides which sources want it. The switch says
  so rather than implying a level.
- **The switch is labelled with the device's own name for it.** Sony ships DSEE, DSEE
  HX, DSEE HX AI and DSEE Ultimate, and the device reports which one it has in its
  audio capability; `dseeTypeName()` turns that into the label. A LinkBuds Clip reports
  plain `DSEE`. These are product names, so only the fallback for a device that did not
  say is translated.

### Connected devices (multipoint)

Sony calls it multipoint: the headset holds two source devices at once and one of
them has playback. `MdrController::refreshMultipoint()` reads all of it and the
"Connected devices" section on `DevicePage` shows it.

- **Two feature bits, not one.** `MDR_FEATURE_PAIRED_DEVICE_MANAGEMENT` says the
  headset keeps a list of what it is paired with; `MDR_FEATURE_SOURCE_SWITCH_CONTROL`
  says playback can be pinned to one of them. A device may advertise either without
  the other, so the list and the switch are gated separately and the section header
  appears for whichever exists.
- **One event covers the lot.** `MDR_EVENT_PAIRED_DEVICES_CHANGED` reports the
  device list, which entry holds playback, the automatic-switching flag and a
  refusal — libmdr raises it for all four — so a single refresh reads them all,
  the same shape as `refreshPlayback()` under Playback.
- **Nothing is reflected optimistically**, unlike the listening mode. Every
  paired-device command is answered by a notification carrying what the headset
  actually did, refusals included, so predicting the outcome here would only
  compete with the truth arriving a moment later.
- **A refusal leaves the old state standing**, which on its own is
  indistinguishable from a tap that did nothing. `mdrHeadphonesGetSourceSwitchControlResult()`
  is where the reason lives - on a call, not connected, voice assistant busy - and
  `multipointMessage` carries it to the page. Staging a new request clears it, in
  libmdr and here.
- **The address must be the headset's own 17-character form.** libmdr validates
  that and refuses anything else, which is why the list's `address` is passed back
  untouched rather than reformatted for display.
- Turning multipoint itself on and off is **not** here, but it is in the app: it
  is not a dedicated request in the protocol, it sits among the device-defined
  booleans behind `mdrHeadphonesGetGeneralSetting*`, so it lives on
  `SettingsPage` with the rest of them (see The device's own settings). This
  section shows what the headset reports either way.

### The device's own settings

`SettingsPage`, reached from `DevicePage`'s pulley menu, holds what the headset
keeps about itself rather than about the sound: the booleans it defines on its
own — multipoint is one — and what its Bluetooth link is tuned for. Two unrelated
features, so the menu entry is there for whichever of them the device has.

- **The device defines the booleans, names them, and decides how many.**
  `mdrHeadphonesGetGeneralSettingInfo` reports up to four slots and
  `MDR_TEXT_GENERAL_SETTING_SUBJECT`/`SUMMARY` name each one, so the page is a
  `Repeater` over whatever came back rather than a fixed set of switches.
- **Only the boolean ones, and only the writable ones.** The C ABI carries
  `MDR_GENERAL_SETTING_BOOLEAN` and nothing else: a list-shaped setting reports
  `MDR_GENERAL_SETTING_UNKNOWN`, `mdrHeadphonesGetGeneralSetting` answers
  `MDR_RESULT_ERROR_NOT_SUPPORTED` for it, and `refreshGeneralSettings()` leaves
  it out. That is not hypothetical — see the replay below.
- **The names arrive as keys, not sentences.** A device answering
  `GsStringFormat::ENUM_NAME` sends `MULTIPOINT_SETTING`, not "Connect to two
  devices"; one answering `RAW_NAME` sends the words. The C ABI does not carry
  which of the two it was, so `looksLikeToken()` decides on the shape of the
  string — a key is upper case with underscores and words are not.
  `generalSettingTitle()` translates the keys it knows and tidies up the ones it
  does not (`MULTIPOINT_SETTING` → "Multipoint setting"), so a headset offering
  something never seen here is still usable. `generalSettingDescription()` does
  the same but falls back to **nothing**: a key nobody has words for tells the
  reader less than the switch's own name already did.
- **Connection quality is a feature of its own**, `MDR_FEATURE_CONNECTION_MODE`,
  read and written as `MDRConnectionMode.audio_priority`
  (`MDR_AUDIO_PRIORITY_QUALITY` / `_STABILITY`). Its picker returns **-1** for a
  device that has not said which it is on, the way `EqualizerPage`'s preset picker
  handles a preset it cannot show, and `onCurrentIndexChanged` treats a negative
  index as "not a request" — otherwise the assignment on page load would send the
  device the first item.
- **Both kinds of change drop the Bluetooth links, and the device asks first.**
  Writing one of these does not apply it. The device acknowledges the request,
  holds it, and sends `ALERT_NTFY_PARAM FIXED_MESSAGE <reason> POSITIVE_NEGATIVE`
  — the reason being `DISCONNECT_CAUSED_BY_CHANGING_MULTIPOINT` or
  `DISCONNECT_CAUSED_BY_CONNECTION_MODE_CHANGE`, since applying it costs every
  link it has. Unanswered, the held request is dropped in silence and the setting
  reads back unchanged; that was the whole of this section's first version not
  working, and the desktop client has the same bug (see
  [libmdr/UPSTREAM.md](libmdr/UPSTREAM.md), `2497040`).
- **The answer is yes, and the app gives it without asking again.**
  `MDR_EVENT_ALERT` sets `m_alertPending` and `pumpDevice()` answers with
  `mdrHeadphonesRespondToAlert(MDR_ALERT_ACTION_POSITIVE)` — next to the commit,
  and for the same reason: a request still running answers `MDR_RESULT_INPROGRESS`
  and the next tick tries again. Putting the question to the user would be asking
  about something already agreed to, since the only changes this app makes are the
  ones just made on a page that says the headset disconnects for a moment. The
  page saying so is what makes that defensible — and the reconnect then picks the
  session back up when the device does disconnect.

**The LinkBuds Clip's answer is on record**, from replaying
`tests/WF-LC900-2.0.3` through the C ABI offline (the harness the top of this
file describes, ~150 lines against the vendored sources — no phone involved). It
advertises two general settings: index 1, boolean and writable, subject
`MULTIPOINT_SETTING`, summary `MULTIPOINT_SETTING_SUMMARY_LDAC_AVAILABLE`; and
index 2, `TAP_SENSITIVITY_SETTING`, a list of LOW/HIGH that the ABI reports as
unknown and refuses to read. It advertises `MDR_FEATURE_CONNECTION_MODE` and was
captured on `MDR_AUDIO_PRIORITY_QUALITY` — it reads that back as
`AUDIO_RET_PARAM 0x00 0x00`, `CONNECTION_MODE` carrying `SOUND_QUALITY_PRIOR`, in
answer to the `AUDIO_GET_PARAM 0x00` the init chain sends. That is also where the
summary's `_LDAC_AVAILABLE` suffix comes from: it is the variant a device with LDAC
sends, because switching multipoint on is what takes LDAC away.

**Read the inquired type off the read, never off a struct's default.** An earlier
version of this file took the `AUDIO_SET_PARAM 0x05 0x00` the harness produced for
a sound-quality write as the device's own `CONNECTION_MODE_CLASSIC_AUDIO_LE_AUDIO`
variant, and called the mapping confirmed. It was a bug being mistaken for evidence:
libmdr never assigned the field, so the frame carried
`AudioSetParamConnection`'s default (0x05) instead of the `CONNECTION_MODE` (0x00)
that everything else in the exchange uses — and short by a byte for the variant it
claimed, since that one carries a fourth field. The device acknowledges such a frame
and drops it, which is what a write that does nothing looks like from the outside.
Fixed upstream in `e8be775`; the same write is `e8 00 01` now. The lesson generalizes:
the replay shows what libmdr sends, and only the device's own frames say what it wants.

**Answering the alert is what made multipoint work**, confirmed on the LinkBuds Clip on
2026-09-10: the switch is written, the headset asks, the app says yes, the links drop
the way the page warns they will, and after the reconnect the new value is what stands.
Nothing else was needed for it — no re-sending of the held request after the yes, which
was the third of the three things this section used to list as unknown.

The connection quality did not come along, and the reason turned out to be unrelated to
the alert: the write named the wrong inquired type and the device never saw a command it
recognized, so it had nothing to ask about. That is `e8be775` above, and with it the
setting takes on the device too. **Two silent failures with one symptom** is the thing to
remember here — a write that is held pending an answer and a write that is dropped as
unrecognized look identical from the client, and the second was hidden behind the first
for as long as the first was unfixed.

**The alert handshake came out of the same harness**, replaying a capture of the
desktop client failing to turn multipoint off: `GENERAL_SETTING_SET_PARAM
GENERAL_SETTING2 BOOLEAN OFF` goes out, the device ACKs it, and 6 ms later sends
`99 00 07 01` — the question. Answering it puts `98 00 07 01` on the wire, the
same message type coming back with POSITIVE. Both halves were confirmed offline
before anything was tried on the phone, which is the point of keeping the replay
route open: a write that does nothing looks identical to a write that never
happened, and the frames say which it is.

### Equalizer

`MDR_FEATURE_EQUALIZER` puts a button under the listening-mode picker on
`DevicePage`; it pushes `EqualizerPage`, which shows the preset, the band steps and
clear bass. `MdrController::refreshEqualizer()` reads all of that on
`MDR_EVENT_EQUALIZER_CHANGED`, which the device raises for every part of it.

- **The button names the preset** — "Equalizer (Bass)" — from
  `MdrController.equalizerPresetLabel`, which runs the current preset through
  `equalizerPresetName()` rather than looking it up in `equalizerPresets`. The device
  selects Custom by itself after a band write and need never have advertised that
  preset, which is precisely the moment the reader wants to be told; a lookup in the
  advertised list would come up empty there. A preset with no name to give falls back
  to the plain label rather than to empty brackets. Silica grows a `Button` past
  `preferredWidth` to fit its text, so this one is capped at the page margins and the
  label fades — `R&B / Hip-Hop` is the name that makes that matter.
- **Two gates, and they mean different things.** `MDR_FEATURE_EQUALIZER` says the
  headset has an equalizer at all — no bit, no button. `MDREqualizer.available`
  says it will act on a change *right now*, and it goes false while any listening
  mode other than Standard is active. That is the disabled button, not a hidden
  one, and `EqualizerPage` dims its controls and says so if the device switches it
  off while the page is open. `equalizerAvailable` and `equalizerUsable` are those
  two, in that order.
- **The band layout is the device's.** It reports five bands stepping ±10 with a
  clear-bass control beside them, or ten stepping ±6 with none, or no bands at all
  — libmdr refuses anything else, and the LinkBuds Clip is a ten-band device, so
  clear bass never appears on it. The frequency labels come from
  `MdrController::equalizerBandLabel()`, in the order the frames carry them.
- **The presets are the ones the device advertised.** `EQEBB_GET_CAPABILITY` answers
  with the ids a headset has — a small subset of the thirty `MDREqualizerPreset` can
  express — and `mdrHeadphonesGetEqualizerPresets` reports them in the order they were
  listed. That request is work this repository asked for in the SonyHeadphonesClient
  checkout (see [libmdr/UPSTREAM.md](libmdr/UPSTREAM.md)); before it, the page offered
  everything and left the device to ignore what it did not have.
- **An empty list means the device has not said**, not that it has no presets: an
  equalizer variant whose capability carries no list, or one that never answered.
  libmdr refuses nothing while it is empty, so `equalizerPresetList()` falls back to
  everything the C ABI can encode for the family — V1 has no Heavy, Clear, Hard, Soft,
  Gaming or FPS preset and `mdrHeadphonesSetEqualizer` refuses those outright, hence
  the split. The family is read off `kServices[m_serviceIndex]` rather than `MDRModel`:
  this can run before `refreshIdentity()` has.
- **The names are ours, not the headset's.** `MDR_TEXT_EQUALIZER_PRESET_NAME` carries
  what the device calls each preset, but in the one language libmdr asked for, which is
  English. `equalizerPresetName()` is translated, so it wins; an id it has no name for
  is one `MDR_EQ_UNKNOWN` covers, which cannot be selected either and is dropped. On the
  LinkBuds Clip the point is moot in the other direction: it sends an **empty name for
  every preset**, so there would be nothing to show.
- **A preset change must not carry band steps.** Band steps are what makes an EQ custom,
  so a preset write followed by the curve that was on screen before lands the device on
  Custom, flat. That was a libmdr commit-path bug - `pending()` catching the device's own
  mid-commit report of the new curve - fixed in the checkout and vendored with the rest;
  nothing in this app works around it.
- **The device selects Custom itself when bands are written, and says so.** Nothing here
  asks it to. Writing band steps while a named preset is active makes the LinkBuds Clip
  report `CUSTOM` (protocol `0xa0`) unprompted, and `refreshEqualizer()` picks that up like
  any other report, so the picker follows on its own. Confirmed on the device on
  2026-09-10, and visible four times over in `tests/WF-LC900-2.0.3-equalizer`, where every
  preset the old desktop client selected is followed by a `CUSTOM` notification the moment
  it wrote the curve.

  **Do not add a preset write in front of a band write on that basis.** It would be
  redundant here, and it puts a preset change immediately before a curve change - which is
  the sequence that lets the device's report of Custom's *stored* curve land between the
  two and repaint the sliders under the finger. That is the bug above, arriving from the
  other side. A V1 device does not need it either: a WH-1000XM4 selects Custom the same
  way, see below.
- **On V1 a preset and a curve never share a frame.** `EQEBB_SET_PARAM` carries a preset
  and band steps, but a V1 device takes one of them at a time: the preset on its own
  (`58 01 <preset> 00`), which it answers with that preset's curve, or a curve with the
  preset left `UNSPECIFIED` (`58 01 FF 06 ...`), after which it reports `CUSTOM` by itself.
  A frame carrying both is acknowledged and dropped - a WH-1000XM4 keeps its preset and its
  curve - so `RequestCommitV1` sends one or the other. Confirmed on that headset on
  2026-09-11; Gadgetbridge writes it the same way.
- **The list has its own signal.** It arrives whenever the capability answer does —
  `MDR_EVENT_EQUALIZER_CHANGED` covers it like everything else about the equalizer, so
  `refreshEqualizer()` reads it — but the picker is built from it, and restating it on
  every band move would take the list down and rebuild it. Hence
  `equalizerPresetsChanged`, emitted only when the list actually differs.
- **The preset picker is a `ComboBox` with a `Repeater`-built menu**, which the QML
  gotchas rule out on `DevicePage` and allow here: the list is in hand before this page
  can be opened, and `syncCurrent()` assigns `currentIndex` again whenever the list or
  the device's preset changes, so `ComboBoxController` always resolves against the items
  on screen. The `Repeater`'s `onCountChanged` covers a list that arrives late. Silica
  turns a menu of more than five items into a page of its own, which is what makes the
  thirty-item fallback usable in a `ComboBox` at all.
- **The bands are a control of their own**, `app/qml/components/EqualizerBand.qml`,
  laid out in a `Row` the way an equalizer has drawn them since they had faders.
  Silica has no vertical slider and its horizontal one cannot be turned on its side:
  `SliderBase` lays the groove along the item's width, drags on the X axis, and
  reserves a row for a label and another for the value — ten of those is a page and a
  half of scrolling that still does not show the curve. The band draws a groove, a
  0 dB line and a fill that runs from that line rather than from the bottom, so the
  length is the boost or the cut and the side says which.
- **Dragging a band is relative, and the band claims the gesture.** Relative, because
  ten targets side by side means a control that snapped to the touch would rewrite a
  band every time the strip was brushed; a tap therefore changes nothing. Claimed via
  `preventStealing`, because the page scrolls the way the band drags and whoever grabs
  first keeps it — without it every drag goes to the flickable. The cost is that the
  page cannot be scrolled by starting on the strip; the page margins either side of it
  are outside the bands and still scroll, which is the way out. A gesture that declares
  itself horizontal is rejected for the rest of the press, so a sideways swipe does not
  get charged to a band.
- **Two spellings of each frequency, one table.** `equalizerBandLabel()` is what fits
  under a band a tenth of the screen wide (`31`, `1k`, `16k`); `equalizerBandFrequency()`
  spells the same entry out for the readout (`16 kHz`), deriving the unit from the
  trailing k rather than tabling it twice.
- **The `Repeater` is modelled on the band *count*, not the values.** The values change
  on every step, and a list model would destroy and rebuild a band underneath the finger
  dragging it.
- **Setting one field means sending them all.** `mdrHeadphonesSetEqualizer` stages
  the preset, clear bass and DSEE together and validates each, and
  `mdrHeadphonesSetEqualizerBands` takes the whole band array, so every setter reads
  the current state first and changes only its own field — the same reason
  `setVolume()` puts the playback status back unaltered.
- **DSEE is not on this page.** It rides in the same `MDREqualizer` struct but is its
  own feature bit and its own switch, on `DevicePage` — see DSEE below. The equalizer
  setters pass it back untouched, as it passes theirs back.

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

### Confirmed on hardware, 2026-09-04

The connected-devices section works against the LinkBuds Clip. So the device does
advertise the paired-device management and source-switch bits, the list it reports
is the one Sound Connect shows, and moving playback between two connected devices
from here does what it says.

### Confirmed on hardware, 2026-09-08

The equalizer page works against the LinkBuds Clip, bands included. So the whole
chain behind it holds on the device: the capability request is answered and the
picker offers the eight presets that device has rather than the thirty the ABI can
express, a preset applies and stays applied, and dragging a band moves the sound
and the slider stays where the device leaves it.

That last part is the one worth naming, because it is where the two libmdr fixes
meet. A preset change writes no band steps, so it no longer collapses into Custom;
a band edit still writes, so the sliders are not merely decorative. The pair only
looks right if both halves are — one without the other reads as a control that
does nothing, or one that undoes itself.

The `Repeater`-built `ComboBox` menu behaves too, which is the reading of
`ComboBoxController` in the QML gotchas confirmed rather than argued.

**DSEE and the gating were confirmed in the same session.** The switch turns upscaling
on and off — audibly, if you believe the ears; there is a short break in playback as the
device applies it, which is the reliable part — and it greys out while a listening mode
other than Standard is active. The Equalizer button beside it did not, at first: both
flags come from one `MDREqualizer` read, so the answer underneath had to be stale, and it
was. Only an unsolicited notification ever carried it and nothing re-read it. With the
libmdr refresh that asks after a listening change, the two grey out together.

**The bands changed shape the same day, and were driven again afterwards.** They were
horizontal Silica `Slider`s in the run above and are a strip of vertical faders now;
every band of the new one has been moved on the device since. So what is confirmed is
not only the reads and writes underneath — `EqualizerBand`'s drag arbitration holds too:
claiming the gesture from the flickable rather than losing it, and moving by how far the
finger moved rather than to where it landed.

### Confirmed on hardware, 2026-09-09

Reconnect-on-wake works against the LinkBuds Clip: buds into the case drops the
session and the page says so in the app's own words rather than libmdr's, and
taking them out picks the session up again by itself.

The first cut of it got two things wrong on the phone, and both are fixed in
`attemptReconnect()` and `connectAttemptFailed()` above: it reached for a headset
that was not connected to the phone at all, and it flashed each attempt's failure
on screen as though that were the answer. What the run confirmed underneath is
the part that matters — BlueZ's `PropertiesChanged` on the device does arrive
when the buds come back, so the watch is a working wake-up and not a hopeful one.

### Confirmed on hardware, 2026-09-10

`SettingsPage` writes, against the LinkBuds Clip. Both settings it offers now apply:
multipoint once the confirmation the device asks for is answered, and the connection
quality once the write names the inquired type the device advertised. Changing either
drops the Bluetooth links the way the page says it will, the reconnect picks the session
back up, and the new value is what the next read shows.

Both fixes were pinned down offline first, replaying captures through the C ABI with no
phone involved, and both turned out to be one frame each. That is the route paying for
itself twice over: the app's own symptom for either bug was a switch that moved and a
device that did not, which says nothing about which of them it was.

### Confirmed on hardware, 2026-09-11

The cover's ambient sound control works, against a **WF-C700N** — the first device
other than the LinkBuds Clip these hands have driven. It advertises no listening
modes at all, which is what left the cover with no actions whatsoever while both of
its lists were gated on those, and the noise action steps it Off, noise cancelling,
ambient sound from the cover.

The LinkBuds Clip was checked in the same session and its listening-mode and distance
actions are unchanged. That is the half worth stating: the lists were restructured
from two to four, so the path that device takes through them is a different one than
before even though nothing about what it offers moved.

Known gaps:
- Leaving `DevicePage` still drops the RFCOMM channel on purpose, since the
  headset allows one control session at a time. Coming back to it is what the
  reconnect covers (see Losing the headset, and getting it back).
- LinkBuds Clip is open-ear and, as the desktop client confirmed on hardware,
  reports no NC/ASM function of any kind — the ambient sound control section
  simply will not appear. It offers background music, voice boost and sound
  leakage reduction, but no cinema.
- V1 (XM4 and older) has now been driven, by someone else: a WH-1000XM4 in pull
  request #2, which found two faults in the V1 path and one in this app. The
  protocol pair is vendored in (see [libmdr/UPSTREAM.md](libmdr/UPSTREAM.md),
  `5a4a393` and `cb3d915`); no V1 device has been in these hands, so the UUID
  fallback and everything above it is confirmed by report rather than by us.
- Touch controls and speak-to-chat are reachable through the C ABI already; only
  the UI is missing. So is the half of the general settings the ABI cannot carry:
  the list-shaped ones, of which the LinkBuds Clip's tap sensitivity is one.
