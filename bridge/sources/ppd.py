# SPDX-License-Identifier: GPL-2.0
"""Optional power-profiles-daemon adapter.

PPD is the freedesktop power-profile service used by GNOME and optionally by
KDE and other desktops.  This adapter only translates ActiveProfile changes
into huawei-turbod queue events; it is not required by the core bridge.
"""

import re
import subprocess
import time

PPD_DEST = "net.hadess.PowerProfiles"
PPD_PATH = "/net/hadess/PowerProfiles"
PROFILE_RE = re.compile(r"ActiveProfile'\s*:\s*<'([^']+)'>")


def ppd_monitor(target_queue, stop):
    """Forward PPD ActiveProfile changes to the daemon's queue."""
    while not stop.is_set():
        try:
            proc = subprocess.Popen(
                [
                    "gdbus",
                    "monitor",
                    "--system",
                    "--dest",
                    PPD_DEST,
                    "--object-path",
                    PPD_PATH,
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
        except OSError:
            time.sleep(5)
            continue

        try:
            for line in proc.stdout:
                if stop.is_set():
                    break
                match = PROFILE_RE.search(line)
                if match:
                    target_queue.put(match.group(1))
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
        if not stop.is_set():
            time.sleep(5)
