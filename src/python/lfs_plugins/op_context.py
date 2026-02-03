# Operator context - passed to operators for scene access and modal registration
# Named op_context.py to avoid conflict with the existing context.py (plugin context)

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .operators import Operator

import lichtfeld as lf


class OperatorContext:
    """Context passed to operators.

    Provides:
    - Scene state queries (has_scene, num_gaussians, etc.)
    - Modal handler registration (modal_handler_add/remove)
    - Undo system integration
    """

    def __init__(self):
        self._modal_handlers: list["Operator"] = []

    def _get_app_context(self):
        """Get the unified application context."""
        return lf.ui.context()

    @property
    def has_scene(self) -> bool:
        return self._get_app_context().has_scene

    @property
    def has_selection(self) -> bool:
        return self._get_app_context().has_selection

    @property
    def num_gaussians(self) -> int:
        return self._get_app_context().num_gaussians

    @property
    def can_transform(self) -> bool:
        return lf.can_transform_selection()

    def modal_handler_add(self, operator: "Operator"):
        """Register operator as modal handler."""
        if operator not in self._modal_handlers:
            self._modal_handlers.append(operator)

    def modal_handler_remove(self, operator: "Operator"):
        """Unregister modal handler."""
        if operator in self._modal_handlers:
            self._modal_handlers.remove(operator)

    def has_modal_handlers(self) -> bool:
        return len(self._modal_handlers) > 0

    def get_modal_handlers(self) -> list["Operator"]:
        return self._modal_handlers.copy()

    def dispatch_modal_event(self, event) -> bool:
        """Dispatch event to modal handlers.

        Returns True if event was consumed.
        """
        for handler in self._modal_handlers[:]:
            result = handler.modal(self, event)

            if "FINISHED" in result:
                self.modal_handler_remove(handler)
                if "UNDO" in handler.options and hasattr(handler, "undo") and hasattr(handler, "redo"):
                    lf.undo.push(
                        handler.label or handler.id,
                        undo=handler.undo,
                        redo=handler.redo,
                    )
                return True

            elif "CANCELLED" in result:
                handler.cancel(self)
                self.modal_handler_remove(handler)
                return True

            elif "RUNNING_MODAL" in result:
                return True

            # PASS_THROUGH - continue to next handler

        return False


# Global context singleton
_context: OperatorContext = None


def get_context() -> OperatorContext:
    """Get the global operator context."""
    global _context
    if _context is None:
        _context = OperatorContext()
    return _context


def reset_context():
    """Reset the global context (for testing)."""
    global _context
    _context = None
