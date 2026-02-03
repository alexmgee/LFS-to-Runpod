/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "core/export.hpp"
#include "property_traits.hpp"

#include <functional>
#include <string>
#include <utility>

namespace lfs::core::prop {

    // Callback for undo command generation
    // Parameters: property_path, old_value, new_value, applier
    using UndoCallback = std::function<void(const std::string&, const std::any&, const std::any&,
                                            std::function<void(const std::any&)>)>;

    // Global undo callback setter (set by command system at startup)
    LFS_CORE_API void set_undo_callback(UndoCallback callback);
    LFS_CORE_API UndoCallback get_undo_callback();

    // AnimatableProperty: Observable with undo integration and property path
    template <typename T>
    class AnimatableProperty {
    public:
        using Callback = std::function<void()>;
        using Traits = PropertyTraits<T>;

        AnimatableProperty() = default;

        explicit AnimatableProperty(T initial) : value_(std::move(initial)) {}

        AnimatableProperty(T initial, Callback cb)
            : value_(std::move(initial)),
              on_change_(std::move(cb)) {}

        // Assignment with optional undo generation
        AnimatableProperty& operator=(const T& v) {
            set(v, true);
            return *this;
        }

        AnimatableProperty& operator=(T&& v) {
            set(std::move(v), true);
            return *this;
        }

        // Set with explicit undo control
        void set(const T& v, bool generate_undo = true) {
            if (value_ == v)
                return;

            T old_value = value_;
            value_ = v;

            if (generate_undo && !owner_id_.empty()) {
                generateUndo(old_value, v);
            }

            if (on_change_)
                on_change_();
        }

        void set(T&& v, bool generate_undo = true) {
            if (value_ == v)
                return;

            T old_value = std::move(value_);
            value_ = std::move(v);

            if (generate_undo && !owner_id_.empty()) {
                generateUndo(old_value, value_);
            }

            if (on_change_)
                on_change_();
        }

        // Set from animation system (no undo, but triggers callback)
        void setAnimated(const T& v) {
            if (value_ == v)
                return;
            value_ = v;
            if (on_change_)
                on_change_();
        }

        // Set quietly (no undo, no callback)
        void setQuiet(const T& v) { value_ = v; }
        void setQuiet(T&& v) { value_ = std::move(v); }

        // Accessors
        operator const T&() const { return value_; }
        [[nodiscard]] const T& get() const { return value_; }
        T& getMutable() { return value_; }

        // Callback management
        void setCallback(Callback cb) { on_change_ = std::move(cb); }
        void notifyChanged() {
            if (on_change_)
                on_change_();
        }

        // Property path for undo/animation system
        void setPropertyPath(const std::string& owner_id, const std::string& prop_id) {
            owner_id_ = owner_id;
            prop_id_ = prop_id;
        }

        [[nodiscard]] const std::string& ownerId() const { return owner_id_; }
        [[nodiscard]] const std::string& propId() const { return prop_id_; }
        [[nodiscard]] std::string propertyPath() const {
            if (owner_id_.empty())
                return prop_id_;
            return owner_id_ + "." + prop_id_;
        }

    private:
        void generateUndo(const T& old_value, const T& new_value) {
            auto undo_cb = get_undo_callback();
            if (!undo_cb)
                return;

            auto applier = [this](const std::any& val) {
                this->setQuiet(std::any_cast<T>(val));
                if (on_change_)
                    on_change_();
            };

            undo_cb(propertyPath(), std::any(old_value), std::any(new_value), std::move(applier));
        }

        T value_{};
        Callback on_change_{nullptr};
        std::string owner_id_;
        std::string prop_id_;
    };

    // Non-member operators for template argument deduction
    template <typename T>
    bool operator==(const AnimatableProperty<T>& lhs, const T& rhs) {
        return lhs.get() == rhs;
    }

    template <typename T>
    bool operator==(const T& lhs, const AnimatableProperty<T>& rhs) {
        return lhs == rhs.get();
    }

    template <typename T>
    bool operator!=(const AnimatableProperty<T>& lhs, const T& rhs) {
        return lhs.get() != rhs;
    }

    template <typename T>
    bool operator!=(const T& lhs, const AnimatableProperty<T>& rhs) {
        return lhs != rhs.get();
    }

    template <typename T, typename U>
    auto operator*(const U& lhs, const AnimatableProperty<T>& rhs) -> decltype(lhs * rhs.get()) {
        return lhs * rhs.get();
    }

    template <typename T, typename U>
    auto operator*(const AnimatableProperty<T>& lhs, const U& rhs) -> decltype(lhs.get() * rhs) {
        return lhs.get() * rhs;
    }

} // namespace lfs::core::prop
