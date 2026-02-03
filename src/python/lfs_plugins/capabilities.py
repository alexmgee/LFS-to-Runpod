# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Plugin capability registry for cross-plugin feature exposure."""

import logging
import threading
import traceback
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Any, Callable, Dict, List, Optional

if TYPE_CHECKING:
    from .context import PluginContext

_log = logging.getLogger(__name__)


@dataclass
class CapabilitySchema:
    """JSON Schema-like definition for capability arguments."""

    properties: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    required: List[str] = field(default_factory=list)


@dataclass
class Capability:
    """A registered capability provided by a plugin."""

    name: str
    description: str
    handler: Callable[[Dict[str, Any], "PluginContext"], Dict[str, Any]]
    schema: CapabilitySchema = field(default_factory=CapabilitySchema)
    plugin_name: Optional[str] = None
    requires_gui: bool = True


class CapabilityRegistry:
    """Thread-safe registry for plugin capabilities."""

    _instance: Optional["CapabilityRegistry"] = None
    _lock = threading.Lock()

    def __init__(self):
        self._capabilities: Dict[str, Capability] = {}
        self._mutex = threading.Lock()

    @classmethod
    def instance(cls) -> "CapabilityRegistry":
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = cls()
        return cls._instance

    def register(
        self,
        name: str,
        handler: Callable[[Dict[str, Any], "PluginContext"], Dict[str, Any]],
        description: str = "",
        schema: Optional[CapabilitySchema] = None,
        plugin_name: Optional[str] = None,
        requires_gui: bool = True,
    ) -> None:
        """Register a capability.

        Args:
            name: Unique capability name (e.g., "selection.by_text")
            handler: Function(args: dict, ctx: PluginContext) -> dict
            description: Human-readable description
            schema: Optional argument schema for validation/documentation
            plugin_name: Name of the plugin providing this capability
            requires_gui: Whether this capability requires an active GUI/viewport
        """
        with self._mutex:
            if name in self._capabilities:
                _log.warning("Overwriting existing capability: %s", name)

            self._capabilities[name] = Capability(
                name=name,
                description=description,
                handler=handler,
                schema=schema or CapabilitySchema(),
                plugin_name=plugin_name,
                requires_gui=requires_gui,
            )
            _log.debug("Registered capability: %s (requires_gui=%s)", name, requires_gui)

    def unregister(self, name: str) -> bool:
        """Unregister a capability by name."""
        with self._mutex:
            if name in self._capabilities:
                del self._capabilities[name]
                _log.debug("Unregistered capability: %s", name)
                return True
            return False

    def unregister_all_for_plugin(self, plugin_name: str) -> int:
        """Unregister all capabilities from a specific plugin."""
        with self._mutex:
            to_remove = [
                name for name, cap in self._capabilities.items() if cap.plugin_name == plugin_name
            ]
            for name in to_remove:
                del self._capabilities[name]
            if to_remove:
                _log.debug("Unregistered %d capabilities for plugin: %s", len(to_remove), plugin_name)
            return len(to_remove)

    def invoke(self, name: str, args: Dict[str, Any]) -> Dict[str, Any]:
        """Invoke a capability by name.

        Args:
            name: Capability name
            args: Arguments dict

        Returns:
            Result dict with 'success' key and either 'result' or 'error'
        """
        with self._mutex:
            cap = self._capabilities.get(name)

        if cap is None:
            return {"success": False, "error": f"Unknown capability: {name}"}

        try:
            from .context import PluginContext

            ctx = PluginContext.build(self, include_view=cap.requires_gui)
            result = cap.handler(args, ctx)

            if isinstance(result, dict):
                if "success" not in result:
                    result["success"] = True
                return result
            return {"success": True, "result": result}
        except Exception as e:
            tb = traceback.format_exc()
            _log.error("Capability '%s' failed:\n%s", name, tb)
            return {"success": False, "error": str(e), "error_type": type(e).__name__, "traceback": tb}

    def get(self, name: str) -> Optional[Capability]:
        """Get a capability by name."""
        with self._mutex:
            return self._capabilities.get(name)

    def list_all(self) -> List[Capability]:
        """List all registered capabilities."""
        with self._mutex:
            return list(self._capabilities.values())

    def has(self, name: str) -> bool:
        """Check if a capability is registered."""
        with self._mutex:
            return name in self._capabilities
