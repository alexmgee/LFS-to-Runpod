/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "pty_process.hpp"
#include <cassert>
#include <core/executable_path.hpp>
#include <core/logger.hpp>
#include <filesystem>

#ifdef _WIN32
#include <process.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

    std::filesystem::path getVenvSitePackages() {
#ifdef _WIN32
        const char* const home = std::getenv("USERPROFILE");
        const auto venv_dir = std::filesystem::path(home ? home : "C:\\") / ".lichtfeld" / "venv";
        return venv_dir / "Lib" / "site-packages";
#else
        const char* const home = std::getenv("HOME");
        const auto venv_dir = std::filesystem::path(home ? home : "/tmp") / ".lichtfeld" / "venv";
        const auto lib_dir = venv_dir / "lib";

        if (std::filesystem::exists(lib_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(lib_dir)) {
                if (entry.is_directory()) {
                    const auto name = entry.path().filename().string();
                    if (name.starts_with("python")) {
                        return entry.path() / "site-packages";
                    }
                }
            }
        }
        return venv_dir / "lib" / "python3" / "site-packages";
#endif
    }

} // namespace

namespace lfs::vis::terminal {

    PtyProcess::~PtyProcess() {
        close();
    }

    PtyProcess::PtyProcess(PtyProcess&& other) noexcept
#ifdef _WIN32
        : hpc_(other.hpc_),
          pipe_in_(other.pipe_in_),
          pipe_out_(other.pipe_out_),
          process_(other.process_),
          thread_(other.thread_),
          pipe_only_(other.pipe_only_) {
        other.hpc_ = nullptr;
        other.pipe_in_ = INVALID_HANDLE_VALUE;
        other.pipe_out_ = INVALID_HANDLE_VALUE;
        other.process_ = INVALID_HANDLE_VALUE;
        other.thread_ = INVALID_HANDLE_VALUE;
        other.pipe_only_ = false;
    }
#else
        : master_fd_(other.master_fd_),
          child_pid_(other.child_pid_),
          attached_(other.attached_) {
        other.master_fd_ = -1;
        other.child_pid_ = -1;
        other.attached_ = false;
    }
#endif

    PtyProcess& PtyProcess::operator=(PtyProcess&& other) noexcept {
        if (this != &other) {
            close();
#ifdef _WIN32
            hpc_ = other.hpc_;
            pipe_in_ = other.pipe_in_;
            pipe_out_ = other.pipe_out_;
            process_ = other.process_;
            thread_ = other.thread_;
            pipe_only_ = other.pipe_only_;
            other.hpc_ = nullptr;
            other.pipe_in_ = INVALID_HANDLE_VALUE;
            other.pipe_out_ = INVALID_HANDLE_VALUE;
            other.process_ = INVALID_HANDLE_VALUE;
            other.thread_ = INVALID_HANDLE_VALUE;
            other.pipe_only_ = false;
#else
            master_fd_ = other.master_fd_;
            child_pid_ = other.child_pid_;
            attached_ = other.attached_;
            other.master_fd_ = -1;
            other.child_pid_ = -1;
            other.attached_ = false;
#endif
        }
        return *this;
    }

#ifdef _WIN32

