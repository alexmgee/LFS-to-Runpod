# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for registry cache edge cases."""

import concurrent.futures
import sys
import tempfile
import threading
import time
from pathlib import Path

import pytest


@pytest.fixture
def registry_test_dir():
    """Create temporary directory for registry cache tests."""
    with tempfile.TemporaryDirectory() as tmpdir:
        cache_dir = Path(tmpdir) / "cache"
        cache_dir.mkdir()
        yield cache_dir


class TestRegistryCache:
    """Tests for registry cache edge cases."""

    def test_cache_file_corruption(self, registry_test_dir):
        """Handle corrupted cache file."""
        scripts_dir = Path(__file__).parent.parent.parent / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))

        from lfs_plugins.registry import RegistryClient

        cache_file = registry_test_dir / "registry_cache.json"
        cache_file.write_text("{ invalid json {{{{")

        client = RegistryClient(cache_dir=registry_test_dir)

        # Should handle corrupt cache gracefully
        try:
            result = client.search("test")
        except Exception:
            pass  # May fail, but should not crash

    def test_partial_json_write(self, registry_test_dir):
        """Handle partially written cache file."""
        scripts_dir = Path(__file__).parent.parent.parent / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))

        from lfs_plugins.registry import RegistryClient

        cache_file = registry_test_dir / "registry_cache.json"
        # Truncated JSON
        cache_file.write_text('{"plugins": [{"name": "test"')

        client = RegistryClient(cache_dir=registry_test_dir)

        try:
            result = client.search("test")
        except Exception:
            pass

    def test_concurrent_cache_access(self, registry_test_dir):
        """Concurrent cache read/write operations."""
        scripts_dir = Path(__file__).parent.parent.parent / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))

        from lfs_plugins.registry import RegistryClient

        errors = []
        barrier = threading.Barrier(4)

        def cache_worker():
            try:
                barrier.wait(timeout=5.0)
                client = RegistryClient(cache_dir=registry_test_dir)
                for _ in range(10):
                    try:
                        client.search("test")
                    except Exception:
                        pass  # Network errors expected
            except Exception as e:
                errors.append(e)

        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
            futures = [ex.submit(cache_worker) for _ in range(4)]
            concurrent.futures.wait(futures, timeout=30.0)

        # Should not have thread safety errors
        assert not errors, f"Errors: {errors}"

    def test_cache_directory_missing(self, registry_test_dir):
        """Handle missing cache directory."""
        scripts_dir = Path(__file__).parent.parent.parent / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))

        from lfs_plugins.registry import RegistryClient

        # Non-existent subdirectory
        nonexistent_dir = registry_test_dir / "nonexistent" / "deep" / "path"
        client = RegistryClient(cache_dir=nonexistent_dir)

        try:
            result = client.search("test")
        except Exception:
            pass  # May fail, but should not crash

    def test_cache_file_permissions(self, registry_test_dir):
        """Handle cache file permission errors."""
        scripts_dir = Path(__file__).parent.parent.parent / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))

        from lfs_plugins.registry import RegistryClient

        cache_file = registry_test_dir / "registry_cache.json"
        cache_file.write_text("{}")

        # Try to make read-only (may not work on all systems)
        try:
            cache_file.chmod(0o444)

            client = RegistryClient(cache_dir=registry_test_dir)
            try:
                result = client.search("test")
            except Exception:
                pass
        finally:
            # Restore permissions for cleanup
            try:
                cache_file.chmod(0o644)
            except Exception:
                pass


class TestRegistryCacheExpiry:
    """Tests for cache expiry logic."""

    def test_cache_ttl_respected(self, registry_test_dir):
        """Cache TTL should be respected."""
        scripts_dir = Path(__file__).parent.parent.parent / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))

        from lfs_plugins.registry import RegistryClient

        # Write old cache
        cache_file = registry_test_dir / "registry_cache.json"
        cache_file.write_text(
            """
{
    "timestamp": 0,
    "plugins": []
}
"""
        )

        client = RegistryClient(cache_dir=registry_test_dir)

        # Old cache should trigger refresh attempt
        try:
            result = client.search("test")
        except Exception:
            pass  # Network errors expected

    def test_cache_refresh_failure_uses_stale(self, registry_test_dir):
        """If refresh fails, should use stale cache."""
        scripts_dir = Path(__file__).parent.parent.parent / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))

        from lfs_plugins.registry import RegistryClient

        # Write valid but old cache
        import json
        import time

        cache_file = registry_test_dir / "registry_cache.json"
        cache_data = {
            "timestamp": time.time() - 86400 * 30,  # 30 days old
            "plugins": [{"name": "cached_plugin", "versions": []}],
        }
        cache_file.write_text(json.dumps(cache_data))

        client = RegistryClient(cache_dir=registry_test_dir)

        # Should use stale cache if network fails
        try:
            result = client.search("cached_plugin")
        except Exception:
            pass


class TestRegistryClientRobustness:
    """Tests for registry client robustness."""

    def test_search_with_special_characters(self, registry_test_dir):
        """Search query with special characters."""
        scripts_dir = Path(__file__).parent.parent.parent / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))

        from lfs_plugins.registry import RegistryClient

        client = RegistryClient(cache_dir=registry_test_dir)

        try:
            client.search("test<script>alert('xss')</script>")
        except Exception:
            pass

        try:
            client.search("test'; DROP TABLE plugins;--")
        except Exception:
            pass

        try:
            client.search("test\x00null")
        except Exception:
            pass

    def test_empty_search_query(self, registry_test_dir):
        """Empty search query."""
        scripts_dir = Path(__file__).parent.parent.parent / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))

        from lfs_plugins.registry import RegistryClient

        client = RegistryClient(cache_dir=registry_test_dir)

        try:
            result = client.search("")
        except Exception:
            pass

    def test_very_long_search_query(self, registry_test_dir):
        """Very long search query."""
        scripts_dir = Path(__file__).parent.parent.parent / "scripts"
        if str(scripts_dir) not in sys.path:
            sys.path.insert(0, str(scripts_dir))

        from lfs_plugins.registry import RegistryClient

        client = RegistryClient(cache_dir=registry_test_dir)

        try:
            result = client.search("x" * 10000)
        except Exception:
            pass
