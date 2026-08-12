# Building VitaShell Enhanced

This document covers how to build VitaShell Enhanced from source.  
The build system is inherited from the original VitaShell and uses CMake + VitaSDK.

---

## Prerequisites

### VitaSDK

VitaSDK is the unofficial PS Vita homebrew toolchain.  
Install it by following the official guide: https://vitasdk.org/

**Linux (recommended):**

```sh
git clone https://github.com/vitasdk/vdpm
cd vdpm
./bootstrap-vitasdk.sh
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH
```

Add the export lines to your shell profile (`~/.bashrc`, `~/.zshrc`, etc.) to make them permanent.

**Windows:**

Building natively on Windows is not officially supported by VitaSDK.  
Use WSL2 (Windows Subsystem for Linux) with Ubuntu 22.04 or later and follow the Linux instructions above.

**macOS:**

```sh
git clone https://github.com/vitasdk/vdpm
cd vdpm
./bootstrap-vitasdk.sh
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH
```

### Required VitaSDK packages

The following packages must be installed via `vdpm` (or already included in a full VitaSDK install):

```
vita2d
quirc
libpng
libjpeg
zlib
libarchive
libbz2
liblzma
openssl
libexpat
onigmo
ftpvita
libmad
libvorbis
libogg
```

If any are missing after the bootstrap, install them individually:

```sh
vdpm quirc
vdpm libarchive
# etc.
```

### Build tools (host system)

- CMake 2.8 or later
- make (GNU)
- arm-vita-eabi-gcc (installed with VitaSDK)

---

## Getting the source

```sh
git clone https://github.com/Zushikina-kun/VitaShellEnhanced.git
cd VitaShellEnhanced
```

---

## Building

```sh
mkdir build
cd build
cmake ..
make
```

On a modern machine the build takes roughly 30–90 seconds.

### Output files

| File | Purpose |
|---|---|
| `build/VitaShell.vpk` | Installable package for the PS Vita |
| `build/eboot.bin` | Executable only (for FTP-based updates) |
| `build/VitaShell.vpk_param.sfo` | SFO parameter file |

---

## Installing on the Vita

### Via VPK (recommended for first install)

1. Copy `VitaShell.vpk` to the Vita over USB or FTP.
2. Install using molecularShell, an existing VitaShell install, or any VPK installer.

### Via FTP (for development / rapid iteration)

If VitaShell is already installed and FTP is running:

```sh
# From the build directory — replace VITA_IP with your Vita's IP
curl -T eboot.bin ftp://VITA_IP:1337/ux0:/app/VITASHELL/
```

Or use the built-in make target:

```sh
make send PSVITAIP=192.168.1.x
```

### Via USB

```sh
# Replace G: with your Vita's drive letter (Windows)
make copy
```

This copies `eboot.bin` to `G:/app/VITASHELL/eboot.bin`.

---

## Build flags

The CMakeLists.txt compiles with:

```
-Wall -O3 -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format-truncation -fno-lto
```

`-O3` is used throughout (same as the original VitaShell).  
`-fno-lto` is required — LTO can cause subtle issues with VitaSDK's stub libraries.

Do **not** add `-O4` / `-Ofast` — these can produce incorrect floating-point results on ARM that affect the UI rendering math.

---

## Submodules / modules

VitaShell includes several kernel and user modules built as separate CMake subprojects:

| Directory | Type | Purpose |
|---|---|---|
| `modules/kernel/` | `.skprx` kernel module | Kernel-level helper functions |
| `modules/user/` | `.suprx` user module | User-space helper stubs |
| `modules/patch/` | `.skprx` kernel module | Runtime patches |
| `modules/usbdevice/` | `.skprx` kernel module | USB mass storage device |

These are built automatically as part of `make`. Their outputs are embedded into the main executable as binary resources.

---

## Clean build

```sh
rm -rf build
mkdir build && cd build
cmake .. && make
```

---

## Troubleshooting

**`VITASDK not found` / `CMAKE_TOOLCHAIN_FILE` error:**

Make sure `$VITASDK` is set and exported:

```sh
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH
```

**`quirc not found`:**

```sh
vdpm quirc
```

**`libarchive not found`:**

```sh
vdpm libarchive
```

**ARM linker errors about missing stubs:**

Run a full VitaSDK update:

```sh
cd vdpm && git pull && ./bootstrap-vitasdk.sh
```

**`make: arm-vita-eabi-gcc: not found`:**

VitaSDK is not in `$PATH`. Re-run the export commands above or add them to your shell profile.

---

## Upstream relationship

This fork tracks the original VitaShell at:  
https://github.com/TheOfficialFloW/VitaShell

To pull future upstream changes into your local clone:

```sh
git remote add upstream https://github.com/TheOfficialFloW/VitaShell.git
git fetch upstream
git merge upstream/master
```

Be aware that upstream changes to `qr.c`, `network_download.c`, or `main.h`
may conflict with this fork's improvements and will need manual resolution.
