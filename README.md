# Lauscher

Control Sony headphones from Sailfish OS: battery levels, device information,
listening modes, and playback. It speaks Sony's MDR protocol over Bluetooth RFCOMM,
using `libmdr` from
[SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient) as the protocol
implementation and BlueZ's `Profile1` D-Bus API as the transport.

Status: **proof of concept**, developed against a **Sony LinkBuds Clip (WF-LC900)**,
firmware 2.0.3, on Sailfish OS 5.1.0.11. Confirmed on that device: connecting, battery
and device info, playback control, and all four listening modes. Other Sony models
should work to the extent they advertise the same functions, but none has been tried.

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
resolved automatically. Declaring `bluez5` puts the package outside harbour's allowed
dependencies, so `sfdk check` fails its Requires suite by design — this is meant to be
installed by hand, not through the Jolla Store. See
[CLAUDE.md](CLAUDE.md) for the reasoning.

## Layout

```
harbour-lauscher.pro   TEMPLATE=subdirs, ordered: libmdr then app
libmdr/                static build of the MDR protocol library
  upstream/            vendored SonyHeadphonesClient/libmdr - see libmdr/UPSTREAM.md
  3rdparty/fmt/        vendored fmt 12.1.0, the subset mdr::Format needs
app/                   the Sailfish app: C++ backend plus QML UI
rpm/                   package spec
CLAUDE.md              protocol and platform notes; read before touching either
```

The two qmake subprojects are deliberate: `libmdr` needs C++20, while Sailfish's Qt 5.6
headers are only safe up to C++17. They meet at libmdr's pure C ABI, so no C++ ABI
crosses the boundary.

## Licence

`libmdr/upstream/` is MIT, from SonyHeadphonesClient - see
[libmdr/upstream/LICENSE](libmdr/upstream/LICENSE). `libmdr/3rdparty/fmt/` is fmt's own
licence. The RPM spec declares the app itself MIT; a licence file for it still needs to
be added.
