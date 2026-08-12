# VitaShell Modernization — QR Scanner

## Overview

This document describes the overhauled QR scanner introduced in the VitaShell
Modernization fork.  It covers the scanning pipeline, preprocessing stages,
threading model, known camera limitations, and how to tune the system.

---

## What changed vs. the original VitaShell QR scanner

| Area | Original | Modernized |
|---|---|---|
| Preprocessing | Raw grayscale only | 3-stage escalating pipeline |
| Frame selection | Single latest frame | Sharpness-ranked pool of 3 frames |
| Thread signature | Wrong (no SceSize/void* params) | Fixed (correct Vita SDK signature) |
| Race condition on frame data | Shared raw pointer, no sync | Separate grayscale pool, flag-based handoff |
| NULL crash in stopQR | `qr_data` could be NULL | Texture pointer checked before use |
| Thread cleanup | Delete without stop | Proper delete |
| CPU usage in spin loop | 10 µs delay (very high CPU burn) | 5 ms delay |
| URL buffer size | 128 bytes (truncated real URLs) | 1024 bytes |
| HTTP requests per scan | 2 separate requests | 1 combined request |
| Visual feedback | None (just raw camera feed) | White bracket frame + green lock flash |
| Camera not available | Generic `-1` returned silently | `QR_NO_CAMERA` message shown |

---

## Scanning Pipeline

```
Camera frame (640×360 ABGR)
        │
        ▼
  RGBA → Grayscale (BT.601 luma, integer arithmetic)
        │
        ├─ Variance check (frame sharpness)
        │   └─ Low variance? → skip frame, increment fail counter
        │
        ├─ Frame pool (3 frames buffered)
        │   └─ Decode thread picks highest-variance frame
        │
        ▼
  Stage 0: Raw grayscale → quirc decoder
        │
        └─ Failed after 4 frames?
                │
                ▼
          Stage 1: Contrast stretch (histogram equalisation)
                        → quirc decoder
                │
                └─ Failed after 4 frames?
                        │
                        ▼
                  Stage 2: Adaptive threshold (local mean, 21px window)
                                → quirc decoder
                        │
                        ▼
                   Success → payload → qr_scan_thread
                   Still failed → continue cycling stages
```

Stage resets to 0 on every successful decode.

---

## Preprocessing Stages

### Stage 0 — Raw Grayscale (Fast)

Used first because it adds zero overhead beyond the BT.601 conversion that
must happen anyway.  Succeeds on well-lit, high-contrast QR codes.

### Stage 1 — Contrast Stretch

Stretches the pixel value range so the darkest pixel in the frame maps to 0
and the brightest maps to 255.  Effective for:

- Dim ambient light
- Overexposed camera
- Low-contrast QR codes printed on glossy surfaces

Costs: one extra pass over the grayscale buffer (~230 KB on Vita).

### Stage 2 — Adaptive Threshold

Binarises the image using a local mean computed in a 21×21 pixel window with
a bias of 10.  Effective for:

- Uneven illumination (shadow across the QR code)
- Small QR codes at the edge of the camera's focus range
- QR codes on screens with slight glare

Costs: significantly more CPU than stages 0–1 due to the local-window
computation.  Used only after stages 0 and 1 have both failed.

**Implementation note:** The window scan uses a stride of 3 (samples every
3rd pixel) to reduce cost.  This is a deliberate speed/quality tradeoff; the
window is still representative enough for typical QR code sizes.

---

## Frame Pool and Sharpness Selection

Three grayscale frames are buffered.  Before the decode thread processes a
frame, it selects whichever buffered frame has the highest variance
(= highest spatial frequency = sharpest / highest contrast).

This helps in two scenarios:

1. **Camera auto-focus hunting** — the Vita front camera adjusts focus
   continuously.  Mid-hunt frames are blurry; the pool lets us skip them.

2. **Hand tremor** — holding the Vita perfectly still is difficult.  The
   pool increases the chance of catching a momentarily sharp frame.

The variance is computed on a 1-in-4 pixel subsample for speed.  Frames with
variance below `QR_MIN_VARIANCE` (200) are skipped entirely as too dark or
featureless.

---

## Visual Overlay

A white bracket-corner targeting frame is drawn over the camera feed each
render frame.  This shows the user where to aim — the QR code should fill
roughly the inner two-thirds of the frame.

When a QR code is successfully decoded, the brackets flash green for 45
render frames (~1.5 seconds at 30 fps) before returning to white.

