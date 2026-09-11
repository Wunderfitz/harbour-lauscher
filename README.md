# Lauscher

Control Sony headphones from Sailfish OS: battery levels, device information, the
ambient sound control and listening modes, the equalizer, playback and the headset's
own settings. It speaks Sony's MDR protocol over Bluetooth RFCOMM, using `libmdr` from
[SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient) as the protocol
implementation and BlueZ's `Profile1` D-Bus API as the transport.

It is a control channel, not an audio one: music keeps playing over the usual route
while Lauscher is connected. Pair the headset in the Bluetooth settings first — Lauscher
lists what is already paired and connects on tap.

Lauscher is an independent project, not affiliated with, endorsed or certified by Sony.

## What it does

Everything below is implemented and has been driven on real hardware. What a headset
actually shows depends on what it advertises — two Sony models rarely offer the same
set of features, and a section that is missing is one the device did not offer.

- **Connect.** Lists the paired devices that speak MDR, and only those; phones,
  speakers and car kits never appear. If exactly one supported headset is already
  connected when the app starts, it opens straight away.
- **Reconnect by itself.** Buds going into their case take the control channel with
  them, which is not a fault. The app waits, says what it is waiting for, and picks the
  session up when the headset comes back.
- **Battery and identity.** Per-side battery levels, model name, firmware, serial and
  the codec in use.
- **Playback.** Track names, play/pause, next and previous, and volume as a percentage.
- **Ambient sound control.** Off, noise cancelling, ambient sound with its level, and
  focus on voice.
- **Listening modes.** Standard, background music with its distance, cinema, voice
  boost and sound leakage reduction — whichever of them the device advertises.
- **Equalizer.** The presets the headset itself reports, the band steps as a strip of
  faders (five bands or ten, whichever the device has), and clear bass where it exists.
- **DSEE.** Sony's upscaling, under its own name — DSEE, DSEE HX, DSEE Ultimate — as
  the device reports it.
- **Connected devices (multipoint).** Which source devices the headset holds, which of
  them has playback, moving playback between them, and whether the headset may move it
  by itself.
- **The headset's own settings.** The booleans the device defines, multipoint among
  them, and what its Bluetooth link is tuned for: sound quality or connection
  stability.
- **Cover page.** Battery, volume and the active mode at a glance, with cover actions
  for the listening mode, the background music distance and the ambient sound control —
  whichever two of those the device has.

English and German.

## Devices

No Sony model is on a list of supported hardware — the app asks the headset what it can
do and shows that. These are the ones it has actually run against:

