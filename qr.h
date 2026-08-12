/*
  VitaShell
  Copyright (C) 2015-2018, TheFloW

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
  VitaShell Modernization - QR Scanner Overhaul
  Changes from original header:
  - Added QR_MAX_URL_LENGTH (1024) to replace the old 128-byte MAX_URL_LENGTH
    for download paths — long GitHub release URLs were being silently truncated.
  - Added qrDrawOverlay() declaration (new visual overlay function).
  - Thread entry now has correct Vita SDK signature (SceSize, void*).
*/

#ifndef __QR_H__
#define __QR_H__

#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

/*
  QR_MAX_URL_LENGTH:
  Maximum length for a URL decoded from a QR code and used in download paths.
  The original MAX_URL_LENGTH of 128 was far too small for modern URLs
  (e.g. GitHub release asset URLs are typically 150-250 chars).
  1024 bytes covers all practical homebrew distribution URLs.
  This is used for last_download[] and download_path[] buffers only;
  the QR payload buffer is still capped at MAX_QR_LENGTH (1024) from main.h.
*/
#define QR_MAX_URL_LENGTH 1024

/* Public API */
int  qr_scan_thread(SceSize args, void *argp);

int  initQR();
int  finishQR();
int  startQR();
int  stopQR();
int  enabledQR();
int  scannedQR();
int  renderCameraQR(int x, int y);
void qrDrawOverlay(int cam_x, int cam_y);
char *getLastQR();
char *getLastDownloadQR();
void setScannedQR(int scanned);

#endif