The overlay uses `vita2d_draw_rectangle` primitives only — no extra textures
or dependencies.

---

## Threading Model

```
Main/render thread (high priority)
  │
  ├─ sceCameraRead() — captures latest frame into VRAM texture
  ├─ vita2d_draw_texture() — draws camera feed
  ├─ qrDrawOverlay() — draws bracket frame
  └─ rgba_to_gray_fast() → qr_gray_pool[write_idx]
       └─ sets qr_frame_ready = 1

Decode thread (priority 0x40, second core)
  ├─ polls qr_frame_ready every 5ms
  ├─ selects highest-variance frame from pool
  ├─ runs preprocessing pipeline
  ├─ calls quirc
  └─ sets qr_scanned = 1 on success
```

The synchronisation between threads uses a single `volatile int` flag
(`qr_frame_ready`).  This is sufficient on the Vita's dual-core ARM
because:

- Each flag write/read is a word-size atomic operation on ARMv7.
- The render thread only writes new data when `qr_frame_ready == 0`.
- The decode thread only reads data when `qr_frame_ready == 1`.
- There is exactly one producer and one consumer.

No mutex is used because acquiring a mutex on every render frame at 30fps
would add unnecessary overhead.

---

## Network: Combined Download Info Request

The original code made two separate HTTP GET requests to check a URL before
downloading:

1. `getDownloadFileSize()` — to get `Content-Length`
2. `getFieldFromHeader()` — to get `Content-Disposition`

This was problematic because:

- GitHub release asset URLs are pre-signed and expire.  The second request
  might get a different redirect or a 403.
- It doubled the "Please wait..." dialog time.
- Both requests downloaded the full response headers.

The modernized code replaces this with a single `getDownloadInfo()` call that
extracts both pieces of information from one request.

---

## URL Length

The original `MAX_URL_LENGTH` was 128 bytes.  This silently truncated long
URLs such as:

```
https://github.com/owner/repository/releases/download/v1.2.3/filename.vpk
```

(Typical GitHub release URL: ~90–200+ characters.)

`QR_MAX_URL_LENGTH` is now 1024 bytes.  `MAX_URL_LENGTH` in `main.h` has
been updated to match.

---

## Camera Hardware Notes

### PS Vita 1000 (OLED)
- Front camera: OmniVision OV5640, 640×480 max, used at 640×360 for QR.
- Fixed focus.  No autofocus on the front camera.
- Low-light performance is mediocre — Stage 1 (contrast stretch) is
  frequently needed in indoor conditions.

### PS Vita 2000 (LCD)
- Front camera: similar specification, slightly different optics.
- Same API, same behaviour.

### Camera not available
- `initQR()` returns -1 and sets `qr_enabled = 0`.
- The QR menu option should check `enabledQR()` before showing the scanner.

---

## Tuning Constants

All tuning constants are defined at the top of `qr.c`:

| Constant | Default | Meaning |
|---|---|---|
| `QR_FRAME_POOL_SIZE` | 3 | Number of frames buffered for sharpness selection |
| `QR_MIN_VARIANCE` | 200 | Minimum frame variance to attempt decode |
| `QR_FAST_FRAMES` | 4 | Frames to attempt at Stage 0 before escalating |
| `QR_CONTRAST_FRAMES` | 4 | Frames to attempt at Stage 1 before escalating |
| `QR_FRAME_BRACKET_LEN` | 40 | Length of corner bracket arms in pixels |
| `QR_FRAME_BRACKET_T` | 3 | Thickness of corner brackets in pixels |
| `QR_LOCK_FLASH_FRAMES` | 45 | Render frames to show green lock indicator |

---

## Known Limitations

1. **Fixed focus camera** — the Vita front camera cannot focus.  Small or
   distant QR codes may never decode regardless of preprocessing.  Hold the
   Vita ~15–30cm from the QR code for best results.

2. **Stage 2 CPU cost** — the adaptive threshold runs on the ARM CPU and is
   noticeably slower (~30–60ms at 444MHz) than stages 0 and 1.  It is only
   activated after 8 consecutive failed frames.

3. **No perspective correction** — heavily tilted QR codes may fail.  quirc
   handles mild perspective internally but extreme angles are out of scope.

4. **Single QR code decoded** — the system extracts all quirc results but
   reports only the first successfully decoded payload.  Multiple QR codes
   in the same frame are not supported.

5. **No GPU preprocessing** — all image processing runs on the CPU.  The
   Vita GPU (SGX543MP4) does not expose a general-purpose compute API in
   the homebrew SDK.
