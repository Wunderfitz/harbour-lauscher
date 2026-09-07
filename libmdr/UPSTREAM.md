# Where `upstream/` comes from

`upstream/` is a copy of the `libmdr/` directory of
[SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient), taken at a
state this app is known to work against:

| | |
|---|---|
| Repository | `https://github.com/mos9527/SonyHeadphonesClient` |
| Base | `965c458d` (branch `v1-compat`) |
| Plus | the WF-LC900 protocol work, branch `mdr-v2-session-correctness-and-listening-modes` on `https://github.com/Wunderfitz/SonyHeadphonesClient`, tip `f852611` |
| Taken on | 2026-09-07 |

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
and DSEE changes, which nothing here has a UI for yet. A vanilla `v1-compat` checkout
will not drive this device correctly.

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
