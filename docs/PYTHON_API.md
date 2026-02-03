# LichtFeld Studio Python API

Python integration for LichtFeld Studio built on **nanobind**, providing scripting, automation, and plugin development capabilities.

## Table of Contents

- [Installation & Setup](#installation--setup)
- [Tensor API](#tensor-api)
- [Scene Graph API](#scene-graph-api)
- [Camera API](#camera-api)
- [I/O Operations](#io-operations)
- [Rendering](#rendering)
- [Training Control](#training-control)
- [UI Framework](#ui-framework)
- [Plugin System](#plugin-system)
- [Package Management](#package-management)
- [Logging](#logging)
- [Advanced Topics](#advanced-topics)

---

## Installation & Setup

Enable Python bindings in CMake:

```bash
cmake -DBUILD_PYTHON_BINDINGS=ON ..
```

```python
import lichtfeld as lf
```

---

## Tensor API

The `Tensor` class provides GPU-accelerated tensor operations with NumPy interoperability.

### Creating Tensors

```python
# From NumPy
arr = np.array([[1, 2], [3, 4]], dtype=np.float32)
t = lf.Tensor.from_numpy(arr)                    # Copy to GPU
t = lf.Tensor.from_numpy(arr, copy=False)        # Share memory (CPU only)

# Factory functions (all use lf.Tensor.* prefix)
zeros = lf.Tensor.zeros([3, 4])                  # 3x4 zeros on CUDA
ones = lf.Tensor.ones([3, 4], device="cpu")      # 3x4 ones on CPU
full = lf.Tensor.full([2, 2], 5.0)               # 2x2 filled with 5.0
eye = lf.Tensor.eye(3)                           # 3x3 identity matrix
rand = lf.Tensor.rand([10, 10])                  # Uniform random [0, 1)
randn = lf.Tensor.randn([10, 10])                # Normal distribution
arange = lf.Tensor.arange(0, 10, 2)              # [0, 2, 4, 6, 8]
linspace = lf.Tensor.linspace(0, 1, 5)           # [0, 0.25, 0.5, 0.75, 1]
                                                 # Note: dtype parameter is currently ignored
randint = lf.Tensor.randint(0, 10, [5, 5])       # Random integers
empty = lf.Tensor.empty([100, 100])              # Uninitialized
```

### Data Types

Supported dtypes: `float32`, `float16`, `int32`, `int64`, `uint8`, `bool`

```python
t = lf.Tensor.zeros([3, 3], dtype="float16")
t = t.to("int32")  # Type conversion

# Type extraction methods (in-place, returns self)
t.float_()  # Convert to float32
t.int_()    # Convert to int32
t.bool_()   # Convert to bool
```

### Properties

| Property | Description |
|----------|-------------|
| `t.shape` | Tuple of dimensions |
| `t.ndim` | Number of dimensions |
| `t.numel` | Total number of elements |
| `t.device` | "cuda" or "cpu" |
| `t.dtype` | Data type string |
| `t.is_cuda` | True if on GPU |
| `t.is_contiguous` | True if memory is contiguous |

### Arithmetic Operations

```python
c = a + b; c = a - b; c = a * b; c = a / b  # Element-wise
c = a + 5.0; c = a * 2.0; c = 10.0 - a      # Scalar
a += b; a *= 2.0                             # In-place
```

### Mathematical Functions

```python
# Unary functions
t.exp(), t.log(), t.sqrt(), t.abs(), t.neg()

# Trigonometric
t.sin(), t.cos(), t.tan(), t.asin(), t.acos(), t.atan()
t.sinh(), t.cosh(), t.tanh()

# Activation functions
t.sigmoid(), t.relu(), t.gelu(), t.swish()

# Other math
t.floor(), t.ceil(), t.round(), t.trunc()
t.sign(), t.reciprocal(), t.rsqrt()
t.square(), t.log2(), t.log10(), t.log1p(), t.exp2()
t.pow(2.0)
t.isnan(), t.isinf(), t.isfinite()
```

### Reduction Operations

```python
t.sum(), t.sum_scalar()          # Sum (tensor / float)
t.mean(), t.mean_scalar()        # Mean
t.max(), t.max_scalar()          # Max
t.min(), t.min_scalar()          # Min
t.prod()                         # Product
t.std(), t.var()                 # Standard deviation, variance
t.norm(p=2.0)                    # Lp norm
t.all(), t.any()                 # Boolean reductions

# Dimension-wise
t.sum(dim=0)
t.mean(dim=1, keepdim=True)
t.argmax(dim=0), t.argmin(dim=1)
```

### Shape Operations

```python
t.reshape([2, 6])          # New shape (may copy)
t.view([2, 6])             # New shape (shares memory)
t.flatten()                # Flatten to 1D
t.flatten(start_dim=1)     # Flatten from dim 1
t.squeeze(), t.squeeze(dim=0)
t.unsqueeze(dim=0)
t.expand([3, 4, 5])        # Broadcast to new shape
t.t()                      # 2D transpose
t.transpose(0, 1)          # Swap dimensions
t.permute([2, 0, 1])       # Reorder dimensions
```

### Indexing

```python
t[0], t[1:3], t[:, 0], t[..., -1]                    # Basic indexing
t.index_select(dim=0, indices=indices)
t.gather(dim=1, indices=indices)
mask = t > 0.5
t.masked_select(mask), t.masked_fill(mask, 0.0)
t.nonzero()
t[0] = 1.0; t[:, 0] = other_tensor                   # Assignment
```

### Linear Algebra

```python
a.matmul(b), a @ b         # Matrix multiplication
a.mm(b)                    # 2D matrix multiply
a.bmm(b)                   # Batched matrix multiply
a.dot(b)                   # Dot product
```

### Element-wise Operations

```python
t.clamp(min=0.0, max=1.0)  # Clamp values (both min AND max required)
t.maximum(other)           # Element-wise max
t.minimum(other)           # Element-wise min
lf.Tensor.where(cond, x, y)  # Conditional selection
```

### Memory Operations

```python
t.clone()              # Deep copy
t.contiguous()         # Ensure contiguous memory
t.cpu(), t.cuda()      # Move between devices
t.sync()               # Synchronize CUDA operations
t.item()               # Extract scalar value
t.size(dim)            # Get size of dimension
```

### NumPy Interoperability

```python
arr = t.numpy()                   # Copy to NumPy
arr = t.numpy(copy=False)         # Share memory (CPU tensors only)
arr = np.asarray(t)               # Via __array__ protocol
t = lf.Tensor.from_numpy(arr)
```

### DLPack Protocol (Zero-Copy)

```python
capsule = t.__dlpack__()
device = t.__dlpack_device__()
t = lf.Tensor.from_dlpack(other_tensor)

# PyTorch interop
torch_tensor = torch.from_dlpack(t)
lf_tensor = lf.Tensor.from_dlpack(torch_tensor)
```

### Combining Tensors

```python
c = lf.Tensor.cat([a, b, c], dim=0)    # Concatenation
s = lf.Tensor.stack([a, b, c], dim=0)  # Stacking
```

---

## Scene Graph API

Manages all objects in the 3D scene including Gaussian splats, point clouds, cameras, and groups.

### Accessing the Scene

```python
scene = lf.get_scene()
lf.list_scene()  # Print scene tree to console
```

### Scene Properties

| Property | Description |
|----------|-------------|
| `scene.node_count` | Total number of nodes |
| `scene.total_gaussian_count` | Total gaussians in scene |
| `scene.has_nodes()` | True if scene has any nodes |
| `scene.has_training_data()` | True if training model exists |
| `scene.scene_center` | PyTensor [3] scene center |

### Adding Nodes

```python
group_id = scene.add_group("MyGroup")
child_group_id = scene.add_group("ChildGroup", parent=group_id)

splat_id = scene.add_splat(
    name="MySplat",
    means=means_tensor,        # [N, 3]
    sh0=sh0_tensor,            # [N, 1, 3]
    shN=shN_tensor,            # [N, (degree+1)^2-1, 3]
    scaling=scaling_tensor,    # [N, 3]
    rotation=rotation_tensor,  # [N, 4]
    opacity=opacity_tensor,    # [N, 1]
    sh_degree=3,
    scene_scale=1.0,
    parent=group_id            # Optional parent
)
```

### Querying Nodes

```python
node = scene.get_node("NodeName")
node = scene.get_node_by_id(node_id)
all_nodes = scene.get_nodes()
visible = scene.get_visible_nodes()
roots = scene.root_nodes()
is_visible = scene.is_node_effectively_visible(node_id)
```

### Node Operations

```python
scene.remove_node("NodeName")
scene.remove_node("NodeName", keep_children=True)
scene.rename_node("OldName", "NewName")
scene.reparent(node_id, new_parent_id)
scene.duplicate_node("NodeName")
scene.merge_group("GroupName")
scene.clear()
```

### Transforms

```python
transform = scene.get_world_transform(node_id)  # 4x4 matrix as nested tuples
scene.set_node_transform("NodeName", transform_tuple)
scene.set_node_transform("NodeName", transform_tensor)  # PyTensor 4x4
```

### Node Properties

| Property | Description |
|----------|-------------|
| `node.id` | Unique ID |
| `node.parent_id` | Parent node ID (-1 if root) |
| `node.children` | List of child IDs |
| `node.type` | NodeType enum |
| `node.name` | Node name |
| `node.gaussian_count` | Number of gaussians (splat nodes) |
| `node.centroid` | Center point (splat nodes) |
| `node.visible` | Visibility flag (read-write) |
| `node.locked` | Lock flag (read-write) |
| `node.local_transform` | Local transform (tuple) |
| `node.world_transform` | World transform (tuple) |

```python
splat_data = node.splat_data()      # PySplatData or None
point_cloud = node.point_cloud()    # PyPointCloud or None
cropbox = node.cropbox()            # PyCropBox or None

# Camera nodes only
node.camera_uid, node.image_path, node.mask_path
node.has_camera                   # True if camera data available
node.camera_R, node.camera_T      # Rotation [3,3], Translation [3,1]
node.camera_focal_x, node.camera_focal_y  # Focal lengths in pixels
node.camera_width, node.camera_height     # Image dimensions
```

### Node Types

```python
from lichtfeld import NodeType
# SPLAT, POINTCLOUD, GROUP, CROPBOX, DATASET, CAMERA_GROUP, CAMERA, IMAGE_GROUP, IMAGE
```

### Training Model

```python
combined = scene.combined_model()
training = scene.training_model()
scene.set_training_model_node("SplatName")
name = scene.training_model_node_name
```

### Bounds

```python
bounds = scene.get_node_bounds(node_id)
if bounds:
    (min_x, min_y, min_z), (max_x, max_y, max_z) = bounds
center = scene.get_node_bounds_center(node_id)
```

### CropBox Operations

```python
cropbox_id = scene.get_cropbox_for_splat(splat_id)
cropbox_id = scene.get_or_create_cropbox_for_splat(splat_id)
cropbox = scene.get_cropbox_data(cropbox_id)
cropbox.min = (-1.0, -1.0, -1.0)  # tuple[float, float, float]
cropbox.max = (1.0, 1.0, 1.0)     # tuple[float, float, float]
cropbox.enabled = True
cropbox.inverse = False
cropbox.color = (1.0, 0.0, 0.0)   # tuple[float, float, float]
cropbox.line_width = 2.0
scene.set_cropbox_data(cropbox_id, cropbox)
```

### Selection System

```python
scene.set_selection(indices_tensor)
scene.set_selection_mask(bool_tensor)
scene.clear_selection()
has_sel = scene.has_selection()
mask = scene.selection_mask

# Selection groups
group_id = scene.add_selection_group("Region1", (1.0, 0.0, 0.0))
scene.remove_selection_group(group_id)
scene.rename_selection_group(group_id, "NewName")
scene.set_selection_group_color(group_id, (0.0, 1.0, 0.0))
scene.set_selection_group_locked(group_id, True)
is_locked = scene.is_selection_group_locked(group_id)
scene.active_selection_group = group_id

for group in scene.selection_groups():
    print(f"{group.name}: {group.count} points, color={group.color}")
```

### Splat Data (PySplatData)

```python
splat = scene.combined_model()

# Raw tensor access (views, modify in-place)
means = splat.means_raw           # [N, 3]
sh0 = splat.sh0_raw               # [N, 1, 3]
shN = splat.shN_raw               # [N, (degree+1)^2-1, 3]
scaling = splat.scaling_raw       # [N, 3] log-space
rotation = splat.rotation_raw     # [N, 4] quaternions
opacity = splat.opacity_raw       # [N, 1] logit-space

# Computed getters (with activations applied)
means = splat.get_means()
opacity = splat.get_opacity()     # Sigmoid applied
rotation = splat.get_rotation()   # Normalized
scaling = splat.get_scaling()     # Exp applied
shs = splat.get_shs()             # Concatenated SH coefficients

# Properties
splat.num_points
splat.active_sh_degree
splat.max_sh_degree
splat.scene_scale
splat.visible_count()

# SH degree control
splat.increment_sh_degree()
splat.set_active_sh_degree(2)
splat.set_max_sh_degree(3)

# Soft deletion
splat.soft_delete(mask_tensor)    # Returns previous state
splat.undelete(mask_tensor)
splat.clear_deleted()
removed_count = splat.apply_deleted()  # Permanently remove
splat.has_deleted_mask()
deleted = splat.deleted           # Boolean tensor

# Memory
splat.reserve_capacity(100000)
```

### Point Cloud (PyPointCloud)

```python
pc = node.point_cloud()

# Properties (tensors, some may be None)
pc.means
pc.colors     # Optional[PyTensor]
pc.normals    # Optional[PyTensor]
pc.sh0        # Optional[PyTensor]
pc.shN        # Optional[PyTensor]
pc.opacity    # Optional[PyTensor]
pc.scaling    # Optional[PyTensor]
pc.rotation   # Optional[PyTensor]

# Metadata
pc.size
pc.is_gaussian()
pc.attribute_names

# Operations
pc.normalize_colors()
removed = pc.filter(keep_mask)
removed = pc.filter_indices(indices)
```

---

## Camera API

### Camera Dataset

```python
train_cams = lf.train_cameras()  # Returns PyCameraDataset
val_cams = lf.val_cameras()      # Returns PyCameraDataset

for i in range(len(train_cams)):
    cam = train_cams[i]

for cam in train_cams.cameras():
    print(cam.image_name)

cam = train_cams.get_camera_by_filename("image001.jpg")
train_cams.set_resize_factor(2)
train_cams.set_max_width(1920)
```

### Camera Properties

| Property | Description |
|----------|-------------|
| `cam.focal_x`, `cam.focal_y` | Focal lengths |
| `cam.center_x`, `cam.center_y` | Principal point |
| `cam.fov_x`, `cam.fov_y` | Field of view |
| `cam.image_width`, `cam.image_height` | Original size |
| `cam.camera_width`, `cam.camera_height` | After resize |
| `cam.image_name`, `cam.image_path` | Image info |
| `cam.mask_path`, `cam.has_mask` | Mask info |
| `cam.uid` | Unique identifier |
| `cam.R` | [3, 3] rotation (NumPy) |
| `cam.T` | [3] translation (NumPy) |
| `cam.K` | [3, 3] intrinsics (NumPy) |
| `cam.world_view_transform` | [4, 4] (NumPy) |
| `cam.cam_position` | [3] world position (NumPy) |

### Loading Images

```python
image = cam.load_image()                    # [C, H, W] CUDA tensor
image = cam.load_image(resize_factor=2)
image = cam.load_image(max_width=1920)
mask = cam.load_mask()                      # [1, H, W]
mask = cam.load_mask(invert=True)
mask = cam.load_mask(threshold=0.5)
```

---

## I/O Operations

### Loading Data

```python
result = lf.io.load("path/to/file.ply")

result = lf.io.load(
    "path/to/dataset",
    resize_factor=2,
    images_folder="images/",
    progress=lambda p, msg: print(f"{p:.1f}%: {msg}")
)
```

### Load Result

| Property | Description |
|----------|-------------|
| `result.splat_data` | PySplatData or None |
| `result.scene_center` | PyTensor [3] |
| `result.loader_used` | Loader name string |
| `result.load_time_ms` | Load time in milliseconds |
| `result.warnings` | List of warning strings |
| `result.is_dataset` | True if loaded a dataset |

### Saving Data

```python
lf.io.save_ply(data, path, binary=True, progress=None)
lf.io.save_sog(data, path, kmeans_iterations=10, use_gpu=True, progress=None)
lf.io.save_spz(data, path)
lf.io.export_html(data, path, kmeans_iterations=10, progress=None)
```

### Format Information

```python
formats = lf.io.get_supported_formats()      # e.g., ["PLY", "Splat"]
extensions = lf.io.get_supported_extensions() # e.g., [".ply", ".splat"]
is_dataset = lf.io.is_dataset_path("path/to/check")
```

### Opening in UI

```python
lf.app.open("path/to/file.ply")
```

---

## Rendering

### Render a View

```python
image = lf.render_view(
    rotation=rotation,           # [3, 3] rotation matrix
    translation=translation,     # [3] translation vector
    width=1920,
    height=1080,
    fov_degrees=60.0,
    bg_color=bg_tensor           # Optional [3] RGB
)
# Returns: PyTensor [H, W, 3] RGB image or None
```

### Viewport Functions

```python
lf.get_viewport_render()    # Get current viewport render
lf.capture_viewport()       # Capture viewport (cloned for async use)
lf.get_current_view()       # Get current viewport camera info
lf.get_render_scene()       # Get current render scene
```

### Compute Screen Positions

```python
positions = lf.compute_screen_positions(
    rotation=rotation,
    translation=translation,
    width=1920,
    height=1080,
    fov_degrees=60.0
)
# Returns: PyTensor [N, 2] screen coordinates or None
```

---

## Training Control

### Training Context

```python
ctx = lf.context()
ctx.iteration, ctx.max_iterations
ctx.loss, ctx.num_gaussians
ctx.is_training, ctx.is_paused, ctx.is_refining
ctx.phase, ctx.strategy
ctx.refresh()
```

### Gaussian Info

```python
g = lf.gaussians()
g.count, g.sh_degree, g.max_sh_degree
```

### Training Session

```python
session = lf.session()
session.pause(), session.resume(), session.request_stop()

opt = session.optimizer()
opt.set_lr(0.001), opt.scale_lr(0.5), opt.get_lr()

model = session.model()
model.clamp("opacity", min=0.0, max=1.0)
model.scale("scaling", factor=0.9)
model.set("rotation_w", value=1.0)
```

### Training Hooks (Decorators)

```python
@lf.on_training_start
def on_start(ctx): pass

@lf.on_iteration_start
def on_iter(ctx): pass

@lf.on_pre_optimizer_step
def before_opt(ctx): pass

@lf.on_post_step
def after_step(ctx): pass

@lf.on_training_end
def on_end(ctx): pass
```

### Hook Context Dictionary

```python
{'iter': int, 'loss': float, 'num_splats': int, 'is_refining': bool}
```

### Scoped Handlers (RAII Pattern)

```python
handler = lf.ScopedHandler()
handler.on_training_start(callback)
handler.on_iteration_start(callback)
handler.on_post_step(callback)
handler.clear()  # Or let handler go out of scope
```

---

## UI Framework

ImGui-backed widgets for creating custom panels.

### Text Elements

```python
ui.label("Simple text")
ui.heading("Section Heading")
ui.text_colored("Warning!", (1.0, 0.5, 0.0, 1.0))
ui.text_selectable("Selectable text", height=100.0)
ui.bullet_text("Bullet point")
```

### Interactive Controls

```python
if ui.button("Click Me", (100, 30)): pass
if ui.small_button("Small"): pass

changed, value = ui.checkbox("Enable", self.enabled)
changed, value = ui.radio_button("Option A", self.option == "A")
changed, value = ui.input_float("Scale", self.scale, step=0.1)
changed, value = ui.input_int("Count", self.count)
changed, value = ui.input_text("Name", self.name, max_size=128)
changed, idx = ui.combo("Select", options, self.selected_idx)
changed, idx = ui.listbox("Items", items, self.selected)
ui.color_button()
ui.path_input()
```

### Sliders

```python
changed, value = ui.slider_float("Value", current, min, max)
changed, value = ui.slider_float2("Vec2", current, min, max)
changed, value = ui.slider_float3("Vec3", current, min, max)
changed, value = ui.slider_int("Int", current, min, max)
```

### Layout

```python
ui.separator()
ui.new_line()
ui.same_line()
ui.spacing()
ui.set_next_item_width(width)
ui.indent(20.0)
ui.unindent(20.0)

ui.group_begin()
ui.group_end()

ui.push_id("unique_section")
ui.pop_id()
```

### Tree/Collapsing

```python
if ui.collapsing_header("Section"):
    ui.label("Content")

if ui.tree_node("Node"):
    ui.label("Content")
    ui.tree_pop()
```

### Tables

```python
if ui.begin_table("my_table", num_columns, flags):
    ui.table_next_row()
    ui.table_next_column()
    ui.label("Cell 1")
    ui.table_next_column()
    ui.label("Cell 2")
    ui.table_set_column_index(0)  # Jump to column
    ui.end_table()
```

### Tooltips

```python
ui.button("Hover me")
ui.set_item_tooltip("This is a tooltip")
```

### Application State

```python
ui.is_training()
ui.iteration()
ui.loss()
ui.has_scene()
ui.num_gaussians()
ui.has_selection()
```

---

## Plugin System

### Plugin Structure

Create a folder with `plugin.toml`:

```toml
[plugin]
name = "my_plugin"
version = "0.1.0"
description = "My custom plugin"
author = "Your Name"
entry_point = "__init__"
dependencies = ["numpy"]
auto_start = true
hot_reload = true
min_lichtfeld_version = "0.1.0"
```

### Plugin Entry Point

```python
from lichtfeld.plugins import Capability, PluginContext

@Capability("my_capability")
def my_feature(ctx: PluginContext):
    scene = ctx.scene
    pass

def on_load():
    print("Plugin loaded!")

def on_unload():
    print("Plugin unloaded!")
```

### Plugin Manager

```python
from lichtfeld import plugins

plugins.discover()
plugins.load("my_plugin")
plugins.load_all()
plugins.unload("my_plugin")
plugins.reload("my_plugin")
plugins.create()           # Create plugin from template
plugins.check_updates()    # Check for plugin updates

loaded = plugins.list_loaded()
state = plugins.get_state("my_plugin")
error = plugins.get_error("my_plugin")
traceback = plugins.get_traceback("my_plugin")

plugins.start_watcher()
plugins.stop_watcher()
```

### Installing Plugins

```python
name = plugins.install("https://github.com/user/plugin.git")
plugins.update("my_plugin")
plugins.uninstall("my_plugin")

plugins.search("gaussian")
plugins.install_from_registry("plugin_id", "1.0.0")
```

---

## Package Management

LichtFeld uses `uv` for Python package management.

```python
from lichtfeld import packages

venv_path = packages.init()

# Install packages
packages.install("numpy")
packages.install("torch>=2.0")
packages.install_async("large_package")

# Uninstall
packages.uninstall("package_name")

# Query packages
is_installed = packages.is_installed("numpy")
pkg_list = packages.list()
for pkg in pkg_list:
    print(f"{pkg.name}=={pkg.version} at {pkg.path}")

# Install PyTorch with CUDA
packages.install_torch(cuda="auto")
packages.install_torch(cuda="12.1")
packages.install_torch(version="2.1.0")
packages.install_torch_async()  # Background install

# Status
packages.is_busy()  # Check if async operation running

# Utilities
site_packages = packages.site_packages_dir()
uv_available = packages.is_uv_available()
```

---

## Logging

```python
lf.log.debug("Debug message")
lf.log.info("Info message")
lf.log.warn("Warning message")
lf.log.error("Error message")
```

---

## Advanced Topics

### Transform Utilities

```python
transform = lf.mat4([
    [1, 0, 0, tx],
    [0, 1, 0, ty],
    [0, 0, 1, tz],
    [0, 0, 0, 1]
])
```

### Animation Callbacks

```python
@lf.on_frame
def animate(delta_time):
    pass

lf.stop_animation()
```

### Running Scripts

```python
lf.run("path/to/script.py")
```

### Memory Management

- Tensors track ownership with `owns_data` flag
- Scene/model references are non-owning (views)
- DLPack provides zero-copy interop
- Use `sync()` to ensure CUDA operations complete

### Thread Safety

- GIL is acquired in callbacks automatically
- Scene context uses thread-local storage during training
- Hook callbacks run on the training thread

---

## API Reference Summary

| Module | Description |
|--------|-------------|
| `lf.Tensor` | GPU-accelerated tensor operations |
| `lf.get_scene()` | Access scene graph |
| `lf.io.load()` | Load datasets and models |
| `lf.io.save_*()` | Save to various formats |
| `lf.render_view()` | Render from camera |
| `lf.context()` | Training state |
| `lf.session()` | Training control |
| `lf.on_*` | Training hook decorators |
| `lf.log.*` | Logging functions |
| `lf.app.open()` | Open file in UI |
| `lf.packages.*` | Package management |
| `lf.plugins.*` | Plugin system |

---

## Version Information

- **Binding Framework**: nanobind (STABLE_ABI)
- **Python Support**: Python 3.x
- **CUDA Support**: Required for GPU tensors
- **Type Stubs**: Generated for IDE support
