/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "core/export.hpp"

#include "undo_entry.hpp"
#include <deque>
#include <mutex>

namespace lfs::vis::op {

    class LFS_VIS_API UndoHistory {
    public:
        static constexpr size_t MAX_ENTRIES = 100;

        static UndoHistory& instance();

        void push(UndoEntryPtr entry);
        void undo();
        void redo();
        void clear();

        [[nodiscard]] bool canUndo() const;
        [[nodiscard]] bool canRedo() const;
        [[nodiscard]] std::string undoName() const;
        [[nodiscard]] std::string redoName() const;
        [[nodiscard]] size_t undoCount() const;
        [[nodiscard]] size_t redoCount() const;

    private:
        UndoHistory() = default;
        ~UndoHistory() = default;
        UndoHistory(const UndoHistory&) = delete;
        UndoHistory& operator=(const UndoHistory&) = delete;

        std::deque<UndoEntryPtr> undo_stack_;
        std::deque<UndoEntryPtr> redo_stack_;
        mutable std::mutex mutex_;
    };

    inline UndoHistory& undoHistory() {
        return UndoHistory::instance();
    }

} // namespace lfs::vis::op
