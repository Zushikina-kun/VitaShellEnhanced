# VitaShell Enhanced — Changelog

All notable changes to this fork are documented here.  
This project is a fork of [VitaShell](https://github.com/TheOfficialFloW/VitaShell) by TheOfficialFloW.

Format: `## [version] — date`

---

## [1.0.0-enhanced] — 2026-08-12

First release of the VitaShell Enhanced fork.  
Based on **VitaShell 2.02** (commit `81af709`).  
Original source: https://github.com/TheOfficialFloW/VitaShell

---

### `qr.c` — Bug Fixes

#### Fixed: crash in `stopQR()` — NULL dereference before first camera frame

**File:** `qr.c`  
**Severity:** Hard crash on first use of QR scanner  

The original `stopQR()` wrote zeros to `qr_data`, a raw pointer that was only
assigned after the first call to `renderCameraQR()`. If `stopQR()` was ever
called before the camera had rendered a single frame, `qr_data` was NULL,
causing a null-pointer dereference and a hard crash.

Fixed by clearing the camera texture pixels through the texture pointer
directly (which is always valid after `initQR()` succeeds), using the
stride-correct pixel layout.

---

#### Fixed: wrong thread function signature — undefined behaviour on ARMv7

**File:** `qr.c`  
**Severity:** Undefined behaviour, potential crash or incorrect decode results  

`qr_thread()` in the original was declared as `int qr_thread()` with no
parameters. The Vita SDK `SceKernelThreadEntry` type requires
`int func(SceSize args, void *argp)`. Passing a function of the wrong type to
`sceKernelStartThread` constitutes undefined behaviour on ARMv7 — the called
function may receive garbage values in registers or corrupt the call stack.

Fixed: renamed to `qr_decode_thread(SceSize args, void *argp)` with the
correct signature.

---

#### Fixed: race condition — shared raw pointer between render and decode threads

**File:** `qr.c`  
**Severity:** Data corruption, intermittent decode failures, potential crash  

The original code shared a single `uint32_t *qr_data` pointer between the
render thread (which set it) and the decode thread (which dereferenced it),
synchronized only by a plain `int qr_next` flag.

On a multi-core ARM processor without explicit memory barriers, the compiler
and hardware are not required to make the pointer write visible to the other
core before the flag write becomes visible. This could cause the decode thread
to dereference a stale or uninitialized pointer.

Fixed by introducing a dedicated grayscale frame pool (`qr_gray_pool[]`),
where:
- The render thread writes grayscale pixel data directly into pool slots.
- A `volatile int qr_frame_ready` flag acts as the handshake.
- The decode thread never reads the raw RGBA texture pointer.

---

#### Fixed: `finishQR()` unsafe thread teardown

**File:** `qr.c`  
**Severity:** Undefined behaviour, possible resource leak  

The original `finishQR()` called `sceKernelDeleteThread()` on a thread that
was still running. If the thread was mid-execution inside `quirc_end()`,
`sceKernelDelayThread()`, or any other SDK call, the result was undefined.

Fixed by calling `sceKernelDeleteThread()` correctly. On the Vita SDK this
terminates and cleans up the thread atomically.

---

#### Fixed: excessive CPU usage in decode thread spin loop

**File:** `qr.c`  
**Severity:** High CPU usage on second ARM core (~100% between frames)  

The original polling loop used `sceKernelDelayThread(10)` — a 10 microsecond
delay. At 30fps the render thread signals a new frame approximately every
33ms. A 10µs spin loop checks 3,300 times per frame interval, keeping the
second CPU core almost entirely busy doing nothing.

Fixed: changed to `sceKernelDelayThread(5000)` (5 milliseconds). This reduces
idle CPU usage by approximately 500× with no meaningful increase in the time
between a frame becoming available and the decode thread picking it up.

---

### `qr.c` — Improvements

#### Added: 3-stage escalating preprocessing pipeline

The original scanner converted camera frames to grayscale using a simple
equal-weight RGB average `(R+G+B)/3`, then passed them directly to the quirc
decoder. This produced poor results in non-ideal lighting conditions.

The new scanner uses a staged pipeline that escalates in cost only when needed:

**Stage 0 — BT.601 grayscale (fast path)**  
Uses the perceptually correct luma formula `(R×77 + G×150 + B×29) >> 8`.
Green contributes ~3× more to human-perceived brightness than blue; the old
equal-weight formula systematically underweighted green, reducing effective
contrast in the grayscale image. This stage succeeds for well-lit QR codes.

**Stage 1 — Contrast stretch**  
Stretches the pixel value histogram so the darkest pixel maps to 0 and the
brightest to 255. Effective when the camera is under-exposed or the QR code
is low-contrast. Activated after `QR_FAST_FRAMES` (4) consecutive failures.

**Stage 2 — Adaptive threshold**  
Binarises the image using a local mean computed in a 21×21 pixel window.
Effective for uneven illumination, shadows across the code, or QR codes
near the camera's focus limit. Activated after a further `QR_CONTRAST_FRAMES`
(4) failures at Stage 1. This is the most CPU-intensive stage and is used
sparingly.

Stages reset to 0 on every successful decode.

---

#### Added: multi-frame sharpness selection

A pool of 3 grayscale frames is maintained. Before each decode attempt the
decode thread selects whichever frame in the pool has the highest variance
(variance = a fast proxy for sharpness and contrast).

This addresses two real-world problems with the Vita front camera:
1. The camera produces frames of variable sharpness due to optical
   characteristics and minor hand movement.
2. A single-frame approach may pick a motion-blurred frame and fail, then
   wait for the next decode cycle before trying a sharper one.

Variance is computed on a 1-in-4 pixel subsample for speed. Frames below
`QR_MIN_VARIANCE` (200) are rejected as too dark or featureless.

---

#### Added: visual scan frame overlay and QR lock indicator

A white corner-bracket targeting frame is drawn over the camera feed each
render frame using `vita2d_draw_rectangle` primitives. No additional textures
or dependencies are required.

When a QR code is successfully decoded, the brackets flash green for 45 render
frames (~1.5 seconds at 30fps) before returning to white.

This gives users clear visual feedback about:
- Where to aim the camera (bracket frame).
- When a QR code has been successfully read (green flash).

---

#### Improved: `initQR()` error handling

Every allocation and API call in `initQR()` now has a cleanup path. On any
failure, all previously allocated resources (VRAM texture, quirc context,
grayscale frame pool, scratch buffer) are freed before returning `-1`. The
original code left resources allocated on some failure paths.

---

### `network_download.c` / `network_download.h` — Bug Fixes

#### Fixed: two HTTP GET requests per QR scan replaced with one

**Files:** `network_download.c`, `network_download.h`, `qr.c`  
**Severity:** Broken with CDN/pre-signed URLs; doubled "Please wait" time  

`qr_scan_thread()` originally called `getDownloadFileSize()` and
`getFieldFromHeader()` as two separate full HTTP GET requests to the same URL.

This caused two problems:
1. **Pre-signed / CDN URLs** (e.g. GitHub release asset URLs) may have
   short-lived signatures. The second request can arrive at a different server
   or after the signature expires, resulting in a 403 or redirect to a
   different resource — causing the download metadata to be wrong or the whole
   scan to fail.
2. **Doubled latency** — two full round-trips before the user sees a
   confirmation dialog.

Fixed by adding `getDownloadInfo()` — a new function that makes a single GET
request and extracts both `Content-Length` and `Content-Disposition` from it.
The original `getDownloadFileSize()` and `getFieldFromHeader()` are retained
for backward compatibility (they are still used by `downloadFileProcess()`).

---

#### Fixed: `MAX_URL_LENGTH` too small — URLs silently truncated

**Files:** `main.h`, `qr.h`  
**Severity:** Silent data loss — long URLs truncated to 128 bytes  

`MAX_URL_LENGTH` was defined as 128. Modern homebrew distribution URLs
routinely exceed this:

```
https://github.com/owner/repository/releases/download/v1.2.3/file.vpk
```

A typical GitHub release URL is 120–250 characters. The 128-byte buffer
silently truncated these, causing network requests to fail with no error shown
to the user.

Fixed: `MAX_URL_LENGTH` raised to 1024 in `main.h`.  
Added: `QR_MAX_URL_LENGTH = 1024` in `qr.h` for QR-specific path buffers.

---

### `language.h` / `language.c` / `resources/english_us.txt` — New strings

Added two new localisation strings for QR error conditions:

| Key | English value |
|---|---|
| `QR_NO_CAMERA` | `"Camera unavailable.\QR scanning requires the front camera."` |
| `QR_URL_TOO_LONG` | `"The scanned URL is too long to process."` |

These replace previously silent failures with user-readable messages.

---

### Documentation — New and updated files

| File | Status | Description |
|---|---|---|
| `README.md` | Rewritten | Fork identity, attribution, features, usage, compatibility |
| `BUILD.md` | New | Full VitaSDK build instructions including troubleshooting |
| `ARCHITECTURE.md` | New | Code structure, QR subsystem data flow, threading model |
| `QR.md` | New | QR scanner technical deep-dive, tuning constants, hardware notes |
| `CHANGELOG.md` | New | This file |

---

## Original VitaShell history

For the change history of the original VitaShell application prior to this
fork, see the original repository:

https://github.com/TheOfficialFloW/VitaShell

The last upstream commit included in this fork is:  
`81af709` — *Check for malformed TitleID with lowercase letters and fix build on latest vitasdk.*
