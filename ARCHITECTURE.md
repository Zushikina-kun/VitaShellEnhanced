# VitaShell Enhanced — Architecture

This document describes the overall code structure of VitaShell Enhanced,
with emphasis on the QR scanner subsystem which is the primary area of change
in this fork.  Understanding this document is recommended before modifying
any QR-related code.

---

## Repository layout

```
VitaShellEnhanced/
│
├── main.c / main.h          — Entry point, main loop, dialog step state machine
├── main_context.c           — Context menu definitions and callbacks
├── init.c / init.h          — Startup/shutdown, module loading, default file install
│
├── qr.c / qr.h              — QR scanner (completely overhauled in this fork)
├── network_download.c/h     — HTTP download functions (extended in this fork)
│
├── browser.c/h              — File browser UI
├── context_menu.c/h         — Context menu rendering
├── file.c/h                 — File type detection, path utilities
├── io_process.c/h           — Async file I/O (copy, move, delete, install)
├── package_installer.c/h    — VPK installation
│
├── photo.c/h                — Image viewer
├── text.c/h                 — Text viewer/editor
├── hex.c/h                  — Hex editor
│
├── archive.c/h              — Archive browsing (uses libarchive)
├── psarc.c/h                — PSARC format support
├── makezip.c/h              — ZIP creation
├── minizip/                 — minizip library
│
├── audioplayer.c/h          — Audio player
├── audio/                   — Audio backend (vita_audio, mp3, ogg, id3, lrc)
├── libmad/                  — MP3 decoder library
│
├── uncommon_dialog.c/h      — Custom dialog system (replaces SceCommonDialog)
├── message_dialog.c/h       — High-level dialog wrapper
├── ime_dialog.c/h           — On-screen keyboard dialog
├── netcheck_dialog.c/h      — Network check dialog
├── adhoc_dialog.c/h         — Ad-hoc transfer dialog
├── property_dialog.c/h      — File properties dialog
│
├── network_update.c/h       — Self-update network code
├── usb.c/h                  — USB mass storage
├── pfs.c/h                  — PFS (encrypted game partition) mounting
├── refresh.c/h              — LiveArea / license database refresh
├── settings.c/h             — Settings screen
├── theme.c/h                — Theme loading
├── language.c/h             — Localisation
├── config.c/h               — Config file parser (key=value format)
│
├── utils.c/h                — Common utilities (pad input, drawing helpers, etc.)
├── bm.c/h                   — Boyer-Moore string search
├── strnatcmp.c/h            — Natural sort comparison
├── sha1.c/h                 — SHA-1 implementation
├── sfo.c/h                  — SFO file reader
├── rif.c/h                  — RIF license file handling
├── elf.c/h                  — ELF/SELF introspection
├── coredump.c/h             — Crash coredump display
├── sqlite3.c/h              — SQLite (bundled)
│
├── modules/
│   ├── kernel/              — Kernel module (.skprx)
│   ├── user/                — User module (.suprx)
│   ├── patch/               — Patch module (.skprx)
│   └── usbdevice/           — USB device module (.skprx)
│
├── resources/               — Embedded resources (themes, language files, modules)
│   ├── default/             — Default theme assets
│   ├── electron/            — Electron theme assets
│   ├── english_us.txt       — Default English language strings
│   └── *.bin / *.png / *.txt
│
├── l10n/                    — Community language files
├── pkg/                     — VPK packaging metadata (icon, livearea)
├── release/                 — Release output directory
│
├── README.md                — Project overview and usage
├── BUILD.md                 — Build instructions
├── ARCHITECTURE.md          — This file
├── CHANGELOG.md             — Change history
└── QR.md                    — QR scanner technical documentation
```

---

## Core execution model

VitaShell runs a single-threaded main loop at ~30fps with two background threads:

```
Main thread  (SCE priority default)
  │
  ├─ readPad()              — sample controller input
  ├─ drawShellInfo()        — status bar (battery, clock, path)
  ├─ drawFileList()         — file browser
  ├─ drawContextMenu()      — context menu overlay
  ├─ drawUncommonDialog()   — dialog overlay
  └─ vita2d_swap_buffers()  — flip to screen

QR decode thread  (priority 0x40, runs on second ARM core)
  └─ polls qr_frame_ready, runs preprocessing + quirc decode

Power tick thread  (low priority)
  └─ calls scePowerTick() periodically to prevent auto-sleep during operations
```

