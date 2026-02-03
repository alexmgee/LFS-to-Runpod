/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "py_cameras.hpp"

namespace lfs::python {

    void register_cameras(nb::module_& m) {
        // Camera class
        nb::class_<PyCamera>(m, "Camera")
            // Intrinsics
            .def_prop_ro("focal_x", &PyCamera::focal_x, "Focal length X in pixels")
            .def_prop_ro("focal_y", &PyCamera::focal_y, "Focal length Y in pixels")
            .def_prop_ro("center_x", &PyCamera::center_x, "Principal point X in pixels")
            .def_prop_ro("center_y", &PyCamera::center_y, "Principal point Y in pixels")
            .def_prop_ro("fov_x", &PyCamera::fov_x, "Horizontal field of view in radians")
            .def_prop_ro("fov_y", &PyCamera::fov_y, "Vertical field of view in radians")
            // Image info
            .def_prop_ro("image_width", &PyCamera::image_width, "Image width in pixels")
            .def_prop_ro("image_height", &PyCamera::image_height, "Image height in pixels")
            .def_prop_ro("camera_width", &PyCamera::camera_width, "Camera sensor width")
            .def_prop_ro("camera_height", &PyCamera::camera_height, "Camera sensor height")
            .def_prop_ro("image_name", &PyCamera::image_name, "Image filename")
            .def_prop_ro("image_path", &PyCamera::image_path, "Full path to image file")
            .def_prop_ro("mask_path", &PyCamera::mask_path, "Full path to mask file")
            .def_prop_ro("has_mask", &PyCamera::has_mask, "Whether a mask file exists")
            .def_prop_ro("uid", &PyCamera::uid, "Unique camera identifier")
            // Transforms
            .def_prop_ro("R", &PyCamera::R, "Rotation matrix [3, 3]")
            .def_prop_ro("T", &PyCamera::T, "Translation vector [3]")
            .def_prop_ro("K", &PyCamera::K, "Intrinsic matrix [3, 3]")
            .def_prop_ro("world_view_transform", &PyCamera::world_view_transform,
                         "World-to-view transform [4, 4]")
            .def_prop_ro("cam_position", &PyCamera::cam_position,
                         "Camera position in world space [3]")
            // Load methods
            .def("load_image", &PyCamera::load_image,
                 nb::arg("resize_factor") = 1, nb::arg("max_width") = 3840,
                 "Load image as tensor [C, H, W] on CUDA")
            .def("load_mask", &PyCamera::load_mask,
                 nb::arg("resize_factor") = 1, nb::arg("max_width") = 3840,
                 nb::arg("invert") = false, nb::arg("threshold") = 0.5f,
                 "Load mask as tensor [1, H, W] on CUDA");

        // CameraDataset class
        nb::class_<PyCameraDataset>(m, "CameraDataset")
            .def("__len__", &PyCameraDataset::size, "Number of cameras")
            .def("__getitem__", &PyCameraDataset::get, nb::arg("index"), "Get camera by index")
            .def("get_camera_by_filename", &PyCameraDataset::get_camera_by_filename,
                 nb::arg("filename"), "Find camera by image filename")
            .def("cameras", &PyCameraDataset::cameras,
                 "Get all cameras as a list")
            .def("set_resize_factor", &PyCameraDataset::set_resize_factor,
                 nb::arg("factor"), "Set image resize factor for all cameras")
            .def("set_max_width", &PyCameraDataset::set_max_width,
                 nb::arg("width"), "Set maximum image width for all cameras");
    }

} // namespace lfs::python
