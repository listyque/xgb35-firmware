# Credits and third-party notices

**StarDroid was put together by ToadWarrior.**

## Remastered data and video readers

- **CascLib**, by Ladislav Zezula and contributors, reads the player's local installation. The MIT license and pinned source revision are retained in `third_party/casclib/`. Its network entry point is disabled in StarDroid. The APK carries `licenses/CascLib-MIT.txt`.
- **libvpx 1.15.2**, by the WebM Project authors, decodes imported VP8/VP9 video. The pinned source archive is retained under `vendor/`; `scripts/build_vpx.py` records its hash and build options. BSD license and patent notices are included in the APK as `licenses/libvpx-BSD.txt` and `licenses/libvpx-PATENTS.txt`.

These libraries contain no StarCraft artwork. Graphics come from the player's own installation.

StarDroid is an unofficial Android adaptation. StarCraft and Brood War are Blizzard Entertainment properties. The player supplies original game archives; Blizzard game data is not distributed with this repository or APK.

| Component | Origin and attribution | Notice |
| --- | --- | --- |
| OpenBW engine and web-host ancestry | [OpenBW](https://github.com/OpenBW/openbw), [heiner/openbw](https://github.com/heiner/openbw), and their contributors | Pinned commit `469d2037e2fd03085dac4729912f4188bc2a778b`; provenance and archive changes in [UPSTREAM.json](UPSTREAM.json). |
| libsmacker 1.2.0 | Greg Kennedy and contributors; pinned [JonnyH/libsmacker](https://github.com/JonnyH/libsmacker) commit `ae8d4c9ec07b24d43ccff184d6e512bae793dfd1` | LGPL 2.1 or later. Full license in [vendor/libsmacker/COPYING](vendor/libsmacker/COPYING). Source is included and the Android decoder is a separate shared library. |
| PyMS listfiles and format references | Zach Zahos / poiuyqwert, [PyMS](https://github.com/poiuyqwert/PyMS) | [MIT license](app/src/main/assets/missions/PyMS-LICENSE.txt) is preserved with the mission metadata. |
| Weave map | Alexandre Coderre-Chabot, [acodcha/sc](https://github.com/acodcha/sc) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/); map attribution in [ATTRIBUTION.md](app/src/main/assets/game/maps/ATTRIBUTION.md). The native copy is the same map. |
| Asio in the upstream archive | Asio authors and contributors | Boost Software License 1.0; `deps/asio/LICENSE_1_0.txt` is retained inside the vendored source archive. |
| JSON-java test dependency | [stleary/JSON-java](https://github.com/stleary/JSON-java), version 20240303 | Used only by development test harnesses, not bundled as an APK dependency. Upstream source and licensing are maintained in the linked JSON-java repository. |
| EUD requirement baseline | EUD Editor, LeeJungHun; StarDraft authors, pinned commit `c3609230453b33349e8081f8383c3caa8d0d6e45` | MIT. Exact 5,392-byte requirement metadata extraction, hashes, provenance and full notices in [docs/EUD_REQUIREMENTS95.md](docs/EUD_REQUIREMENTS95.md). No executable or game archive is included. |

The pinned OpenBW archive does not contain a top-level engine license file. This repository does not assign a new license to that upstream code or claim that one blanket license covers every component. Existing notices and their scope are preserved. Upstream's license discussion is tracked in [OpenBW/openbw issue 17](https://github.com/OpenBW/openbw/issues/17).

See [REFERENCES.md](REFERENCES.md) for historical implementation references. References are not included game assets. See [docs/BUILDING.md](docs/BUILDING.md) for rebuilding the native app and replacing the decoder library.

### Mark Adler puff (DEFLATE decoder)

`native-engine/puff100.h` adapts puff2.3 from zlib1.3.1 under its zlib license. The decoder is header-only, namespaced, and builds fixed Huffman tables per call for thread safety. Its copyright and full license are preserved in the header and `app/src/main/assets/native/puff-license.txt`. Source URLs and upstream hashes are recorded in `docs/MPQ_ZLIB100.md`.

### eudplib compact-table test reference

The MIT-licensed producer serializer from [armoha/eudplib](https://github.com/armoha/eudplib), pinned at `979f2fb4e8ffa4ec62e5872bddc4c0b36ae006cf`, is retained with its license under `tests/fixtures/eudplib101/`. The regression generator uses its audited serialization method to exercise real compact jump-table layouts. This is a development test reference and is not an APK runtime dependency. Details and source identities are in `docs/EUD_GITHUB_TOOLS101.md`.

## GSM 06.10 decoder (test102)

The WAV codec49 decoder embeds libgsm1.0.24 by Jutta Degener and Carsten
Bormann (Technische Universitaet Berlin). Its permissive notice is retained in
`vendor/libgsm/COPYRIGHT` and the APK asset `licenses/libgsm.txt`. The exact
source archive hash, unchanged files and two local linkage-header changes are
recorded in `vendor/libgsm/UPSTREAM.json`. No encoder or command-line tool is
exposed by the app.

## UnRAR extraction source

RAR imports use Alexander Roshal's UnRAR **7.30 beta 2**, distributed as `unrarsrc-7.3.2.tar.gz`. This is an extraction-only dependency; no RAR encoder is included. The complete license is in [third_party/unrar/LICENSE.txt](third_party/unrar/LICENSE.txt), with upstream notices in [third_party/unrar/ACKNOWLEDGMENTS.txt](third_party/unrar/ACKNOWLEDGMENTS.txt). The APK includes both as `assets/licenses/UnRAR.txt` and `assets/licenses/UnRAR-ACKNOWLEDGMENTS.txt`. It is not MIT-licensed. The source pin is in `third_party/unrar/BUILD.json`, and the bounded import modifications are in `patches/native-rar-bounded128.patch`. The archive reader is built as its own shared library.