    bool PtyProcess::spawn(const std::string& shell, int cols, int rows) {
        close();

        const COORD size = {static_cast<SHORT>(cols), static_cast<SHORT>(rows)};

        HANDLE pipe_pty_in = INVALID_HANDLE_VALUE;
        HANDLE pipe_pty_out = INVALID_HANDLE_VALUE;

        if (!CreatePipe(&pipe_in_, &pipe_pty_in, nullptr, 0)) {
            LOG_ERROR("CreatePipe for input failed");
            return false;
        }

        if (!CreatePipe(&pipe_pty_out, &pipe_out_, nullptr, 0)) {
            LOG_ERROR("CreatePipe for output failed");
            CloseHandle(pipe_in_);
            CloseHandle(pipe_pty_in);
            pipe_in_ = INVALID_HANDLE_VALUE;
            return false;
        }

        const HRESULT hr = CreatePseudoConsole(size, pipe_pty_in, pipe_pty_out, 0, &hpc_);
        CloseHandle(pipe_pty_in);
        CloseHandle(pipe_pty_out);

        if (FAILED(hr)) {
            LOG_ERROR("CreatePseudoConsole failed: {}", hr);
            CloseHandle(pipe_in_);
            CloseHandle(pipe_out_);
            pipe_in_ = INVALID_HANDLE_VALUE;
            pipe_out_ = INVALID_HANDLE_VALUE;
            return false;
        }

        STARTUPINFOEXW si = {};
        si.StartupInfo.cb = sizeof(si);

        SIZE_T attr_size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
        si.lpAttributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attr_size));

        if (!si.lpAttributeList ||
            !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size) ||
            !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                       hpc_, sizeof(hpc_), nullptr, nullptr)) {
            LOG_ERROR("Failed to setup process attributes");
            if (si.lpAttributeList)
                HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
            cleanup();
            return false;
        }

        std::wstring cmd = shell.empty() ? L"cmd.exe" : std::wstring(shell.begin(), shell.end());

        PROCESS_INFORMATION pi = {};
        const BOOL success = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                            EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                                            &si.StartupInfo, &pi);

        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);

        if (!success) {
            LOG_ERROR("CreateProcess failed: {}", GetLastError());
            cleanup();
            return false;
        }

        process_ = pi.hProcess;
        thread_ = pi.hThread;

        LOG_INFO("PTY spawned: {}", shell.empty() ? "cmd.exe" : shell);
        return true;
    }

    bool PtyProcess::attachPipes(HANDLE read_handle, HANDLE write_handle) {
        close();
        assert(read_handle != INVALID_HANDLE_VALUE);
        assert(write_handle != INVALID_HANDLE_VALUE);
        pipe_out_ = read_handle;
        pipe_in_ = write_handle;
        pipe_only_ = true;
        LOG_INFO("PTY attached to pipe handles");
        return true;
    }

    void PtyProcess::close() {
        if (!pipe_only_) {
            if (process_ != INVALID_HANDLE_VALUE) {
                TerminateProcess(process_, 0);
                CloseHandle(process_);
                process_ = INVALID_HANDLE_VALUE;
            }
            if (thread_ != INVALID_HANDLE_VALUE) {
                CloseHandle(thread_);
                thread_ = INVALID_HANDLE_VALUE;
            }
        }
        cleanup();
        pipe_only_ = false;
    }

    void PtyProcess::cleanup() {
        if (hpc_) {
            ClosePseudoConsole(hpc_);
            hpc_ = nullptr;
        }
        if (pipe_in_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_in_);
            pipe_in_ = INVALID_HANDLE_VALUE;
        }
        if (pipe_out_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_out_);
            pipe_out_ = INVALID_HANDLE_VALUE;
        }
    }

    bool PtyProcess::is_running() const {
        if (pipe_only_)
            return pipe_in_ != INVALID_HANDLE_VALUE && pipe_out_ != INVALID_HANDLE_VALUE;
        if (process_ == INVALID_HANDLE_VALUE)
            return false;
        DWORD exit_code = 0;
        return GetExitCodeProcess(process_, &exit_code) && exit_code == STILL_ACTIVE;
    }

    ssize_t PtyProcess::read(char* buf, size_t len) {
        if (pipe_out_ == INVALID_HANDLE_VALUE)
            return -1;

        DWORD available = 0;
        if (!PeekNamedPipe(pipe_out_, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
            return 0;
        }

        DWORD bytes_read = 0;
        const DWORD to_read = static_cast<DWORD>(std::min(len, static_cast<size_t>(available)));
        if (!ReadFile(pipe_out_, buf, to_read, &bytes_read, nullptr)) {
            return -1;
        }
        return static_cast<ssize_t>(bytes_read);
    }

    ssize_t PtyProcess::write(const char* buf, size_t len) {
        if (pipe_in_ == INVALID_HANDLE_VALUE)
            return -1;

        DWORD bytes_written = 0;
        if (!WriteFile(pipe_in_, buf, static_cast<DWORD>(len), &bytes_written, nullptr)) {
            return -1;
        }
        return static_cast<ssize_t>(bytes_written);
    }

    void PtyProcess::resize(int cols, int rows) {
        if (hpc_) {
            const COORD size = {static_cast<SHORT>(cols), static_cast<SHORT>(rows)};
            ResizePseudoConsole(hpc_, size);
        }
    }

    void PtyProcess::interrupt() {
        if (pipe_in_ != INVALID_HANDLE_VALUE) {
            constexpr char CTRL_C = '\x03';
            DWORD written;
            WriteFile(pipe_in_, &CTRL_C, 1, &written, nullptr);
        }
    }

    int PtyProcess::fd() const {
        return -1;
    }

#else // POSIX

    bool PtyProcess::spawn(const std::string& shell, int cols, int rows) {
        close();

        struct winsize ws = {};
        ws.ws_col = static_cast<unsigned short>(cols);
        ws.ws_row = static_cast<unsigned short>(rows);

        // Resolve paths before fork (requires /proc/self/exe)
        std::string python_module_dir;
        if (const auto path = lfs::core::getPythonModuleDir(); !path.empty()) {
            python_module_dir = path.string();
        }

        std::string venv_site_packages;
        if (const auto path = getVenvSitePackages(); std::filesystem::exists(path)) {
            venv_site_packages = path.string();
        }

        child_pid_ = forkpty(&master_fd_, nullptr, nullptr, &ws);
        if (child_pid_ < 0) {
            LOG_ERROR("forkpty failed: {}", strerror(errno));
            return false;
        }

        if (child_pid_ == 0) {
            const char* sh = shell.empty() ? getenv("SHELL") : shell.c_str();
            if (!sh || sh[0] == '\0')
                sh = "/bin/bash";

            setenv("TERM", "xterm-256color", 1);

            // Build PYTHONPATH: venv + lichtfeld module + existing
            std::string pythonpath;
            if (!venv_site_packages.empty()) {
                pythonpath = venv_site_packages;
            }
            if (!python_module_dir.empty()) {
                if (!pythonpath.empty())
                    pythonpath += ":";
                pythonpath += python_module_dir;
            }
            if (!pythonpath.empty()) {
                if (const char* existing = getenv("PYTHONPATH"); existing && existing[0] != '\0') {
                    pythonpath += ":";
                    pythonpath += existing;
                }
                setenv("PYTHONPATH", pythonpath.c_str(), 1);
            }

            execlp(sh, sh, nullptr);
            _exit(127);
        }

        const int flags = fcntl(master_fd_, F_GETFL, 0);
        fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);

        LOG_INFO("PTY spawned: {} (pid {})", shell.empty() ? getenv("SHELL") : shell.c_str(), child_pid_);
        return true;
    }

    bool PtyProcess::attach(int fd) {
        close();
        assert(fd >= 0);
        master_fd_ = fd;
        attached_ = true;
        child_pid_ = -1;

        const int flags = fcntl(master_fd_, F_GETFL, 0);
        fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);

        LOG_INFO("PTY attached to fd {}", fd);
        return true;
    }

    void PtyProcess::close() {
        if (master_fd_ >= 0) {
            ::close(master_fd_);
            master_fd_ = -1;
        }
        if (!attached_ && child_pid_ > 0) {
            kill(child_pid_, SIGTERM);
            int status;
            waitpid(child_pid_, &status, WNOHANG);
            child_pid_ = -1;
        }
        attached_ = false;
    }

    void PtyProcess::cleanup() {}

    bool PtyProcess::is_running() const {
        if (attached_)
            return master_fd_ >= 0;
        if (child_pid_ <= 0)
            return false;
        int status;
        const pid_t result = waitpid(child_pid_, &status, WNOHANG);
        return result == 0;
    }

    ssize_t PtyProcess::read(char* buf, size_t len) {
        if (master_fd_ < 0)
            return -1;
        const ssize_t n = ::read(master_fd_, buf, len);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        return n;
    }

    ssize_t PtyProcess::write(const char* buf, size_t len) {
        if (master_fd_ < 0)
            return -1;
        return ::write(master_fd_, buf, len);
    }

    void PtyProcess::resize(int cols, int rows) {
        if (master_fd_ >= 0) {
            struct winsize ws = {};
            ws.ws_col = static_cast<unsigned short>(cols);
            ws.ws_row = static_cast<unsigned short>(rows);
            ioctl(master_fd_, TIOCSWINSZ, &ws);
        }
    }

    void PtyProcess::interrupt() {
        if (master_fd_ >= 0) {
            if (attached_) {
                constexpr char CTRL_C = '\x03';
                ::write(master_fd_, &CTRL_C, 1);
            } else {
                const pid_t fg_pgid = tcgetpgrp(master_fd_);
                if (fg_pgid > 0) {
                    kill(-fg_pgid, SIGINT);
                }
            }
        }
    }

    int PtyProcess::fd() const {
        return master_fd_;
    }

#endif

} // namespace lfs::vis::terminal
