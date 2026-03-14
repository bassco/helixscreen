# SPDX-License-Identifier: GPL-3.0-or-later
from __future__ import annotations

from pathlib import Path
from typing import Optional

MODULE_PATH = Path(__file__).resolve().parent
HELIXSCREEN_REPO = "https://github.com/prestonbrown/helixscreen"
HELIXSCREEN_DIR = Path.home().joinpath("helixscreen")
HELIXSCREEN_SERVICE_NAME = "helixscreen"
HELIXSCREEN_INSTALLER_URL = (
    "https://raw.githubusercontent.com/prestonbrown/helixscreen/main/scripts/install.sh"
)

# Platform-dependent install locations
_INSTALL_PATHS = [
    Path.home().joinpath("helixscreen"),
    Path("/opt/helixscreen"),
    Path("/usr/data/helixscreen"),
    Path("/root/printer_software/helixscreen"),
]


def find_install_dir() -> Optional[Path]:
    """Find the actual HelixScreen install directory."""
    for path in _INSTALL_PATHS:
        try:
            if path.exists() and path.joinpath("helix-screen").exists():
                return path
        except PermissionError:
            continue
    # Also scan /home/*/helixscreen
    home = Path("/home")
    if home.is_dir():
        try:
            for home_dir in home.iterdir():
                candidate = home_dir.joinpath("helixscreen")
                if candidate.exists() and candidate.joinpath("helix-screen").exists():
                    return candidate
        except PermissionError:
            pass
    return None
