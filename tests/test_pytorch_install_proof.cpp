/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "python/package_manager.hpp"
#include "python/uv_runner.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <iomanip>
#include <thread>

using namespace lfs::python;
using namespace std::chrono;

// This test proves UV output streams correctly with a REAL large package
// Run with: ./build/tests/lichtfeld_tests --gtest_filter='*PyTorchInstall*'
TEST(PyTorchInstallProof, StreamingOutputWhileUIResponsive) {
    auto& pm = PackageManager::instance();

    if (!pm.is_uv_available()) {
        GTEST_SKIP() << "UV not available";
    }

    ASSERT_TRUE(pm.ensure_venv()) << "Failed to create venv";

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  PROOF: Real PyTorch Installation with Streaming Output        ║\n";
    std::cout << "║  This demonstrates:                                            ║\n";
    std::cout << "║    1) Output streams in real-time (not buffered until end)     ║\n";
    std::cout << "║    2) UI remains responsive (60 FPS simulation)                ║\n";
    std::cout << "║    3) Progress bars and download status are visible            ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << std::endl;

    auto start = steady_clock::now();
    std::atomic<int> frame_count{0};
    std::atomic<int> line_count{0};
    std::atomic<bool> done{false};
    std::atomic<bool> success{false};

    // Track when each line arrives to prove streaming
    std::vector<std::pair<long, std::string>> timestamped_output;
    std::mutex output_mutex;

    bool started = pm.install_torch_async(
        "auto", "",
        [&](const std::string& line, bool is_error, bool is_line_update) {
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start).count();
            line_count++;

            {
                std::lock_guard<std::mutex> lock(output_mutex);
                timestamped_output.emplace_back(elapsed, line);
            }

            // Format output nicely
            if (is_line_update) {
                std::cout << "\r\033[K"; // Clear line for progress updates
            }
            std::cout << "\033[36m[" << std::setw(6) << elapsed << "ms]\033[0m ";
            if (is_error) {
                std::cout << "\033[31m" << line << "\033[0m";
            } else {
                // Highlight download progress
                if (line.find("Downloading") != std::string::npos ||
                    line.find("━") != std::string::npos) {
                    std::cout << "\033[33m" << line << "\033[0m";
                } else if (line.find("Installed") != std::string::npos ||
                           line.find("+") != std::string::npos) {
                    std::cout << "\033[32m" << line << "\033[0m";
                } else {
                    std::cout << line;
                }
            }
            if (!is_line_update) {
                std::cout << std::endl;
            }
            std::cout << std::flush;
        },
        [&](bool s, int exit_code) {
            done = true;
            success = s;
        });

    ASSERT_TRUE(started) << "Failed to start torch installation";

    std::cout << "\n\033[1m>>> Simulating 60 FPS GUI render loop...\033[0m\n" << std::endl;

    // Simulate GUI render loop at 60 FPS
    while (!done) {
        pm.poll(); // Non-blocking!
        frame_count++;
        std::this_thread::sleep_for(microseconds(16667)); // 60 FPS
    }

    auto total_ms = duration_cast<milliseconds>(steady_clock::now() - start).count();

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                         RESULTS                                ║\n";
    std::cout << "╠════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Total time:        " << std::setw(8) << total_ms << " ms"
              << std::string(30, ' ') << "║\n";
    std::cout << "║  Output lines:      " << std::setw(8) << line_count.load()
              << std::string(33, ' ') << "║\n";
    std::cout << "║  Frames rendered:   " << std::setw(8) << frame_count.load()
              << std::string(33, ' ') << "║\n";
    if (total_ms > 0) {
        double fps = frame_count.load() * 1000.0 / total_ms;
        std::cout << "║  Effective FPS:     " << std::setw(8) << std::fixed << std::setprecision(1)
                  << fps << std::string(33, ' ') << "║\n";
    }
    std::cout << "║  Success:           " << std::setw(8) << (success ? "YES" : "NO")
              << std::string(33, ' ') << "║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";

    // Analyze streaming behavior
    std::cout << "\n\033[1m>>> Streaming Analysis:\033[0m\n";

    if (timestamped_output.size() > 1) {
        long first_output = timestamped_output.front().first;
        long last_output = timestamped_output.back().first;
        long output_span = last_output - first_output;

        std::cout << "  First output at:  " << first_output << " ms\n";
        std::cout << "  Last output at:   " << last_output << " ms\n";
        std::cout << "  Output span:      " << output_span << " ms\n";

        // Count outputs in different time windows
        int early = 0, middle = 0, late = 0;
        for (const auto& [ts, _] : timestamped_output) {
            if (ts < total_ms / 3)
                early++;
            else if (ts < 2 * total_ms / 3)
                middle++;
            else
                late++;
        }

        std::cout << "  Output distribution: early=" << early << " middle=" << middle
                  << " late=" << late << "\n";

        // If output spans most of the operation time, it's truly streaming
        bool is_streaming = output_span > total_ms * 0.5;
        std::cout << "\n  \033[1mVERDICT: Output is "
                  << (is_streaming ? "\033[32mSTREAMING" : "\033[31mBUFFERED") << "\033[0m\n";

        EXPECT_TRUE(is_streaming || total_ms < 1000)
            << "For long operations, output should stream progressively";
    }

    // Verify UI responsiveness
    if (total_ms > 100) {
        double fps = frame_count.load() * 1000.0 / total_ms;
        std::cout << "\n  \033[1mVERDICT: UI is "
                  << (fps > 30 ? "\033[32mRESPONSIVE" : "\033[31mBLOCKED") << " (" << fps
                  << " FPS)\033[0m\n";

        EXPECT_GT(fps, 30) << "UI should maintain at least 30 FPS during installation";
    }

    std::cout << std::endl;
}
