# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Plugin dependency installer using uv."""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional, Callable, Tuple
from urllib.parse import urlparse

from .plugin import PluginInstance
from .errors import PluginDependencyError, PluginError
try:
    import tomllib
except ImportError:
    import tomli as tomllib


class PluginInstaller:
    """Install plugin dependencies using uv."""

    def __init__(self, plugin: PluginInstance):
        self.plugin = plugin

    def _get_embedded_python(self) -> Optional[Path]:
        """Get path to the embedded Python executable."""
        try:
            import lichtfeld
            python_path = lichtfeld.packages.embedded_python_path()
            if python_path:
                return Path(python_path)
        except (ImportError, AttributeError):
            pass
        return None

    def ensure_venv(self) -> bool:
        """Create plugin-specific venv if needed."""
        venv_path = self.plugin.info.path / ".venv"
        self.plugin.venv_path = venv_path

        if venv_path.exists():
            return True

        uv = self._find_uv()
        if not uv:
            raise PluginDependencyError("uv not found")

        cmd = [str(uv), "venv", str(venv_path), "--allow-existing"]

        embedded_python = self._get_embedded_python()
        if embedded_python and embedded_python.exists():
            cmd.extend(["--python", str(embedded_python)])

        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
        )

        if result.returncode != 0:
            raise PluginDependencyError(f"Failed to create venv: {result.stderr}")

        return True

    def install_dependencies(
        self, on_progress: Optional[Callable[[str], None]] = None
    ) -> bool:
        """Install all dependencies declared in plugin.toml."""
        deps = list(self.plugin.info.dependencies)
        plugin_path = self.plugin.info.path
        use_sync = (plugin_path / "uv.lock").exists()
        if not deps and not use_sync:
            return True

        uv = self._find_uv()
        if not uv:
            raise PluginDependencyError("uv not found")

        venv_python = self._get_venv_python()

        if use_sync:
            cmd = [
                str(uv),
                "sync",
                "--project",
                str(plugin_path),
                "--python",
                str(venv_python),
            ]
            action = "Syncing dependencies with uv..."
            error_label = "uv sync"
        else:
            cmd = [
                str(uv),
                "pip",
                "install",
                "--project",
                str(plugin_path),
                "--python",
                str(venv_python),
                *deps,
            ]
            action = f"Installing {len(deps)} dependencies with uv..."
            error_label = "uv pip install"

        if on_progress:
            on_progress(action)

        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        output_lines = []
        if proc.stdout is not None:
            for line in iter(proc.stdout.readline, ""):
                line = line.rstrip()
                if line and on_progress:
                    on_progress(line)
                output_lines.append(line)

        proc.wait()
        if proc.returncode != 0:
            tail = "\n".join(output_lines[-10:])
            raise PluginDependencyError(f"{error_label} failed:\n{tail}")

        return True

    def _find_uv(self) -> Optional[Path]:
        """Find uv binary."""
        # First try to get uv path from the C++ PackageManager (most reliable)
        try:
            import lichtfeld
            uv_path = lichtfeld.packages.uv_path()
            if uv_path:
                return Path(uv_path)
        except (ImportError, AttributeError):
            pass

        # Fallback: check bundled location (build directory)
        exe_dir = Path(sys.executable).parent
        bundled_paths = [
            exe_dir / "bin" / "uv",
            exe_dir / "uv",
            exe_dir.parent / "bin" / "uv",
        ]
        for p in bundled_paths:
            if p.exists():
                return p

        # Fall back to system PATH
        uv = shutil.which("uv")
        return Path(uv) if uv else None

    def _get_venv_python(self) -> Path:
        """Get path to venv's Python interpreter."""
        assert self.plugin.venv_path is not None
        venv = self.plugin.venv_path

        # Linux/macOS
        python = venv / "bin" / "python"
        if python.exists():
            return python

        # Windows
        python = venv / "Scripts" / "python.exe"
        return python


def parse_github_url(url: str) -> Tuple[str, str, Optional[str]]:
    """Parse GitHub URL into (owner, repo, branch).

    Supports:
        - https://github.com/owner/repo
        - https://github.com/owner/repo.git
        - https://github.com/owner/repo/tree/branch
        - github:owner/repo
        - github:owner/repo@branch
        - owner/repo (assumes GitHub)
    """
    url = url.strip()

    # Handle github: shorthand
    if url.startswith("github:"):
        url = url[7:]  # Remove "github:"
        if "@" in url:
            repo_part, branch = url.rsplit("@", 1)
        else:
            repo_part, branch = url, None

        parts = repo_part.split("/")
        if len(parts) != 2:
            raise PluginError(f"Invalid GitHub shorthand: {url}")
        return parts[0], parts[1], branch

    # Handle owner/repo shorthand
    if "/" in url and not url.startswith("http"):
        parts = url.split("/")
        if len(parts) == 2 and not url.startswith("."):
            return parts[0], parts[1], None

    # Normalize URLs without scheme (github.com/owner/repo -> https://github.com/owner/repo)
    if url.startswith("github.com/") or url.startswith("www.github.com/"):
        url = "https://" + url

    # Handle full URLs
    parsed = urlparse(url)
    if parsed.netloc not in ("github.com", "www.github.com"):
        raise PluginError(f"Not a GitHub URL: {url}")

    path_parts = parsed.path.strip("/").split("/")
    if len(path_parts) < 2:
        raise PluginError(f"Invalid GitHub URL: {url}")

    owner = path_parts[0]
    repo = path_parts[1].removesuffix(".git")

    # Check for /tree/branch pattern
    branch = None
    if len(path_parts) >= 4 and path_parts[2] == "tree":
        branch = path_parts[3]

    return owner, repo, branch


