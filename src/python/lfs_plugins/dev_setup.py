# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Plugin development setup - creates plugin with venv and VS Code config."""

import json
import subprocess
from pathlib import Path

from .templates import create_plugin


def create_plugin_with_venv(
    name: str,
    *,
    uv_path: str,
    python_path: str,
    typings_dir: str = "",
    site_packages_dir: str = "",
) -> str:
    """Create plugin with venv and VS Code configuration. Returns plugin path."""
    plugin_dir = create_plugin(name)

    assert uv_path, "UV path not provided"
    assert python_path, "Python path not provided"

    venv_path = plugin_dir / ".venv"

    cmd = [uv_path, "venv", str(venv_path), "--python", python_path]
    subprocess.run(cmd, capture_output=True, text=True, check=True)

    _generate_vscode_config(plugin_dir, venv_path, typings_dir, site_packages_dir)
    return str(plugin_dir)


def _generate_vscode_config(
    plugin_dir: Path,
    venv_path: Path,
    typings_dir: str,
    site_packages_dir: str,
) -> None:
    venv_python = venv_path / "bin" / "python"

    vscode_dir = plugin_dir / ".vscode"
    vscode_dir.mkdir(exist_ok=True)

    extra_paths = []
    if typings_dir:
        extra_paths.append(typings_dir)
    if site_packages_dir:
        extra_paths.append(site_packages_dir)

    settings = {
        "python.defaultInterpreterPath": str(venv_python),
        "python.analysis.extraPaths": extra_paths,
        "python.analysis.typeCheckingMode": "basic",
    }
    (vscode_dir / "settings.json").write_text(json.dumps(settings, indent=4) + "\n")

    pyright = {
        "include": ["."],
        "extraPaths": extra_paths,
        "pythonVersion": "3.12",
        "typeCheckingMode": "basic",
        "venvPath": str(plugin_dir),
        "venv": ".venv",
    }
    (plugin_dir / "pyrightconfig.json").write_text(json.dumps(pyright, indent=4) + "\n")

    launch = {
        "version": "0.2.0",
        "configurations": [
            {
                "name": "Attach to LichtFeld",
                "type": "debugpy",
                "request": "attach",
                "connect": {"host": "localhost", "port": 5678},
            }
        ],
    }
    (vscode_dir / "launch.json").write_text(json.dumps(launch, indent=4) + "\n")
