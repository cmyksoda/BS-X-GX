# BS-X GX

*A Satellaview station receiver for the Wii.*

A way to experience the charm of the Satellaview in 2026!

BS-X GX boots your Wii straight into the **BS-X** town, the Satellaview cartridge shell from 1995, and automatically tunes it to a broadcast station over the network. Whatever the station is airing at that moment will show up in the town's download building. Walk in, download it into your (emulated, persistent) 8M memory pack, and play it! Just how the Satellaview used to work.  

This is a heavily trimmed fork of [Snes9x GX](https://github.com/dborth/snes9xgx) (Tantric, Zopenko, Askot), which is a port of [Snes9x](https://github.com/snes9xgit/snes9x).  
The satellite data format is [SatellaWave](https://github.com/LuigiBlood/sat_wave)'s (LuigiBlood).  
The station server lives in a separate repository, **bsx-station**.

> Status: early development. Nothing here is released yet. The whole loop (tune in, download, play) works in
> Dolphin; real-hardware testing is next.

## What you need

- A Wii (or Wii U in vWii mode) with the Homebrew Channel. An SD card or USB drive.
- **`BS-X.bin`** — the 1 MB Satellaview BS-X ROM, English + No-DRM version, from the [BS-X Project](https://project.satellaview.org/downloads.htm). **Not included** — put it at `sd:/bsxgx/BS-X.bin` (or `usb:/bsxgx/BS-X.bin`).
- A station to tune to: Home → Settings → Network → **Station Address**, as `host` or `host:port`. Without one,
  the town still works offline, like a Satellaview with no dish.

## Install

```
sd:/apps/bsxgx/boot.dol      ← the app (from the release zip; releases are hand-packed)
sd:/apps/bsxgx/meta.xml
sd:/apps/bsxgx/icon.png
sd:/bsxgx/BS-X.bin           ← you supply this
sd:/bsxgx/saves/             ← created automatically: BS-X.srm, BS-X.mempack, BS-X.psram
```

## Building

The fork targets **libogc2**; the easiest way to build is the libogc2 container:

```
podman run --rm -v "$PWD":/src:Z -w /src ghcr.io/extremscorner/libogc2:latest make -f Makefile.wii -j$(nproc)
```

Output: `executables/bsx-gx-wii.dol`.

## Credits

- **[Snes9x GX](https://github.com/dborth/snes9xgx)** by Tantric, Zopenko, Askot, michniewski, InfiniteBlue and contributors, the front-end this is forked from; its upstream README is preserved as `docs-upstream-snes9xgx-README.md`
- **[Snes9x](https://github.com/snes9xgit/snes9x)** by the Snes9x Team, the emulator core
- **libogc2** and **devkitPPC** by Extrems, shagkur, and WinterMute
- **[SatellaWave](https://github.com/LuigiBlood/sat_wave)** and the Satellaview research it rests on, by LuigiBlood
- **BS-X translation and No-DRM patch** by the [BS-X Project](https://project.satellaview.org/downloads.htm)
- The menu artwork by the3seashells and the menu sounds by Peter de Man, from Snes9x GX
- **FreeTypeGX** by Armin Tamzarian, for drawing the menu text
- **wiidrc** by FIX94, for Wii U GamePad support
- **oggplayer** by Francisco Muñoz "Hermes", for the menu music
- **PNGU** by frontier, for loading PNG images
- The menu font, **Horatio D** by URW, and the optional language fonts: **Systema** by MASUDA mitiya (Konatu), the M+ Project and Systema81 for Japanese, **LexiGulim** by Lexitech for Korean, and **FZLanTingHei** by Beijing Founder Electronics for Chinese

## License

The front-end (`source/` except `source/snes9x/`) is free software under the [GNU General Public License, version 2](LICENSE). Some parts use other licenses: the emulator core (`source/snes9x/`) is under the Snes9x license (non-commercial), see [`source/snes9x/LICENSE`](source/snes9x/LICENSE); FreeTypeGX is LGPL version 3 or later, wiidrc is MIT, oggplayer has its own BSD-style license, and the fonts keep their makers' terms. Each of these files carries its own notice.

---

*This project was made with AI assistance. For more information, see [my AI usage statement](https://github.com/cmyksoda/cmyksoda/blob/main/AI_USAGE.md).*
