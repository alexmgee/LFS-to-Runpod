/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <glad/glad.h>

#include "core/image_io.hpp"
#include "icon_cache.hpp"
#include "internal/resource_paths.hpp"

namespace lfs::vis::gui {

    namespace {
        constexpr const char* ICON_PREFIX = "icon/";
        constexpr const char* ICON_SUFFIX = ".png";
        constexpr const char* DEFAULT_ICON = "default";
    } // namespace

    IconCache& IconCache::instance() {
        static IconCache cache;
        return cache;
    }

    IconCache::~IconCache() { clear(); }

    unsigned int IconCache::getIcon(const std::string& name) {
        if (name.empty()) {
            return 0;
        }

        {
            std::lock_guard lock(mutex_);
            const auto it = cache_.find(name);
            if (it != cache_.end()) {
                return it->second;
            }
        }

        unsigned int texture_id = loadTexture(name);
        if (texture_id == 0 && name != DEFAULT_ICON) {
            texture_id = loadTexture(DEFAULT_ICON);
        }

        {
            std::lock_guard lock(mutex_);
            cache_[name] = texture_id;
        }

        return texture_id;
    }

    void IconCache::clear() {
        std::lock_guard lock(mutex_);
        for (const auto& [name, texture_id] : cache_) {
            if (texture_id != 0) {
                glDeleteTextures(1, &texture_id);
            }
        }
        cache_.clear();
    }

    unsigned int IconCache::loadTexture(const std::string& icon_name) {
        std::string path_str = icon_name;
        if (icon_name.find('/') == std::string::npos && icon_name.find('.') == std::string::npos) {
            path_str = std::string(ICON_PREFIX) + icon_name + ICON_SUFFIX;
        }

        const auto path = lfs::vis::getAssetPath(path_str);
        const auto [data, width, height, channels] = lfs::core::load_image_with_alpha(path);
        if (!data) {
            return 0;
        }

        unsigned int texture_id = 0;
        glGenTextures(1, &texture_id);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        const GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);

        lfs::core::free_image(data);
        glBindTexture(GL_TEXTURE_2D, 0);

        return texture_id;
    }

} // namespace lfs::vis::gui