Long-running operations (file copy, install, download) are launched as
additional short-lived threads via `sceKernelCreateThread` and tracked through
the `DIALOG_STEP_*` state machine in `main.c`.

---

## Dialog step state machine

`main.c` contains a large `switch` on `getDialogStep()` that drives all
asynchronous UI flows.  Each major operation has its own `DIALOG_STEP_*`
enum value defined in `main.h`.

QR-relevant steps:

```
DIALOG_STEP_QR               — scanner is open, waiting for decode
DIALOG_STEP_QR_DONE          — decode succeeded, closing scanner dialog
DIALOG_STEP_QR_WAITING       — qr_scan_thread is running (network probe)
DIALOG_STEP_QR_CONFIRM       — showing download/install confirmation
DIALOG_STEP_QR_DOWNLOADING   — downloading the file
DIALOG_STEP_QR_DOWNLOADED    — download complete (non-VPK)
DIALOG_STEP_QR_DOWNLOADED_VPK — download complete (VPK, triggers install)
DIALOG_STEP_QR_OPEN_WEBSITE  — offering to open URL in browser
DIALOG_STEP_QR_SHOW_CONTENTS — showing decoded non-URL text
DIALOG_STEP_INSTALL_CONFIRMED_QR — installing a QR-downloaded VPK
```

---

## QR subsystem — detailed architecture

### Files

| File | Role |
|---|---|
| `qr.c` | All QR logic: camera, frame processing, decode thread, overlay |
| `qr.h` | Public API + `QR_MAX_URL_LENGTH` constant |
| `uncommon_dialog.c` | Calls `renderCameraQR()` once per frame when scanner is open |
| `main.c` | Handles `DIALOG_STEP_QR*` transitions, spawns `qr_scan_thread` |
| `network_download.c` | Provides `getDownloadInfo()` used by `qr_scan_thread` |

### Data flow

```
sceCameraRead(1, &cam_info_read)
        │
        │  ABGR pixels → vita2d texture (VRAM)
        ▼
vita2d_draw_texture()          — display raw camera feed
        │
qrDrawOverlay()                — draw white/green bracket overlay on top
        │
rgba_to_gray_fast()            — convert ABGR → grayscale (BT.601 luma)
        │                         write into qr_gray_pool[write_idx]
        │
compute_variance()             — measure frame sharpness
        │
qr_frame_ready = 1             — signal decode thread

        ┌──────────────────────────────────┐
        │       Decode thread wakes        │
        │                                  │
        │  pick highest-variance frame     │
        │  from qr_gray_pool[]             │
        │                                  │
        │  Stage 0: raw → quirc_decode()   │
        │      failed N times?             │
        │  Stage 1: contrast → quirc       │
        │      failed N times?             │
        │  Stage 2: adaptive_thresh → quirc│
        │                                  │
        │  success → last_qr[], qr_scanned=1│
        └──────────────────────────────────┘

        ┌──────────────────────────────────┐
        │  main.c detects qr_scanned==1    │
        │  → DIALOG_STEP_QR_DONE           │
        │  → spawns qr_scan_thread         │
        └──────────────────────────────────┘

        ┌──────────────────────────────────┐
        │  qr_scan_thread                  │
        │  getDownloadInfo() → 1 HTTP req  │
        │  show confirmation dialog        │
        │  → downloadFileProcess()         │
        └──────────────────────────────────┘
```

### Thread synchronisation

The render thread and decode thread share the grayscale frame pool using a
single `volatile int qr_frame_ready` flag.

```
Render thread:
  while (!qr_frame_ready) {
      write gray frame
      set variance
      qr_frame_ready = 1      ← store-release (ARM word write)
  }

Decode thread:
  while (!qr_frame_ready) { sleep(5ms); }
  qr_frame_ready = 0          ← load-acquire (ARM word read)
  process frame
```

This is a single-producer / single-consumer pattern.  One `volatile int` flag
is sufficient for correctness on ARMv7 because:
- Word reads/writes are atomic on ARMv7.
- `volatile` prevents compiler reordering across the flag access.
- The hardware memory model on Cortex-A9 is weakly ordered but the
  single-word flag acts as a sufficient barrier for this access pattern.

