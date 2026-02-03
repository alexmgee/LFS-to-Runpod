# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Registry for tracking signal subscriptions by owner."""

from __future__ import annotations

import logging
from collections import defaultdict
from threading import Lock
from typing import Callable

logger = logging.getLogger(__name__)


class SubscriptionRegistry:
    """Tracks signal subscriptions by owner for automatic cleanup."""

    _instance: SubscriptionRegistry | None = None

    def __init__(self) -> None:
        self._subscriptions: dict[str, list[Callable[[], None]]] = defaultdict(list)
        self._lock = Lock()

    @classmethod
    def instance(cls) -> SubscriptionRegistry:
        if cls._instance is None:
            cls._instance = cls()
        return cls._instance

    def register(self, owner: str, unsubscribe: Callable[[], None]) -> Callable[[], None]:
        """Register an unsubscribe function under an owner.

        Returns a wrapped unsubscribe that also removes from registry.
        """
        with self._lock:
            self._subscriptions[owner].append(unsubscribe)

        def wrapped_unsubscribe() -> None:
            unsubscribe()
            with self._lock:
                try:
                    self._subscriptions[owner].remove(unsubscribe)
                except ValueError:
                    pass

        return wrapped_unsubscribe

    def unsubscribe_all(self, owner: str) -> int:
        """Unsubscribe all callbacks for an owner. Returns count."""
        with self._lock:
            unsubs = self._subscriptions.pop(owner, [])

        count = 0
        for unsub in unsubs:
            try:
                unsub()
                count += 1
            except Exception as e:
                logger.error("Error unsubscribing for %s: %s", owner, e)

        return count
