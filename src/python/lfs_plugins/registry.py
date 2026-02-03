# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Plugin registry client."""

import hashlib
import json
import logging
import urllib.request
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from .errors import PluginNotFoundError, RegistryOfflineError, VersionNotFoundError

try:
    from packaging.version import Version
except ImportError:
    Version = None

_log = logging.getLogger(__name__)

REGISTRY_URL = "https://lichtfeld.github.io/plugin-registry"
CACHE_TTL_HOURS = 1
HTTP_TIMEOUT_SEC = 10


@dataclass(frozen=True)
class RegistryPluginInfo:
    """Plugin metadata from registry."""

    name: str
    namespace: str
    display_name: str
    description: str
    author: str
    latest_version: str
    keywords: Tuple[str, ...] = field(default_factory=tuple)
    downloads: int = 0
    repository: Optional[str] = None

    @property
    def full_id(self) -> str:
        return f"{self.namespace}:{self.name}"


@dataclass(frozen=True)
class RegistryVersionInfo:
    """Version-specific metadata."""

    version: str
    min_lichtfeld_version: str
    max_lichtfeld_version: Optional[str]
    dependencies: Tuple[str, ...] = field(default_factory=tuple)
    checksum: str = ""
    download_url: str = ""
    git_ref: Optional[str] = None


class RegistryClient:
    """Fetches and caches registry data."""

    def __init__(self, cache_dir: Optional[Path] = None):
        self._cache_dir = cache_dir or Path.home() / ".lichtfeld" / "cache" / "registry"
        self._cache_dir.mkdir(parents=True, exist_ok=True)
        self._index: Optional[Dict] = None

    def search(
        self,
        query: str,
        compatible_only: bool = True,
        lichtfeld_version: str = "1.0.0",
    ) -> List[RegistryPluginInfo]:
        """Search plugins by name, description, or keywords."""
        index = self._get_index()
        query_lower = query.lower()
        results = []

        for entry in index.get("plugins", []):
            searchable = f"{entry.get('name', '')} {entry.get('summary', '')} {' '.join(entry.get('keywords', []))}".lower()
            if query_lower in searchable:
                results.append(
                    RegistryPluginInfo(
                        name=entry["name"],
                        namespace=entry.get("namespace", "community"),
                        display_name=entry.get("display_name", entry["name"]),
                        description=entry.get("summary", ""),
                        author=entry.get("author", ""),
                        latest_version=entry.get("latest_version", "0.0.0"),
                        keywords=tuple(entry.get("keywords", [])),
                        downloads=entry.get("downloads", 0),
                        repository=entry.get("repository"),
                    )
                )
        return results

    def get_plugin(self, plugin_id: str) -> Dict:
        """Get detailed plugin info from registry or cache."""
        _, name = self._parse_id(plugin_id)
        cache_path = self._cache_dir / "plugins" / f"{name}.json"

        if cache_path.exists():
            with open(cache_path) as f:
                return json.load(f)

        url = f"{REGISTRY_URL}/plugins/{name}.json"
        try:
            data = self._fetch_json(url)
            cache_path.parent.mkdir(parents=True, exist_ok=True)
            with open(cache_path, "w") as f:
                json.dump(data, f)
            return data
        except Exception as e:
            raise PluginNotFoundError(f"Plugin '{plugin_id}' not found: {e}") from e

    def resolve_version(
        self,
        plugin_id: str,
        requested_version: Optional[str],
        lichtfeld_version: str,
    ) -> RegistryVersionInfo:
        """Resolve best matching version for the given LichtFeld version."""
        plugin = self.get_plugin(plugin_id)
        versions = plugin.get("versions", {})

        if requested_version:
            if requested_version not in versions:
                raise VersionNotFoundError(f"Version {requested_version} not found for {plugin_id}")
            v = versions[requested_version]
        elif Version is None:
            if not versions:
                raise VersionNotFoundError(f"No versions found for {plugin_id}")
            v = versions[max(versions.keys())]
        else:
            current = Version(lichtfeld_version)
            compatible = [
                (Version(ver), info)
                for ver, info in versions.items()
                if Version(info.get("min_lichtfeld_version", "0.0.0")) <= current
                and (not info.get("max_lichtfeld_version") or current <= Version(info["max_lichtfeld_version"]))
            ]
            if not compatible:
                raise VersionNotFoundError(f"No compatible version for LichtFeld {lichtfeld_version}")
            compatible.sort(key=lambda x: x[0], reverse=True)
            v = compatible[0][1]

        return RegistryVersionInfo(
            version=v.get("version", requested_version or "unknown"),
            min_lichtfeld_version=v.get("min_lichtfeld_version", "0.0.0"),
            max_lichtfeld_version=v.get("max_lichtfeld_version"),
            dependencies=tuple(v.get("dependencies", [])),
            checksum=v.get("checksum", ""),
            download_url=v.get("download_url", ""),
            git_ref=v.get("git_ref"),
        )

    def verify_checksum(self, path: Path, expected: str) -> bool:
        """Verify SHA-256 checksum of a file."""
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        return actual == expected.removeprefix("sha256:")

    def _get_index(self) -> Dict:
        """Get index from cache or fetch from registry."""
        if self._index:
            return self._index

        cache_path = self._cache_dir / "index.json"
        timestamp_path = self._cache_dir / "last_update"
        cache_ttl = timedelta(hours=CACHE_TTL_HOURS)

        if cache_path.exists() and timestamp_path.exists():
            if datetime.now() - datetime.fromtimestamp(timestamp_path.stat().st_mtime) < cache_ttl:
                with open(cache_path) as f:
                    self._index = json.load(f)
                    return self._index

        try:
            self._index = self._fetch_json(f"{REGISTRY_URL}/index.json")
            with open(cache_path, "w") as f:
                json.dump(self._index, f)
            timestamp_path.touch()
            return self._index
        except Exception:
            if cache_path.exists():
                _log.debug("Registry offline, using cached index")
                with open(cache_path) as f:
                    self._index = json.load(f)
                    return self._index
            raise RegistryOfflineError("Cannot reach registry and no cache available")

    def _fetch_json(self, url: str) -> Dict:
        """Fetch JSON from URL."""
        req = urllib.request.Request(url, headers={"User-Agent": "LichtFeld-PluginManager/1.0"})
        with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT_SEC) as resp:
            return json.loads(resp.read().decode())

    def _parse_id(self, plugin_id: str) -> Tuple[str, str]:
        """Parse 'namespace:name' into (namespace, name). Defaults to 'lichtfeld' namespace."""
        if ":" in plugin_id:
            namespace, name = plugin_id.split(":", 1)
            return namespace, name
        return "lichtfeld", plugin_id
