# Python API Issues

Issues discovered during documentation review comparing `docs/PYTHON_API.md` against the actual C++ implementations.

---

## Critical Issues

### 1. `repeat()` Not Bound to Python

**Location:** `src/python/lfs/py_tensor.cpp`

The `repeat()` function is fully implemented in C++ (lines 1045-1075) and declared in the header (line 177), but **not registered** in the nanobind bindings. No `.def("repeat", ...)` call exists.

**Fix Required:** Add binding at ~line 1574:
```cpp
.def("repeat", &PyTensor::repeat, nb::arg("repeats"), "Tile the tensor")
```

---

### 2. `linspace()` Ignores dtype Parameter

**Location:** `src/python/lfs/py_tensor.cpp:1302-1306`

The `dtype` parameter is accepted but ignored in the implementation:
```cpp
PyTensor PyTensor::linspace(float start, float end, int64_t steps,
                            const std::string& device,
                            const std::string& dtype) {
    // dtype is never used!
    return PyTensor(Tensor::linspace(start, end, static_cast<size_t>(steps), parse_device(device)));
}
```

**Fix Required:** Pass dtype to underlying `Tensor::linspace()` or document the limitation.

---

### 3. `packages.uninstall_async()` Not Implemented

**Location:** `src/python/lfs/py_packages.cpp`

Function was previously documented but does not exist in the implementation. Only `install_async()` is implemented.

**Status:** Removed from documentation. If needed, implement the function.

---

## Implementation Gaps

### UI Framework - Functions Not Implemented

These were documented but do not exist in `src/python/lfs/py_ui.cpp`:

| Function | Status |
|----------|--------|
| `push_style_color()` | Not implemented |
| `pop_style_color()` | Not implemented |
| `get_style_color()` | Not implemented |
| `set_style_color()` | Not implemented |
| `push_style_var_float()` | Not implemented |
| `push_style_var_vec2()` | Not implemented |
| `pop_style_var()` | Not implemented |
| `is_key_pressed()` | Not implemented |
| `is_mouse_down()` | Not implemented |
| `begin_disabled()` | Not implemented |
| `end_disabled()` | Not implemented |

**Status:** Removed from documentation. Consider implementing if needed.

---

## Documentation Corrections Made

### Tensor API

| Issue | Correction |
|-------|------------|
| Factory functions at module level | Changed `lf.zeros()` to `lf.Tensor.zeros()` etc. |
| `lf.cat()`, `lf.stack()`, `lf.where()` | Changed to `lf.Tensor.cat()` etc. |
| Missing type extraction methods | Added `t.float_()`, `t.int_()`, `t.bool_()` |
| `clamp()` parameters optional | Clarified both min AND max are required |
| `repeat()` documented | Removed (not bound) |

### I/O Operations - Added Missing Functions

| Function | Signature |
|----------|-----------|
| `lf.io.save_ply()` | `save_ply(data, path, binary=True, progress=None)` |
| `lf.io.save_sog()` | `save_sog(data, path, kmeans_iterations=10, use_gpu=True, progress=None)` |
| `lf.io.save_spz()` | `save_spz(data, path)` |
| `lf.io.export_html()` | `export_html(data, path, kmeans_iterations=10, progress=None)` |

### Rendering - Added Missing Functions

| Function | Description |
|----------|-------------|
| `lf.get_viewport_render()` | Get current viewport render |
| `lf.capture_viewport()` | Capture viewport (cloned for async use) |
| `lf.get_current_view()` | Get current viewport camera info |
| `lf.get_render_scene()` | Get current render scene |

### UI Framework - Added Missing Functions

| Function | Description |
|----------|-------------|
| `slider_float2()`, `slider_float3()` | Multi-component sliders |
| `color_button()` | Color button widget |
| `path_input()` | File/folder path input with dialog |
| `spacing()` | Add vertical spacing |
| `set_next_item_width()` | Set width of next item |
| `collapsing_header()` | Collapsible section header |
| `tree_node()`, `tree_pop()` | Tree view widgets |
| `begin_table()`, `end_table()` | Table container |
| `table_next_row()`, `table_next_column()` | Table navigation |
| `table_set_column_index()` | Jump to specific column |

### Plugin System - Added Missing Functions

| Function | Description |
|----------|-------------|
| `plugins.create()` | Create plugin from template |
| `plugins.check_updates()` | Check for plugin updates |

### Package Management - Corrections

| Change | Details |
|--------|---------|
| Removed `uninstall_async()` | Not implemented |
| Added `install_torch_async()` | Background PyTorch installation |
| Added `is_busy()` | Check if async operation running |

### Scene/Camera API - Minor Fixes

| Issue | Correction |
|-------|------------|
| CropBox tuple types | Added `tuple[float, float, float]` annotations |
| PointCloud optional properties | Added `# Optional[PyTensor]` comments |
| Camera dataset return types | Clarified returns `PyCameraDataset` |

---

## Recommendations

### High Priority

1. **Bind `repeat()` function** - Implementation exists, just needs registration
2. **Fix `linspace()` dtype** - Either implement or document limitation

### Medium Priority

3. **Implement UI styling functions** - If style customization is needed
4. **Implement UI input functions** - `is_key_pressed()`, `is_mouse_down()` for interactive plugins
5. **Implement `packages.uninstall_async()`** - For consistency with `install_async()`

### Low Priority

6. **Add `begin_disabled()`/`end_disabled()`** - For conditional UI disabling
7. **Consider module-level tensor factory functions** - For convenience (`lf.zeros()` instead of `lf.Tensor.zeros()`)

---

## Files Reviewed

| File | Purpose |
|------|---------|
| `src/python/lfs/py_tensor.cpp` | Tensor class bindings |
| `src/python/lfs/py_scene.cpp` | Scene graph bindings |
| `src/python/lfs/py_cameras.cpp` | Camera API bindings |
| `src/python/lfs/py_io.cpp` | I/O operations |
| `src/python/lfs/py_rendering.cpp` | Rendering functions |
| `src/python/lfs/py_ui.cpp` | UI framework |
| `src/python/lfs/py_plugins.cpp` | Plugin system |
| `src/python/lfs/py_packages.cpp` | Package management |
| `src/python/lfs/module.cpp` | Main module, training hooks, logging |
