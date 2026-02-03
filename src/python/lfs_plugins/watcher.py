# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Hot reload file watcher."""

import hashlib
import importlib
import logging
import threading
import time
from pathlib import Path
from typing import Dict, Set, TYPE_CHECKING

from .plugin import PluginState

_log = logging.getLogger(__name__)

if TYPE_CHECKING:
    from .manager import PluginManager


class PluginWatcher:
    """Watch plugin files for changes and trigger reloads."""

    def __init__(self, manager: "PluginManager", poll_interval: float = 1.0,
                 watch_builtins: bool = True):
        self.manager = manager
        self.poll_interval = poll_interval
        self.watch_builtins = watch_builtins
        self._running = False
        self._thread: threading.Thread = None
        self._pending_reloads: Set[str] = set()
        self._lock = threading.Lock()
        self._file_hashes: Dict[str, Dict[Path, str]] = {}
        self._builtin_path = Path(__file__).parent
        self._builtin_mtimes: Dict[Path, float] = {}

    def start(self):
        """Start the file watcher thread."""
        if self._running:
            return
        self._running = True
        self._thread = threading.Thread(target=self._watch_loop, daemon=True)
        self._thread.start()

    def stop(self):
        """Stop the file watcher."""
        self._running = False
        if self._thread:
            self._thread.join(timeout=2.0)
            self._thread = None

    def _watch_loop(self):
        """Main polling loop."""
        while self._running:
            try:
                self._check_for_changes()
                if self.watch_builtins:
                    self._check_builtin_changes()
                self._process_pending_reloads()
            except Exception as e:
                _log.error("Watcher loop error: %s", e, exc_info=True)
            time.sleep(self.poll_interval)

    def _check_for_changes(self):
        """Check all loaded plugins for file changes."""
        for name, plugin in self.manager.get_active_plugins_snapshot():
            if not plugin.info.hot_reload:
                continue
            if self._has_changes(plugin):
                with self._lock:
                    self._pending_reloads.add(name)

    def _has_changes(self, plugin) -> bool:
        """Check if any plugin files were modified."""
        plugin_name = plugin.info.name

        for py_file in plugin.info.path.rglob("*.py"):
            if ".venv" in py_file.parts:
                continue

            try:
                current_mtime = py_file.stat().st_mtime
                prev_mtime = plugin.file_mtimes.get(py_file, 0)

                if current_mtime > prev_mtime:
                    return True

                if current_mtime == prev_mtime and prev_mtime > 0:
                    if self._content_changed(plugin_name, py_file):
                        return True

            except FileNotFoundError:
                if py_file in plugin.file_mtimes:
                    return True
            except PermissionError:
                _log.warning("Permission denied: %s", py_file)
            except OSError as e:
                _log.debug("OSError checking %s: %s", py_file, e)

        return False

    def _content_changed(self, plugin_name: str, py_file: Path) -> bool:
        """Check if file content changed via SHA256 hash."""
        try:
            content = py_file.read_bytes()
            current_hash = hashlib.sha256(content).hexdigest()

            if plugin_name not in self._file_hashes:
                self._file_hashes[plugin_name] = {}

            prev_hash = self._file_hashes[plugin_name].get(py_file)
            self._file_hashes[plugin_name][py_file] = current_hash

            return prev_hash is not None and current_hash != prev_hash
        except OSError:
            return False

    def _check_builtin_changes(self):
        """Check builtin lfs_plugins files for changes."""
        for py_file in self._builtin_path.rglob("*.py"):
            if "__pycache__" in py_file.parts:
                continue

            try:
                mtime = py_file.stat().st_mtime
                prev_mtime = self._builtin_mtimes.get(py_file, 0)

                if prev_mtime > 0 and mtime > prev_mtime:
                    self._reload_builtin(py_file)

                self._builtin_mtimes[py_file] = mtime
            except OSError:
                continue

    def _reload_builtin(self, path: Path):
        """Reload a builtin module with full state preservation."""
        try:
            rel_path = path.relative_to(self._builtin_path.parent)
            module_name = str(rel_path.with_suffix("")).replace("/", ".")

            import sys
            if module_name not in sys.modules:
                return

            from .props import PropertyGroup
            for cls_name, instance in list(PropertyGroup._instances.items()):
                if instance:
                    instance._save_values()
                    PropertyGroup._instances[cls_name] = None

            module = sys.modules[module_name]
            importlib.reload(module)

            if hasattr(module, "register"):
                module.register()

            import lichtfeld as lf
            if hasattr(lf.ui, "request_redraw"):
                lf.ui.request_redraw()

        except Exception as e:
            import lichtfeld as lf
            if hasattr(lf, "LOG"):
                lf.LOG.warning(f"Hot-reload failed for {path.name}: {e}")

    def _process_pending_reloads(self):
        """Process queued plugin reloads."""
        with self._lock:
            pending = self._pending_reloads.copy()
            self._pending_reloads.clear()

        for name in pending:
            try:
                success = self.manager.reload(name)
                if success:
                    _log.info("Hot-reloaded plugin: %s", name)
                else:
                    error = self.manager.get_error(name)
                    _log.error("Hot-reload failed for %s: %s", name, error)
            except Exception as e:
                _log.error("Hot-reload exception for %s: %s", name, e, exc_info=True)

    def clear_plugin_hashes(self, plugin_name: str):
        """Clear stored hashes for a plugin."""
        self._file_hashes.pop(plugin_name, None)