| Device | Protocol | What was confirmed |
|---|---|---|
| **LinkBuds Clip (WF-LC900)**, firmware 2.0.3 | V2 | The development device. Every feature the app offers has been exercised on it, including the equalizer bands, DSEE, multipoint and the connection quality. Being open-ear, it advertises no ambient sound control at all, so that section does not appear. |
| **WF-C700N** | V2 | Connects, and the ambient sound control works from the page and the cover. It advertises no listening modes, which is a case worth naming: the UI adapts to that rather than coming up empty. |
| **WH-1000XM4** | V1 | Reported working by [JimKnopfIoT](https://github.com/Wunderfitz/harbour-lauscher/pull/2), who found and diagnosed four faults in the older protocol path that are now fixed. No V1 device has been in the author's hands. |

Newer models — WF/WH-1000XM5 and up, the LinkBuds series — speak the same V2 protocol
as the LinkBuds Clip; the XM4 generation and older speak V1. Both are built in.

If a headset of yours works, or does not, a report is welcome — see below.

## Installing

There is no Jolla Store package and there will not be one: the app cannot work without
bluetoothd, and declaring that dependency puts it outside harbour's allowed set (`sfdk
check` fails its Requires suite by design). Install the RPM by hand instead, with
`pkcon install-local` or the file manager, after allowing untrusted software in the
Sailfish settings.

Every push builds an RPM for aarch64, armv7hl and i486 and attaches them to the run
under [Actions](https://github.com/Wunderfitz/harbour-lauscher/actions) — that is the
quickest way to get a fix that is not in a release yet. Otherwise build it yourself,
below.

Sailfish OS **5.1.0.11** or newer is required. That is not a preference: `libmdr` needs
C++20, which arrived in the Sailfish toolchain with this release's GCC 13.4. Running on
a newer OS than the one it was built against is fine and is what the author does
(5.2.0.17 on a Jolla Phone 2026).

## Good to know

- **Only one app can hold the control channel.** Leaving the device page closes it on
  purpose, so Sony's own app — or another phone — can take over right away.
- **A battery level of 0 % is shown dimmed.** The headset reports the same thing for an
  earbud sitting in its case and for an empty one, and says nothing that would tell the
  two apart, so the app declines to guess.
- **Empty track names are normal.** They come from the phone over AVRCP, not from the
  headset, so a silent Now-playing section usually means the media player is not
  publishing metadata.
- **No MAC addresses are shown anywhere.** Addresses are used as identifiers and
  nothing more; a device that never sent a name shows as "Unnamed device".
- **Changing multipoint or the connection quality drops the Bluetooth links** for a
  moment. The headset asks before it applies either, the app answers yes, and the page
  says so before you tap.
- **A headset missing from the picker** means bluetoothd holds no MDR service record for
  it. Reconnecting it once in the Bluetooth settings re-runs the lookup, which is what
  the hint on the empty picker says.

## Reporting a problem

Open an [issue](https://github.com/Wunderfitz/harbour-lauscher/issues) with the headset
model and its firmware version. `journalctl -f` while reproducing carries the app's own
warnings, including the reason BlueZ refused a connection. For anything that looks like
the headset behaving unexpectedly rather than the app misbehaving, the per-frame
protocol log is the thing to capture — it is off by default because it is loud and
carries Bluetooth addresses and media metadata; [CLAUDE.md](CLAUDE.md) has the one-line
change that turns it on.

## Building

Out of tree, the way Qt Creator does it: give `sfdk` the path to the sources and it
shadow-builds into the current directory.

```sh
mkdir -p ../build-harbour-lauscher-aarch64 && cd ../build-harbour-lauscher-aarch64
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build ../harbour-lauscher
```

The RPMs end up in `RPMS/` inside that build directory. One build directory per target
means switching targets needs no cleaning, and the source tree stays clean. Opening
`harbour-lauscher.pro` in Qt Creator gives the same arrangement. See
[CLAUDE.md](CLAUDE.md) for the details that cost debugging time.

At run time the package needs `bluez5` (bluetoothd is the transport, over D-Bus),
`sailjail-permissions` (the desktop file asks for the `Bluetooth` permission),
`sailfishsilica-qt5` and the QtQuick 2 import plugin; the Qt libraries it links are
resolved automatically.

## Layout

```
harbour-lauscher.pro   TEMPLATE=subdirs, ordered: libmdr then app
libmdr/                static build of the MDR protocol library
  upstream/            vendored SonyHeadphonesClient/libmdr - see libmdr/UPSTREAM.md
  3rdparty/fmt/        vendored fmt 12.1.0, the subset mdr::Format needs
app/                   the Sailfish app: C++ backend plus QML UI
rpm/                   package spec and changelog
CLAUDE.md              protocol and platform notes; read before touching either
```

The two qmake subprojects are deliberate: `libmdr` needs C++20, while Sailfish's Qt 5.6
headers are only safe up to C++17. They meet at libmdr's pure C ABI, so no C++ ABI
crosses the boundary.

Protocol bugs are fixed in the
[SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient) checkout and the
vendored copy refreshed from it, rather than patched here;
[libmdr/UPSTREAM.md](libmdr/UPSTREAM.md) records which commit the copy is pinned to and
what has gone upstream.

## Not there yet

- Touch controls and speak-to-chat are reachable through the protocol library already;
  only the UI is missing.
- The list-shaped device settings — the LinkBuds Clip's tap sensitivity is one — are
  not carried by libmdr's C ABI, so only the boolean ones can be shown.

## Licence

Lauscher is GPLv3 or later - see [LICENSE](LICENSE) for the licence text, and the
header on each source file for the grant.

The vendored code keeps its own terms: `libmdr/upstream/` is MIT, from
SonyHeadphonesClient (see [libmdr/upstream/LICENSE](libmdr/upstream/LICENSE)), and
`libmdr/3rdparty/fmt/` is under fmt's licence. Both are permissive and GPL-compatible,
which is what makes linking them into a GPLv3 binary fine.
