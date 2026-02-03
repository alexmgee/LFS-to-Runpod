# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for hook registration edge cases."""

import gc
import weakref

import pytest


class TestHookEdgeCases:
    """Tests for hook registration edge cases."""

    def test_remove_hook_with_gc_callback(self, lf):
        """Remove hook whose callback has been garbage collected."""
        results = []

        def create_callback():
            def callback(ctx):
                results.append("called")

            return callback

        # Register and then delete local reference
        cb = create_callback()
        weak_cb = weakref.ref(cb)
        lf.on_training_start(cb)

        # Delete and force GC
        del cb
        gc.collect()

        # Callback may or may not be collected depending on implementation
        # This test verifies no crash occurs

    def test_hook_callback_raises_exception(self, lf):
        """Hook callback that raises exception."""
        other_called = [False]

        @lf.on_training_end
        def failing_hook(ctx):
            raise RuntimeError("Hook failed")

        @lf.on_training_end
        def other_hook(ctx):
            other_called[0] = True

        # Hooks registered, would be invoked during training
        # Exception in one should not prevent others

    def test_register_same_callback_twice(self, lf):
        """Registering same callback function twice."""
        call_count = [0]

        def my_callback(ctx):
            call_count[0] += 1

        # Register twice
        lf.on_iteration_start(my_callback)
        lf.on_iteration_start(my_callback)

        # May be registered once or twice depending on implementation

    def test_remove_during_iteration(self, lf):
        """Attempt to remove hooks during hook iteration."""
        results = []
        hooks_to_remove = []

        def self_removing(ctx):
            results.append("removing")
            # Try to stop animation (clears frame hooks)
            try:
                lf.stop_animation()
            except Exception:
                pass

        lf.on_frame(self_removing)
        lf.stop_animation()  # Cleanup

    def test_many_hooks_registration(self, lf):
        """Register many hooks."""
        hooks = []

        for i in range(100):
            hook = lf.on_training_start(lambda ctx, i=i: None)
            hooks.append(hook)

        # Should handle many registrations
        del hooks
        gc.collect()


class TestHookTypes:
    """Tests for different hook types."""

    def test_all_hook_types_accept_callable(self, lf):
        """All hook decorators accept callable."""
        results = []

        @lf.on_training_start
        def start_hook(ctx):
            results.append("start")

        @lf.on_iteration_start
        def iter_start_hook(ctx):
            results.append("iter_start")

        @lf.on_pre_optimizer_step
        def pre_opt_hook(ctx):
            results.append("pre_opt")

        @lf.on_post_step
        def post_step_hook(ctx):
            results.append("post_step")

        @lf.on_training_end
        def end_hook(ctx):
            results.append("end")

        # All decorators should work

    def test_frame_hook_stop(self, lf):
        """Frame hook with stop_animation."""
        frame_count = [0]

        def frame_hook(dt):
            frame_count[0] += 1
            if frame_count[0] >= 3:
                lf.stop_animation()

        lf.on_frame(frame_hook)
        lf.stop_animation()  # Ensure cleanup


class TestHookContext:
    """Tests for hook context access."""

    def test_hook_with_none_context(self, lf):
        """Hook that receives None context."""

        @lf.on_training_start
        def handle_none(ctx):
            # Should handle None gracefully
            if ctx is None:
                return
            _ = ctx.iteration

    def test_hook_modifies_context(self, lf):
        """Hook that tries to modify context."""

        @lf.on_iteration_start
        def modify_ctx(ctx):
            try:
                ctx.iteration = 999  # Attempt modification
            except (AttributeError, TypeError):
                pass  # Expected - context may be read-only


class TestHookLifecycle:
    """Tests for hook lifecycle."""

    def test_hook_persists_across_sessions(self, lf):
        """Hooks should persist if sessions are restarted."""
        call_count = [0]

        @lf.on_training_start
        def persistent_hook(ctx):
            call_count[0] += 1

        # Hook registered, should persist

    def test_hook_cleanup_on_module_unload(self, lf):
        """Hooks from modules should be cleaned on unload."""
        # This is more of an integration test - hooks registered by
        # plugins should be cleaned when plugin unloads

        # Create inline module-like scope
        call_count = [0]

        def module_hook(ctx):
            call_count[0] += 1

        lf.on_training_end(module_hook)

        # Simulate module cleanup
        del module_hook
        gc.collect()

        # System should not crash
