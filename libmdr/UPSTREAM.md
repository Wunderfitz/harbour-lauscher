# Where `upstream/` comes from

`upstream/` is a copy of the `libmdr/` directory of
[SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient), taken at a
state this app is known to work against:

| | |
|---|---|
| Repository | `https://github.com/mos9527/SonyHeadphonesClient` |
| Base | `ccc1d949e8d4b60c90f600f8077ef0c3285367b6` (branch `v1-compat`) |
| Plus | the WF-LC900 protocol work, branch `fix/mdr-transmit-sequence-numbers`, tip `f5f29c9666a3a9ee50930400a1c0c08cf2871949` |
| Taken on | 2026-08-30 |

Those extra commits are **not upstream yet**. Four of them are load-bearing for the
LinkBuds Clip: the transmit sequence number fix, the pre-handshake frame guard, gating
V2 init requests on the advertised function list, and the four listening modes. A
vanilla `v1-compat` checkout will not drive this device correctly.

The copy is verbatim - `diff -r` against a checkout's `libmdr/` shows no differences -
except for two files added here from that repository's root:

- `upstream/LICENSE` - the project's MIT licence, which covers this code.
- `upstream/AGENTS.md` - the payload struct conventions, referenced throughout
  [../CLAUDE.md](../CLAUDE.md).

## Refreshing it

```sh
cd <SonyHeadphonesClient checkout>
git ls-files libmdr | while read -r f; do
  install -D "$f" "<this repo>/libmdr/upstream/${f#libmdr/}"
done
diff -r <SonyHeadphonesClient checkout>/libmdr <this repo>/libmdr/upstream   # only the two extras
```

Then update the table above, and check `SOURCES` in [libmdr.pro](libmdr.pro): the build
lists translation units explicitly, so a new `.cpp` upstream has to be added by hand.
Generated sources (`upstream/src/Generated/`) are checked in upstream, so the LLVM-based
codegen never has to run.

The build recipe is ours, not upstream's: upstream builds libmdr with CMake and fetches
fmt over the network, neither of which works inside the Sailfish SDK.
