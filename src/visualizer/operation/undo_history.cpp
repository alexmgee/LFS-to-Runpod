/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "undo_history.hpp"
#include "core/logger.hpp"

namespace lfs::vis::op {

    UndoHistory& UndoHistory::instance() {
        static UndoHistory instance;
        return instance;
    }

    void UndoHistory::push(UndoEntryPtr entry) {
        if (!entry) {
            return;
        }

        std::lock_guard lock(mutex_);

        redo_stack_.clear();

        undo_stack_.push_back(std::move(entry));

        while (undo_stack_.size() > MAX_ENTRIES) {
            undo_stack_.pop_front();
        }

        LOG_DEBUG("Pushed undo entry: {} (stack size: {})", undo_stack_.back()->name(), undo_stack_.size());
    }

    void UndoHistory::undo() {
        std::lock_guard lock(mutex_);

        if (undo_stack_.empty()) {
            return;
        }

        auto entry = std::move(undo_stack_.back());
        undo_stack_.pop_back();

        LOG_DEBUG("Undoing: {}", entry->name());
        entry->undo();

        redo_stack_.push_back(std::move(entry));
    }

    void UndoHistory::redo() {
        std::lock_guard lock(mutex_);

        if (redo_stack_.empty()) {
            return;
        }

        auto entry = std::move(redo_stack_.back());
        redo_stack_.pop_back();

        LOG_DEBUG("Redoing: {}", entry->name());
        entry->redo();

        undo_stack_.push_back(std::move(entry));
    }

    void UndoHistory::clear() {
        std::lock_guard lock(mutex_);
        undo_stack_.clear();
        redo_stack_.clear();
    }

    bool UndoHistory::canUndo() const {
        std::lock_guard lock(mutex_);
        return !undo_stack_.empty();
    }

    bool UndoHistory::canRedo() const {
        std::lock_guard lock(mutex_);
        return !redo_stack_.empty();
    }

    std::string UndoHistory::undoName() const {
        std::lock_guard lock(mutex_);
        if (undo_stack_.empty()) {
            return "";
        }
        return undo_stack_.back()->name();
    }

    std::string UndoHistory::redoName() const {
        std::lock_guard lock(mutex_);
        if (redo_stack_.empty()) {
            return "";
        }
        return redo_stack_.back()->name();
    }

    size_t UndoHistory::undoCount() const {
        std::lock_guard lock(mutex_);
        return undo_stack_.size();
    }

    size_t UndoHistory::redoCount() const {
        std::lock_guard lock(mutex_);
        return redo_stack_.size();
    }

} // namespace lfs::vis::op