def clone_from_url(
    url: str,
    plugins_dir: Path,
    on_progress: Optional[Callable[[str], None]] = None,
) -> Path:
    """Clone a plugin from GitHub URL.

    Args:
        url: GitHub URL or shorthand (github:owner/repo, owner/repo)
        plugins_dir: Directory to clone into
        on_progress: Optional progress callback

    Returns:
        Path to the cloned plugin directory
    """
    owner, repo, branch = parse_github_url(url)
    clone_url = f"https://github.com/{owner}/{repo}.git"

    # Determine plugin name from repo (case-insensitive prefix removal)
    repo_lower = repo.lower()
    if repo_lower.startswith("lichtfeld-plugin-"):
        plugin_name = repo[17:]  # len("lichtfeld-plugin-")
    elif repo_lower.startswith("lfs-plugin-"):
        plugin_name = repo[11:]  # len("lfs-plugin-")
    elif repo_lower.startswith("lichtfeld-") and repo_lower.endswith("-plugin"):
        # Handle LichtFeld-X-Plugin pattern
        plugin_name = repo[10:-7]  # Remove "LichtFeld-" and "-Plugin"
    else:
        plugin_name = repo

    plugins_dir.mkdir(parents=True, exist_ok=True)
    temp_dir = Path(tempfile.mkdtemp(prefix=f".{repo}-", dir=plugins_dir))

    if on_progress:
        on_progress(f"Cloning {owner}/{repo}...")

    # Check if git is available
    git = shutil.which("git")
    if not git:
        raise PluginError("git not found in PATH")

    # Clone the repository
    cmd = [git, "clone", "--depth", "1"]
    if branch:
        cmd.extend(["--branch", branch])
    cmd.extend([clone_url, str(temp_dir)])

    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        shutil.rmtree(temp_dir, ignore_errors=True)
        raise PluginError(f"Failed to clone repository: {result.stderr}")

    # Verify it's a valid plugin
    manifest_path = temp_dir / "plugin.toml"
    if not manifest_path.exists():
        shutil.rmtree(temp_dir, ignore_errors=True)
        raise PluginError(f"Repository is not a valid plugin (missing plugin.toml)")

    with open(manifest_path, "rb") as f:
        data = tomllib.load(f)
    manifest_name = str(data.get("plugin", {}).get("name", "")).strip()
    final_name = manifest_name or plugin_name
    target_dir = plugins_dir / final_name

    if target_dir.exists():
        shutil.rmtree(temp_dir, ignore_errors=True)
        raise PluginError(f"Plugin directory already exists: {target_dir}")

    if temp_dir != target_dir:
        temp_dir.replace(target_dir)

    if on_progress:
        on_progress(f"Cloned {final_name}")

    return target_dir


def update_plugin(
    plugin_dir: Path,
    on_progress: Optional[Callable[[str], None]] = None,
) -> bool:
    """Update a plugin by pulling latest changes.

    Args:
        plugin_dir: Plugin directory (must be a git repo)
        on_progress: Optional progress callback

    Returns:
        True if updated successfully
    """
    git_dir = plugin_dir / ".git"
    if not git_dir.exists():
        raise PluginError(f"Plugin is not a git repository: {plugin_dir}")

    git = shutil.which("git")
    if not git:
        raise PluginError("git not found in PATH")

    if on_progress:
        on_progress(f"Updating {plugin_dir.name}...")

    result = subprocess.run(
        [git, "pull", "--ff-only"],
        cwd=plugin_dir,
        capture_output=True,
        text=True,
    )

    if result.returncode != 0:
        raise PluginError(f"Failed to update plugin: {result.stderr}")

    if on_progress:
        on_progress(f"Updated {plugin_dir.name}")

    return True


def uninstall_plugin(plugin_dir: Path) -> bool:
    """Remove a plugin directory.

    Args:
        plugin_dir: Plugin directory to remove

    Returns:
        True if removed successfully
    """
    if not plugin_dir.exists():
        return False

    shutil.rmtree(plugin_dir)
    return True
