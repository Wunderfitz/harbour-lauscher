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

```sh
sfdk -c target=SailfishOS-5.1.0.11-aarch64 -c no-fix-version build
```

`-c no-fix-version` keeps the package at the spec's version instead of one derived from
git tags. `sfdk build` builds in-tree, so generated `Makefile`s and `.o` files land next
to the sources; delete them when switching targets. See
[CLAUDE.md](CLAUDE.md) for the details that cost debugging time.

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