No mutex is needed.  A mutex would add lock overhead on every render frame
(30 times/second), which is unnecessary for this pattern.

### Preprocessing pipeline

```c
/* Stage 0 — BT.601 grayscale */
rgba_to_gray_fast(src_rgba, gray_dst, CAM_WIDTH * CAM_HEIGHT);
try_decode_gray(qr_ctx, gray_dst, ...);

/* Stage 1 — Contrast stretch */
apply_contrast_boost(gray_dst, scratch, CAM_WIDTH * CAM_HEIGHT);
try_decode_gray(qr_ctx, scratch, ...);

/* Stage 2 — Adaptive threshold */
apply_adaptive_threshold(scratch, scratch, CAM_WIDTH, CAM_HEIGHT, 21, 10);
try_decode_gray(qr_ctx, scratch, ...);
```

Each stage is only entered after the previous one has failed
`QR_FAST_FRAMES` / `QR_CONTRAST_FRAMES` consecutive times.

---

## Dialog system

VitaShell does **not** use the standard `SceCommonDialog` for most dialogs.
Instead it implements its own dialog renderer in `uncommon_dialog.c`, which
intercepts the `sceMsgDialogInit` / `sceMsgDialogGetStatus` / `sceMsgDialogTerm`
calls with its own implementations.

This allows the QR scanner camera feed to be embedded directly inside a
dialog box — which `SceCommonDialog` would not support.

The custom dialog modes are:

| Mode | Constant | Used for |
|---|---|---|
| Standard message | `SCE_MSG_DIALOG_MODE_USER_MSG` | Yes/No, OK dialogs |
| Progress bar | `SCE_MSG_DIALOG_MODE_PROGRESS_BAR` | Download/install progress |
| QR scan | `MSG_DIALOG_MODE_QR_SCAN` (= 10) | Camera feed + scan message |

---

## Network download

`network_download.c` provides four functions:

| Function | Description |
|---|---|
| `getDownloadInfo()` | **New in this fork.** Single HTTP GET that returns Content-Length + Content-Disposition. Used by `qr_scan_thread`. |
| `getDownloadFileSize()` | Original — HTTP GET for Content-Length only. Still used by `downloadFileProcess()`. |
| `getFieldFromHeader()` | Original — HTTP GET for an arbitrary header field. Retained for backward compatibility. |
| `downloadFile()` | Streaming file download with progress callback and cancel support. |
| `downloadFileProcess()` | Full download flow with progress dialog, size check, and error handling. |

All HTTP requests use the VitaSDK `SceHttp` API with SSL verification disabled
(`sceHttpsDisableOption(SCE_HTTPS_FLAG_SERVER_VERIFY)`), which is the same
behaviour as the original VitaShell.

---

## Adding new features

### Adding a new language string

1. Add the enum value to `language.h` in the appropriate section.
2. Add `LANGUAGE_ENTRY(NAME)` to the `language_entries[]` array in `language.c`.
3. Add `NAME = "string"` to `resources/english_us.txt`.
4. Optionally add the string to `l10n/*.txt` files for other languages.

### Adding a new dialog step

1. Add `DIALOG_STEP_YOUR_STEP` to the `DialogSteps` enum in `main.h`.
2. Add a `case DIALOG_STEP_YOUR_STEP:` block in the main switch in `main.c`.
3. Use `setDialogStep(DIALOG_STEP_YOUR_STEP)` to transition into it.

### Adding a new context menu entry

1. Add an enum value to the relevant `MenuXxxEntrys` enum in `main_context.c`.
2. Add a `MenuEntry` to the relevant `menu_xxx_entries[]` array.
3. Handle the new `case` in the relevant `contextMenuXxxEnterCallback()`.

---

## What NOT to modify

The following are explicitly out of scope for this fork and must not be changed:

- `modules/kernel/` — kernel module code
- `init.c` taiHEN module loading paths
- Storage mount/unmount logic (`pfs.c`, `usb.c`, `init.c`)
- Boot/system configuration files
- Anything that writes to `ur0:tai/`, `ux0:tai/`, or boot partition
