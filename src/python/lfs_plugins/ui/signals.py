# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reactive signals for state management.

Signals provide a way to track state changes without polling. When a signal's
value changes, all subscribers are notified.

Example:
    count = Signal(0)
    count.subscribe(lambda v: print(f"Count changed to {v}"))
    count.value = 5  # Prints: "Count changed to 5"
"""

from __future__ import annotations

import logging
import time
from collections.abc import Callable
from contextlib import contextmanager
from threading import Lock
from typing import Generic, TypeVar

T = TypeVar("T")

logger = logging.getLogger(__name__)


class Signal(Generic[T]):
    """A reactive signal that notifies subscribers when its value changes.

    Signals are the foundation of the reactive state system. They hold a value
    and notify subscribers when that value changes.

    Attributes:
        value: The current value. Setting this notifies subscribers if changed.

    Example:
        is_training = Signal(False)
        is_training.subscribe(lambda v: update_ui())
        is_training.value = True  # Triggers update_ui()
    """

    __slots__ = ("_value", "_subscribers", "_lock", "_name", "_next_id")

    def __init__(self, initial_value: T, name: str = "") -> None:
        self._value = initial_value
        self._subscribers: dict[int, Callable[[T], None]] = {}
        self._lock = Lock()
        self._name = name
        self._next_id = 0

    @property
    def value(self) -> T:
        return self._value

    @value.setter
    def value(self, new_value: T) -> None:
        if self._value == new_value:
            return

        self._value = new_value

        if _batch_context.is_batching:
            _batch_context.pending_notifications.add(self)
            return

        self._notify()

    def _notify(self) -> None:
        with self._lock:
            callbacks = list(self._subscribers.values())

        for callback in callbacks:
            try:
                callback(self._value)
            except Exception as e:
                logger.error("Signal '%s' callback error: %s", self._name or "unnamed", e)

    def subscribe(self, callback: Callable[[T], None]) -> Callable[[], None]:
        """Subscribe to value changes.

        Args:
            callback: Called with new value when signal changes.

        Returns:
            Unsubscribe function. Call it to stop receiving notifications.
        """
        with self._lock:
            sub_id = self._next_id
            self._next_id += 1
            self._subscribers[sub_id] = callback

        def unsubscribe() -> None:
            with self._lock:
                self._subscribers.pop(sub_id, None)

        return unsubscribe

    def subscribe_as(self, owner: str, callback: Callable[[T], None]) -> Callable[[], None]:
        """Subscribe with owner tracking for automatic cleanup on plugin unload."""
        from .subscription_registry import SubscriptionRegistry

        unsub = self.subscribe(callback)
        return SubscriptionRegistry.instance().register(owner, unsub)

    def peek(self) -> T:
        """Get value without triggering tracking in computed signals."""
        return self._value

    def __repr__(self) -> str:
        name = f" '{self._name}'" if self._name else ""
        return f"<Signal{name}: {self._value!r}>"


class ComputedSignal(Generic[T]):
    """A signal whose value is derived from other signals.

    Computed signals automatically update when their dependencies change.

    Example:
        a = Signal(2)
        b = Signal(3)
        product = ComputedSignal(lambda: a.value * b.value, [a, b])
        print(product.value)  # 6
        a.value = 4
        print(product.value)  # 12
    """

    __slots__ = (
        "_compute",
        "_dependencies",
        "_cached_value",
        "_dirty",
        "_subscribers",
        "_lock",
        "_unsubscribers",
        "_next_id",
    )

    def __init__(
        self,
        compute: Callable[[], T],
        dependencies: list[Signal],
    ) -> None:
        self._compute = compute
        self._dependencies = dependencies
        self._cached_value: T | None = None
        self._dirty = True
        self._subscribers: dict[int, Callable[[T], None]] = {}
        self._lock = Lock()
        self._unsubscribers: list[Callable[[], None]] = []
        self._next_id = 0

        for dep in dependencies:
            unsub = dep.subscribe(self._on_dependency_change)
            self._unsubscribers.append(unsub)

    def _on_dependency_change(self, _: object) -> None:
        self._dirty = True
        self._notify()

    @property
    def value(self) -> T:
        if self._dirty:
            self._cached_value = self._compute()
            self._dirty = False
        return self._cached_value  # type: ignore

    def _notify(self) -> None:
        with self._lock:
            callbacks = list(self._subscribers.values())

        for callback in callbacks:
            try:
                callback(self.value)
            except Exception as e:
                logger.error("ComputedSignal callback error: %s", e)

    def subscribe(self, callback: Callable[[T], None]) -> Callable[[], None]:
        with self._lock:
            sub_id = self._next_id
            self._next_id += 1
            self._subscribers[sub_id] = callback

        def unsubscribe() -> None:
            with self._lock:
                self._subscribers.pop(sub_id, None)

        return unsubscribe

    def subscribe_as(self, owner: str, callback: Callable[[T], None]) -> Callable[[], None]:
        """Subscribe with owner tracking for automatic cleanup on plugin unload."""
        from .subscription_registry import SubscriptionRegistry

        unsub = self.subscribe(callback)
        return SubscriptionRegistry.instance().register(owner, unsub)

    def __repr__(self) -> str:
        return f"<ComputedSignal: {self.value!r}>"


class ThrottledSignal(Generic[T]):
    """A signal that throttles notifications to a maximum rate.

    Useful for high-frequency updates like training iteration counts where
    updating the UI at every change would be wasteful.

    Example:
        iteration = ThrottledSignal(0, max_rate_hz=30)
        iteration.subscribe(lambda v: update_progress_bar(v))
        for i in range(100000):
            iteration.value = i  # Only notifies ~30 times/second
    """

    __slots__ = ("_signal", "_max_rate_hz", "_last_notify_time", "_pending_value", "_has_pending")

    def __init__(self, initial_value: T, max_rate_hz: float = 60.0, name: str = "") -> None:
        self._signal = Signal(initial_value, name)
        self._max_rate_hz = max_rate_hz
        self._last_notify_time = 0.0
        self._pending_value: T = initial_value
        self._has_pending = False

    @property
    def value(self) -> T:
        return self._signal.value

    @value.setter
    def value(self, new_value: T) -> None:
        if self._signal._value == new_value:
            return

        now = time.monotonic()
        min_interval = 1.0 / self._max_rate_hz

        if now - self._last_notify_time >= min_interval:
            self._signal.value = new_value
            self._last_notify_time = now
            self._has_pending = False
        else:
            self._pending_value = new_value
            self._has_pending = True

    def flush(self) -> None:
        """Force notification of any pending value."""
        if self._has_pending:
            self._signal.value = self._pending_value
            self._last_notify_time = time.monotonic()
            self._has_pending = False

    def subscribe(self, callback: Callable[[T], None]) -> Callable[[], None]:
        return self._signal.subscribe(callback)

    def subscribe_as(self, owner: str, callback: Callable[[T], None]) -> Callable[[], None]:
        """Subscribe with owner tracking for automatic cleanup on plugin unload."""
        return self._signal.subscribe_as(owner, callback)

    def __repr__(self) -> str:
        return f"<ThrottledSignal({self._max_rate_hz}Hz): {self._signal.value!r}>"


class _BatchContext:
    """Context for batching signal updates."""

    __slots__ = ("is_batching", "pending_notifications")

    def __init__(self) -> None:
        self.is_batching = False
        self.pending_notifications: set[Signal] = set()


_batch_context = _BatchContext()


class Batch:
    """Context manager for batching multiple signal updates.

    Inside a batch, signal notifications are deferred until the batch ends.
    This prevents intermediate states from triggering UI updates.

    Example:
        with Batch():
            state.x.value = 10
            state.y.value = 20
            state.z.value = 30
        # All subscribers notified once at end, not three times
    """

    def __enter__(self) -> Batch:
        _batch_context.is_batching = True
        return self

    def __exit__(self, *args: object) -> None:
        _batch_context.is_batching = False
        pending = _batch_context.pending_notifications.copy()
        _batch_context.pending_notifications.clear()
        for signal in pending:
            signal._notify()


@contextmanager
def batch():
    """Context manager for batching signal updates.

    Alias for Batch() as a function.
    """
    with Batch():
        yield
