/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <gtest/gtest.h>

#include "core/logger.hpp"

#ifdef LFS_BUILD_PYTHON_BINDINGS
#include "python/runner.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class PythonIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        lfs::python::ensure_initialized();
    }

    std::filesystem::path createTempScript(const std::string& content) {
        auto temp_dir = std::filesystem::temp_directory_path();
        auto script_path = temp_dir / "test_script.py";
        std::ofstream ofs(script_path);
        ofs << content;
        ofs.close();
        return script_path;
    }
};

TEST_F(PythonIntegrationTest, InitializationSucceeds) {
    // Just verify that initialization doesn't throw
    EXPECT_NO_THROW(lfs::python::ensure_initialized());
}

TEST_F(PythonIntegrationTest, OutputCallbackCanBeSet) {
    bool callback_set = false;
    lfs::python::set_output_callback([&](const std::string&, bool) { callback_set = true; });
    EXPECT_TRUE(true); // If we got here, setting the callback didn't crash
}

TEST_F(PythonIntegrationTest, OutputRedirectCanBeInstalled) {
    // This should not throw
    EXPECT_NO_THROW(lfs::python::install_output_redirect());
}

TEST_F(PythonIntegrationTest, EmptyScriptListSucceeds) {
    auto result = lfs::python::run_scripts({});
    EXPECT_TRUE(result.has_value()) << "Empty script list should succeed";
}

// NOTE: Tests that actually execute Python scripts require the lichtfeld module
// to be importable, which depends on the CommandCenter and training infrastructure.
// These are better tested via integration tests (running training with --python-script).

#else // !LFS_BUILD_PYTHON_BINDINGS

TEST(PythonIntegrationTest, BindingsDisabled) {
    GTEST_SKIP() << "Python bindings not enabled";
}

#endif // LFS_BUILD_PYTHON_BINDINGS
