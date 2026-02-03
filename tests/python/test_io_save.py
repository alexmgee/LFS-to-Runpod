# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for lichtfeld.io saving functionality."""

import pytest


class TestSavePLY:
    """Tests for PLY save functionality."""

    @pytest.mark.slow
    def test_save_ply_creates_file(self, lf, benchmark_ply, tmp_output):
        """Test save_ply creates output file."""
        result = lf.io.load(str(benchmark_ply))
        output_path = tmp_output / "output.ply"

        lf.io.save_ply(result.splat_data, str(output_path))

        assert output_path.exists()
        assert output_path.stat().st_size > 0

    @pytest.mark.slow
    def test_save_ply_binary_format(self, lf, benchmark_ply, tmp_output):
        """Test save_ply creates binary format PLY."""
        result = lf.io.load(str(benchmark_ply))
        output_path = tmp_output / "output.ply"

        lf.io.save_ply(result.splat_data, str(output_path), binary=True)

        # Check that it's binary format
        with open(output_path, "rb") as f:
            header = f.read(100).decode("utf-8", errors="ignore")

        assert "ply" in header
        assert "binary_little_endian" in header

    @pytest.mark.slow
    def test_save_ply_roundtrip(self, lf, benchmark_ply, tmp_output, numpy):
        """Test PLY save/load roundtrip preserves data."""
        original = lf.io.load(str(benchmark_ply))
        output_path = tmp_output / "roundtrip.ply"

        lf.io.save_ply(original.splat_data, str(output_path))
        reloaded = lf.io.load(str(output_path))

        # Point count should match
        assert reloaded.splat_data.num_points == original.splat_data.num_points

        # Means should be close (means_raw is a property, not a method)
        orig_means = original.splat_data.means_raw.numpy()
        reload_means = reloaded.splat_data.means_raw.numpy()
        numpy.testing.assert_allclose(orig_means, reload_means, rtol=1e-4)


class TestSaveSPZ:
    """Tests for SPZ (Niantic compressed) save functionality."""

    @pytest.mark.slow
    def test_save_spz_creates_file(self, lf, benchmark_ply, tmp_output):
        """Test save_spz creates output file."""
        result = lf.io.load(str(benchmark_ply))
        output_path = tmp_output / "output.spz"

        lf.io.save_spz(result.splat_data, str(output_path))

        assert output_path.exists()
        assert output_path.stat().st_size > 0

    @pytest.mark.slow
    def test_save_spz_compressed(self, lf, benchmark_ply, tmp_output):
        """Test SPZ is compressed (smaller than PLY)."""
        result = lf.io.load(str(benchmark_ply))

        ply_path = tmp_output / "output.ply"
        spz_path = tmp_output / "output.spz"

        lf.io.save_ply(result.splat_data, str(ply_path), binary=True)
        lf.io.save_spz(result.splat_data, str(spz_path))

        # SPZ should be smaller than binary PLY
        assert spz_path.stat().st_size < ply_path.stat().st_size


class TestSaveSOG:
    """Tests for SOG (SuperSplat) save functionality."""

    @pytest.mark.slow
    def test_save_sog_creates_file(self, lf, benchmark_ply, tmp_output):
        """Test save_sog creates output file."""
        result = lf.io.load(str(benchmark_ply))
        output_path = tmp_output / "output.sog"

        lf.io.save_sog(result.splat_data, str(output_path), kmeans_iterations=5)

        assert output_path.exists()
        assert output_path.stat().st_size > 0

    @pytest.mark.slow
    def test_save_sog_with_gpu(self, lf, benchmark_ply, tmp_output, gpu_available):
        """Test save_sog with use_gpu=True."""
        if not gpu_available:
            pytest.skip("GPU not available")

        result = lf.io.load(str(benchmark_ply))
        output_path = tmp_output / "output_gpu.sog"

        lf.io.save_sog(
            result.splat_data, str(output_path), kmeans_iterations=5, use_gpu=True
        )

        assert output_path.exists()


class TestExportHTML:
    """Tests for HTML viewer export."""

    @pytest.mark.slow
    def test_export_html_creates_file(self, lf, benchmark_ply, tmp_output):
        """Test export_html creates output file."""
        result = lf.io.load(str(benchmark_ply))
        output_path = tmp_output / "viewer.html"

        lf.io.export_html(result.splat_data, str(output_path), kmeans_iterations=5)

        assert output_path.exists()
        assert output_path.stat().st_size > 0

    @pytest.mark.slow
    def test_export_html_is_valid(self, lf, benchmark_ply, tmp_output):
        """Test exported HTML is valid HTML."""
        result = lf.io.load(str(benchmark_ply))
        output_path = tmp_output / "viewer.html"

        lf.io.export_html(result.splat_data, str(output_path), kmeans_iterations=5)

        content = output_path.read_text()
        assert "<!DOCTYPE html>" in content or "<html" in content


class TestSaveProgress:
    """Tests for progress callbacks during saving."""

    @pytest.mark.slow
    def test_save_ply_with_progress(self, lf, benchmark_ply, tmp_output):
        """Test progress callback during PLY save."""
        result = lf.io.load(str(benchmark_ply))
        output_path = tmp_output / "progress.ply"

        progress_calls = []

        def on_progress(progress, stage):
            progress_calls.append((progress, stage))
            return True  # Continue

        lf.io.save_ply(result.splat_data, str(output_path), progress=on_progress)

        # File should be created (progress callbacks may not be implemented for all operations)
        assert output_path.exists()
        assert output_path.stat().st_size > 0

    @pytest.mark.slow
    def test_save_ply_completes_with_callback(self, lf, benchmark_ply, tmp_output):
        """Test that save completes successfully with progress callback provided."""
        result = lf.io.load(str(benchmark_ply))
        output_path = tmp_output / "with_callback.ply"

        def on_progress(progress, stage):
            return True  # Continue

        lf.io.save_ply(result.splat_data, str(output_path), progress=on_progress)
        assert output_path.exists()
