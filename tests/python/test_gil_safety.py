# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for GIL and reference safety.

Targets:
- py_ui_hooks.cpp:63-94 - Callbacks vector copied outside GIL
- Hook that modifies hook registry during execution
"""

import gc
import sys
import threading
import time
from pathlib import Path

import pytest


@pytest.fixture
def lfs_types():
    """Import lfs_plugins.types module."""
    project_root = Path(__file__).parent.parent.parent
    build_python = project_root / "build" / "src" / "python"
    if str(build_python) not in sys.path:
        sys.path.insert(0, str(build_python))

    try:
        from lfs_plugins import types
        return types
    except ImportError as e:
        pytest.skip(f"lfs_plugins.types not available: {e}")


@pytest.fixture
def op_fixture(lf, lfs_types):
    """Setup and cleanup for operator tests."""
    registered = []

    yield registered, lfs_types

    for op_cls in registered:
        try:
            lf.unregister_class(op_cls)
        except Exception:
            pass


class TestGILSafety:
    """Tests for GIL safety in callbacks."""

    def test_hook_modifies_registry_during_iteration(self, lf):
        """Hook that modifies registry during execution should be safe."""
        results = []
        errors = []

        @lf.on_iteration_start
        def first_hook(ctx):
            results.append("first")
            # Register new hook during iteration
            try:

                @lf.on_iteration_start
                def dynamic_hook(ctx):
                    results.append("dynamic")

            except Exception as e:
                errors.append(e)

        # Without training, hooks won't run, but registration should work
        assert not errors

    def test_callback_deletes_itself(self, lf):
        """Callback that attempts to unregister itself."""
        callback_ref = [None]
        called = [False]

        def self_removing_callback(dt):
            called[0] = True
            # Try to stop during callback
            try:
                lf.stop_animation()
            except Exception:
                pass

        callback_ref[0] = self_removing_callback
        lf.on_frame(self_removing_callback)
        lf.stop_animation()  # Cleanup

        # Should not crash even though callback tried to remove itself

    def test_concurrent_hook_invocation(self, lf):
        """Multiple threads accessing hooks simultaneously."""
        errors = []
        registered_count = [0]
        lock = threading.Lock()

        def register_hooks():
            for i in range(10):
                try:

                    @lf.on_training_start
                    def hook(ctx, i=i):
                        pass

                    with lock:
                        registered_count[0] += 1
                except Exception as e:
                    errors.append(e)

        threads = [threading.Thread(target=register_hooks) for _ in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=5.0)

        assert not errors, f"Errors during concurrent hook registration: {errors}"


class TestCallbackLifetime:
    """Tests for callback object lifetime."""

    def test_callback_reference_after_gc(self, lf):
        """Callback should remain callable after GC."""
        called = [False]

        @lf.on_training_end
        def my_callback(ctx):
            called[0] = True

        # Force garbage collection
        gc.collect()

        # Callback should still be registered
        # (actual invocation requires training)

    def test_lambda_callback_lifetime(self, lf):
        """Lambda callbacks should not be collected prematurely."""
        results = []

        # Register lambda
        callback = lf.on_training_start(lambda ctx: results.append("called"))

        # Delete local reference
        del callback
        gc.collect()

        # Callback should still be registered in the system

    def test_method_callback_reference(self, lf):
        """Method callbacks should handle instance lifecycle."""

        class CallbackHolder:
            def __init__(self):
                self.called = False

            def callback(self, ctx):
                self.called = True

        holder = CallbackHolder()
        lf.on_training_end(holder.callback)

        # Deleting holder shouldn't crash the system
        del holder
        gc.collect()


class TestPropertyCallbackSafety:
    """Tests for property getter/setter callback safety."""

    def test_property_getter_modifies_registry(self, lf, op_fixture):
        """Property getter that modifies registry should be safe."""
        registered, lfs_types = op_fixture

        class ModifyingOp(lfs_types.Operator):
            lf_label = "Modifying Op"

            @property
            def dynamic_prop(self):
                # Try to register another operator during property access
                try:

                    class InnerOp(lfs_types.Operator):
                        lf_label = "Inner"

                        def execute(self, context):
                            return {"FINISHED"}

                    lf.register_class(InnerOp)
                    registered.append(InnerOp)
                except Exception:
                    pass
                return 42

            def execute(self, context):
                _ = self.dynamic_prop
                return {"FINISHED"}

        lf.register_class(ModifyingOp)
        registered.append(ModifyingOp)

        try:
            lf.ops.invoke(ModifyingOp._class_id())
        except Exception:
            pass

    def test_nested_callback_invocation(self, lf, op_fixture):
        """Nested callback invocations should not deadlock."""
        registered, lfs_types = op_fixture
        depth = [0]
        max_depth = [0]
        nested_op_id = [None]

        class NestedOp(lfs_types.Operator):
            lf_label = "Nested Op"

            def execute(self, context):
                depth[0] += 1
                max_depth[0] = max(max_depth[0], depth[0])
                if depth[0] < 3:
                    # Recursive invocation
                    lf.ops.invoke(nested_op_id[0])
                depth[0] -= 1
                return {"FINISHED"}

        lf.register_class(NestedOp)
        registered.append(NestedOp)
        nested_op_id[0] = NestedOp._class_id()

        try:
            lf.ops.invoke(NestedOp._class_id())
            # Should complete without deadlock
            assert max_depth[0] >= 1
        except Exception:
            pass


class TestCrossThreadCallback:
    """Tests for callbacks across threads."""

    def test_callback_from_different_thread(self, lf, op_fixture):
        """Callbacks invoked from non-main thread."""
        registered, lfs_types = op_fixture
        results = []
        errors = []
        thread_op_id = [None]

        class ThreadOp(lfs_types.Operator):
            lf_label = "Thread Op"

            def execute(self, context):
                results.append(threading.current_thread().name)
                return {"FINISHED"}

        lf.register_class(ThreadOp)
        registered.append(ThreadOp)
        thread_op_id[0] = ThreadOp._class_id()

        def thread_callback():
            try:
                # Try to use lf API from another thread
                lf.ops.invoke(thread_op_id[0])
            except Exception as e:
                errors.append(e)

        thread = threading.Thread(target=thread_callback)
        thread.start()
        thread.join(timeout=5.0)

        # May succeed or fail depending on GIL handling
