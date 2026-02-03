"""Training monitor using signals and training hooks.

Displays live training statistics, loss history plot, and auto-saves
checkpoints at configurable intervals.
"""

import lichtfeld as lf
from lfs_plugins.types import Panel
from lfs_plugins.props import PropertyGroup, IntProperty, BoolProperty
from lfs_plugins.ui.state import AppState
from lfs_plugins.ui.signals import Signal


class MonitorSettings(PropertyGroup):
    auto_save_interval = IntProperty(default=5000, min=500, max=50000, name="Auto-save Interval")
    auto_save_enabled = BoolProperty(default=False, name="Auto-save Enabled")


class TrainingMonitorPanel(Panel):
    label = "Training Monitor"
    space = "SIDE_PANEL"
    order = 40

    def __init__(self):
        self.settings = MonitorSettings.get_instance()
        self.best_loss = Signal(float("inf"), name="best_loss")
        self.best_iteration = Signal(0, name="best_iter")
        self.loss_history = []
        self.last_auto_save = 0

        AppState.loss.subscribe_as("training_monitor", self._on_loss_update)
        AppState.iteration.subscribe_as("training_monitor", self._on_iteration)

    def _on_loss_update(self, loss: float):
        if loss <= 0:
            return
        self.loss_history.append(loss)
        if loss < self.best_loss.value:
            self.best_loss.value = loss
            self.best_iteration.value = AppState.iteration.value

    def _on_iteration(self, iteration: int):
        if not self.settings.auto_save_enabled:
            return
        interval = self.settings.auto_save_interval
        if iteration - self.last_auto_save >= interval:
            lf.save_checkpoint()
            self.last_auto_save = iteration
            lf.log.info(f"Auto-saved checkpoint at iteration {iteration}")

    @classmethod
    def poll(cls, context) -> bool:
        return AppState.has_trainer.value

    def draw(self, layout):
        state = AppState.trainer_state.value

        # Status header
        if state == "running":
            layout.text_colored("Training", (0.3, 1.0, 0.3, 1.0))
        elif state == "paused":
            layout.text_colored("Paused", (1.0, 0.8, 0.2, 1.0))
        else:
            layout.label(f"State: {state}")

        # Progress
        iteration = AppState.iteration.value
        max_iter = AppState.max_iterations.value
        progress = iteration / max_iter if max_iter > 0 else 0.0
        layout.progress_bar(progress, f"{iteration:,} / {max_iter:,}")

        layout.separator()

        # Statistics
        layout.label(f"Loss: {AppState.loss.value:.6f}")
        layout.label(f"PSNR: {AppState.psnr.value:.2f} dB")
        layout.label(f"Gaussians: {AppState.num_gaussians.value:,}")

        best = self.best_loss.value
        if best < float("inf"):
            layout.label(f"Best Loss: {best:.6f} (iter {self.best_iteration.value})")

        # Loss plot
        if self.loss_history:
            recent = self.loss_history[-500:]
            layout.separator()
            layout.label("Loss History")
            scale_max = max(recent) * 1.1
            layout.plot_lines("##loss", recent, 0.0, scale_max, (0, 100))

        # Auto-save settings
        layout.separator()
        if layout.collapsing_header("Auto-save", default_open=False):
            layout.prop(self.settings, "auto_save_enabled")
            if self.settings.auto_save_enabled:
                layout.prop(self.settings, "auto_save_interval")
                if self.last_auto_save > 0:
                    layout.text_disabled(f"Last save: iter {self.last_auto_save}")

        # Manual controls
        layout.separator()
        if state == "running":
            if layout.button("Pause", (-1, 0)):
                lf.pause_training()
        elif state == "paused":
            if layout.button("Resume", (-1, 0)):
                lf.resume_training()

        if layout.button("Save Checkpoint", (-1, 0)):
            lf.save_checkpoint()
            lf.log.info("Checkpoint saved manually")


_classes = [TrainingMonitorPanel]
_post_step_handler = None


def _on_post_step():
    ctx = lf.context()
    if ctx.iteration % 100 == 0:
        lf.log.info(
            f"[Monitor] iter={ctx.iteration}, loss={ctx.loss:.6f}, "
            f"gaussians={ctx.num_gaussians}"
        )


def on_load():
    global _post_step_handler
    for cls in _classes:
        lf.register_class(cls)
    _post_step_handler = _on_post_step
    lf.on_post_step(_post_step_handler)
    lf.log.info("Training monitor loaded")


def on_unload():
    for cls in reversed(_classes):
        lf.unregister_class(cls)
    lf.log.info("Training monitor unloaded")
