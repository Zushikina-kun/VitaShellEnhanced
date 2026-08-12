# VitaShell Enhanced

> A modernization fork of [VitaShell](https://github.com/TheOfficialFloW/VitaShell) by [TheOfficialFloW](https://github.com/TheOfficialFloW).  
> Focus: QR scanner overhaul, bug fixes, and quality-of-life improvements — with strict PS Vita hardware compatibility.

---

## Attribution & License

This project is a fork of **VitaShell 2.02** by TheOfficialFloW.  
Original repository: https://github.com/TheOfficialFloW/VitaShell  
Original license: GNU General Public License v3.0 — see [LICENSE](LICENSE)

All modifications in this fork are also released under GPLv3.  
This fork does **not** claim original authorship of VitaShell or any of its subsystems.

---

## What is VitaShell?

VitaShell is an alternative replacement for the PS Vita's LiveArea shell.  
It provides a file manager, package installer, FTP server, USB mass storage, archive browser, text/hex editor, image viewer, audio player, and QR code scanner — all running as a single homebrew application under HENkaku.

VitaShell won first prize in the Revitalize PS Vita homebrew competition.  
HENkaku's molecularShell is also based on VitaShell.

---

## What this fork adds

### QR Scanner — Complete Overhaul

The QR scanner was the weakest part of the original VitaShell.  This fork rewrites it entirely while keeping full compatibility with the rest of the application.

**Bugs fixed in the original scanner:**

| Bug | Impact |
|---|---|
| Wrong thread function signature (`qr_thread()` missing SDK params) | Undefined behaviour on ARMv7, potential crash |
| Race condition: shared `uint32_t*` pointer between render + decode threads | Data corruption, intermittent decode failures |
| NULL dereference in `stopQR()` before first `renderCameraQR()` call | Hard crash on first use |
| `finishQR()` called `sceKernelDeleteThread` on a running thread | Undefined behaviour, resource leak |
| Spin-loop delay of 10 µs | Second CPU core ~100% occupied while idle |
| `MAX_URL_LENGTH` was 128 bytes | Long GitHub/CDN URLs silently truncated |
| Two separate HTTP GET requests per QR scan | Broke with pre-signed/CDN URLs; doubled latency |

**New capabilities:**

- **3-stage preprocessing pipeline** — escalates from fast grayscale → contrast stretch → adaptive threshold based on how many frames have failed. Better scan rate in dim, uneven, or low-contrast conditions.
- **Multi-frame sharpness selection** — 3-frame pool; decode thread always picks the sharpest buffered frame (highest variance). Helps with hand tremor and the Vita camera's variable frame quality.
- **BT.601 perceptual luma** — replaces the original flat RGB average with the correct weighted conversion, improving grayscale contrast.
- **Low-variance frame rejection** — skips dark or featureless frames before wasting decode time on them.
- **Visual scan overlay** — white corner brackets show the user exactly where to aim. Brackets flash green on successful decode.
- **Combined network probe** — `getDownloadInfo()` replaces two separate HTTP requests with one, fixing CDN/GitHub release URL handling.
- **URL length raised to 1024 bytes** — covers all real-world homebrew distribution URLs.
- **Full error handling** — every allocation and API call in `initQR()` has a cleanup path.

For full technical details see [QR.md](QR.md).

### Other fixes

- `uncommon_dialog.c`: added a clear comment guarding the single `renderCameraQR()` call per frame (previously ambiguous from search tooling).
- `language.h/c/txt`: added `QR_NO_CAMERA` and `QR_URL_TOO_LONG` strings for proper user-facing error messages.

---

## Compatibility

| Target | Status |
|---|---|
| PS Vita 1000 (OLED, PCH-1000) | ✅ Primary target |
| PS Vita 2000 (LCD, PCH-2000) | ✅ Compatible |
| PS TV (VTE-1000) | ✅ Compatible (no front camera — QR scanner disabled gracefully) |

**Requirements:**
- HENkaku or Ensō (any version supporting VitaShell 2.x)
- ux0: accessible (Memory Card, SD2Vita, or USB via YAMT/StorageMgr)

**This fork does not modify:**
- taiHEN configuration
- Boot configuration
- Storage drivers (YAMT, StorageMgr, sd2vita)
- Kernel plugin loading behaviour
- Any system partition

All changes are isolated to user-space application code.

---

## Building

See [BUILD.md](BUILD.md) for full instructions.

Quick start (requires VitaSDK):

```sh
git clone https://github.com/Zushikina-kun/VitaShellEnhanced.git
cd VitaShellEnhanced
mkdir build && cd build
cmake .. && make
```

Output: `VitaShell.vpk` in the build directory.

---

## Installing

1. Copy `VitaShell.vpk` to your Vita (USB, FTP, or SD card).
2. Install with molecularShell, VitaShell itself, or any VPK installer.
3. Launch from LiveArea.

---

## Using the QR Scanner

1. Open VitaShell and navigate to any directory.
2. Press **SELECT** (or use the context menu) to open the QR scanner.
3. Hold the Vita ~15–30 cm from the QR code with the **front camera** facing the code.
4. White corner brackets show the target area — aim the QR code inside them.
5. On detection, the brackets flash **green** and VitaShell processes the result:
   - **URL ending in `.vpk`** → offers to download and install
   - **URL ending in other file types** → offers to download
   - **URL with no file extension** → offers to open in browser
   - **Non-URL content** → displays the decoded text

**Tips for better scan rate:**
- Good, even lighting. Avoid backlighting the QR code.
- Fill the bracket area — don't hold the Vita too far away.
- Keep the Vita steady for 1–2 seconds if it doesn't scan immediately.
- The scanner automatically escalates preprocessing after a few failed frames — give it a moment before moving.

---

## Customization (inherited from original VitaShell)

Place custom theme files at `ux0:VitaShell/theme/YOUR_THEME_NAME/`.  
Set the active theme in `ux0:VitaShell/theme/theme.txt`:

```
THEME_NAME = "YOUR_THEME_NAME"
```

| File | Purpose |
|---|---|
| `colors.txt` | All UI colours |
| `archive_icon.png` | Archive file icon |
| `audio_icon.png` | Audio file icon |
| `battery.png` | Battery border |
| `battery_bar_charge.png` | Charging bar |
| `battery_bar_green.png` | Green battery bar |
| `battery_bar_red.png` | Red battery bar |
| `bg_audioplayer.png` | Audio player background |
| `bg_browser.png` | File browser background |
| `bg_hexeditor.png` | Hex editor background |
| `bg_photoviewer.png` | Photo viewer background |
| `bg_texteditor.png` | Text editor background |
| `context.png` | Context menu image |
| `context_more.png` | Context menu "more" image |
| `cover.png` | Default album cover |
| `dialog.png` | Dialog box image |
| `file_icon.png` | Generic file icon |
| `folder_icon.png` | Folder icon |
| `ftp.png` | FTP icon |
| `image_icon.png` | Image file icon |
| `pause.png` | Pause icon |
| `play.png` | Play icon |
| `settings.png` | Settings icon |
| `sfo_icon.png` | SFO file icon |
| `text_icon.png` | Text file icon |

---

## Multi-language (inherited from original VitaShell)

Place language files at `ux0:VitaShell/language/LANG.txt` (UTF-8 encoded).  
VitaShell automatically loads the file matching the system language.

Supported language filenames: `japanese`, `english_us`, `french`, `spanish`,
`german`, `italian`, `dutch`, `portuguese`, `russian`, `korean`, `chinese_t`,
`chinese_s`, `finnish`, `swedish`, `danish`, `norwegian`, `polish`,
`portuguese_br`, `turkish`.

Language files are in the [`l10n/`](l10n/) folder.

**New strings added in this fork** (add to custom language files if needed):

```
QR_NO_CAMERA    = "Camera unavailable.\QR scanning requires the front camera."
QR_URL_TOO_LONG = "The scanned URL is too long to process."
```

---

## Documentation

| File | Contents |
|---|---|
| [README.md](README.md) | This file — overview, usage, compatibility |
| [QR.md](QR.md) | QR scanner technical deep-dive |
| [BUILD.md](BUILD.md) | Build instructions for VitaSDK |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Code structure and subsystem map |
| [CHANGELOG.md](CHANGELOG.md) | All changes vs. original VitaShell |

---

## Credits

**Original VitaShell** (all core functionality):
- [TheOfficialFloW](https://github.com/TheOfficialFloW) — VitaShell author

**Original VitaShell credits:**
- Team Molecule — HENkaku
- xerpi — ftpvitalib and vita2dlib
- wololo — Revitalize contest
- sakya — Lightmp3
- Everyone who contributed to vitasdk

**VitaShell Enhanced (this fork):**
- [Zushikina-kun](https://github.com/Zushikina-kun) — QR scanner overhaul, bug fixes, modernization
