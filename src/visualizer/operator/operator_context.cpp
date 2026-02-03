/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "operator_context.hpp"
#include "scene/scene_manager.hpp"

namespace lfs::vis::op {

    OperatorContext::OperatorContext(SceneManager& scene) : scene_(scene) {}

    bool OperatorContext::hasSelection() const {
        return scene_.hasSelectedNode();
    }

    std::vector<std::string> OperatorContext::selectedNodes() const {
        return scene_.getSelectedNodeNames();
    }

    std::string OperatorContext::activeNode() const {
        return scene_.getSelectedNodeName();
    }

    void OperatorContext::setModalEvent(const ModalEvent& event) {
        current_event_ = std::make_unique<ModalEvent>(event);
        if (event.type == ModalEvent::Type::MOUSE_MOVE) {
            if (const auto* move = event.as<MouseMoveEvent>()) {
                last_mouse_pos_ = glm::vec2(move->position);
            }
        }
    }

    glm::vec2 OperatorContext::mouseDelta() const {
        if (current_event_ && current_event_->type == ModalEvent::Type::MOUSE_MOVE) {
            if (const auto* move = current_event_->as<MouseMoveEvent>()) {
                return glm::vec2(move->delta);
            }
        }
        return glm::vec2(0.0f);
    }

} // namespace lfs::vis::op
