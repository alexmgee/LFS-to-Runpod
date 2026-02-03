# Operator system with typed properties, lifecycle methods, and undo support

from typing import Any, Set

import lichtfeld as lf

from .props import Property
from .types import Operator as BaseOperator


class Operator(BaseOperator):
    """Base class for all operators.

    Class attributes:
        label: Human-readable name
        description: Tooltip text
        options: Set of options like {'UNDO', 'REGISTER', 'BLOCKING'}

    Properties are declared as class attributes using Property descriptors.
    """

    label: str = ""
    description: str = ""
    options: Set[str] = set()

    def __init__(self):
        self._property_values: dict[str, Any] = {}
        self._init_properties()

    def _init_properties(self):
        """Initialize property values from class definitions."""
        for name, prop in self._get_properties().items():
            self._property_values[name] = prop.default

    @classmethod
    def _get_properties(cls) -> dict[str, Property]:
        """Get all Property definitions from class hierarchy."""
        props = {}
        for klass in reversed(cls.__mro__):
            for name, attr in vars(klass).items():
                if isinstance(attr, Property):
                    props[name] = attr
        return props

    def __getattr__(self, name: str) -> Any:
        if name.startswith("_"):
            raise AttributeError(name)
        pv = object.__getattribute__(self, "_property_values")
        if name in pv:
            return pv[name]
        raise AttributeError(f"'{type(self).__name__}' has no attribute '{name}'")

    def __setattr__(self, name: str, value: Any):
        if name.startswith("_"):
            object.__setattr__(self, name, value)
            return
        try:
            pv = object.__getattribute__(self, "_property_values")
        except AttributeError:
            object.__setattr__(self, name, value)
            return
        if name in pv:
            prop = self._get_properties().get(name)
            if prop:
                value = prop.validate(value)
            pv[name] = value
        else:
            object.__setattr__(self, name, value)

    @classmethod
    def poll(cls, context) -> bool:
        """Return True if operator can run in current context."""
        return True

    def invoke(self, context, event) -> set:
        """Called when operator is invoked.

        Returns:
            {'RUNNING_MODAL'} - Operator continues running, receiving modal events
            {'FINISHED'} - Operator completed immediately
            {'CANCELLED'} - Operator was cancelled
        """
        return self.execute(context)

    def modal(self, context, event) -> set:
        """Handle events while running.

        Returns:
            {'RUNNING_MODAL'} - Continue receiving events
            {'FINISHED'} - Complete successfully
            {'CANCELLED'} - Cancel and revert changes
            {'PASS_THROUGH'} - Don't consume event, pass to next handler
        """
        return {"FINISHED"}

    def execute(self, context) -> set:
        """Execute the operator (non-modal path).

        Returns:
            {'FINISHED'} - Completed successfully
            {'CANCELLED'} - Failed or cancelled
        """
        return {"FINISHED"}

    def cancel(self, context):
        """Called when operator is cancelled."""
        pass


def _on_cancel_active_operator():
    """Callback from C++ when ESC is pressed."""
    lf.ui.ops.cancel_modal()


def register():
    """Register operator system callbacks."""
    lf.ui.set_cancel_operator_callback(_on_cancel_active_operator)


def unregister():
    """Unregister operator system."""
    pass
