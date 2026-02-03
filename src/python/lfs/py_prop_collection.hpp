/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "py_prop.hpp"

#include <functional>
#include <nanobind/nanobind.h>
#include <vector>

namespace nb = nanobind;

namespace lfs::python {

    template <typename ItemT, typename WrapperT>
    class PyPropCollection {
    public:
        using GetItemsFunc = std::function<std::vector<ItemT*>()>;
        using WrapFunc = std::function<WrapperT(ItemT*)>;

        PyPropCollection(GetItemsFunc get_items, WrapFunc wrap)
            : get_items_(std::move(get_items)),
              wrap_(std::move(wrap)) {}

        size_t size() const { return get_items_().size(); }

        WrapperT getitem(int64_t index) const {
            auto items = get_items_();
            if (index < 0) {
                index += static_cast<int64_t>(items.size());
            }
            if (index < 0 || static_cast<size_t>(index) >= items.size()) {
                throw nb::index_error("Index out of range");
            }
            return wrap_(items[static_cast<size_t>(index)]);
        }

        std::vector<WrapperT> items() const {
            auto raw_items = get_items_();
            std::vector<WrapperT> result;
            result.reserve(raw_items.size());
            for (auto* item : raw_items) {
                result.push_back(wrap_(item));
            }
            return result;
        }

        class Iterator {
        public:
            Iterator(const PyPropCollection* coll, size_t index)
                : coll_(coll),
                  index_(index),
                  items_(coll->get_items_()) {}

            WrapperT next() {
                if (index_ >= items_.size()) {
                    throw nb::stop_iteration();
                }
                return coll_->wrap_(items_[index_++]);
            }

        private:
            const PyPropCollection* coll_;
            size_t index_;
            std::vector<ItemT*> items_;
        };

        Iterator iter() const { return Iterator(this, 0); }

    private:
        GetItemsFunc get_items_;
        WrapFunc wrap_;
    };

} // namespace lfs::python
