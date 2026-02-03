/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "animation_track.hpp"
#include "interpolation.hpp"

#include <algorithm>
#include <cassert>

namespace lfs::sequencer {

    namespace {
        constexpr float TIME_EPSILON = 0.0001f;
    }

    AnimationTrack::AnimationTrack(TrackId id, ValueType type, std::string target_path)
        : id_(id),
          type_(type),
          target_path_(std::move(target_path)) {}

    void AnimationTrack::addKeyframe(float time, const AnimationValue& value, EasingType easing) {
        assert(getValueType(value) == type_ && "Keyframe value type must match track type");

        for (auto& kf : keyframes_) {
            if (std::abs(kf.time - time) < TIME_EPSILON) {
                kf.value = value;
                kf.easing = easing;
                return;
            }
        }

        keyframes_.push_back({time, value, easing});
        sortKeyframes();
    }

    void AnimationTrack::removeKeyframe(size_t index) {
        assert(index < keyframes_.size());
        keyframes_.erase(keyframes_.begin() + static_cast<ptrdiff_t>(index));
    }

    void AnimationTrack::updateKeyframe(size_t index, float time, const AnimationValue& value) {
        assert(index < keyframes_.size());
        assert(getValueType(value) == type_ && "Keyframe value type must match track type");

        keyframes_[index].time = time;
        keyframes_[index].value = value;
        sortKeyframes();
    }

    std::optional<AnimationValue> AnimationTrack::evaluate(float time) const {
        if (keyframes_.empty()) {
            return std::nullopt;
        }

        if (keyframes_.size() == 1 || time <= keyframes_.front().time) {
            return keyframes_.front().value;
        }

        if (time >= keyframes_.back().time) {
            return keyframes_.back().value;
        }

        for (size_t i = 0; i < keyframes_.size() - 1; ++i) {
            if (time >= keyframes_[i].time && time < keyframes_[i + 1].time) {
                const float segment_duration = keyframes_[i + 1].time - keyframes_[i].time;
                const float local_t = (time - keyframes_[i].time) / segment_duration;
                const float eased_t = applyEasing(local_t, keyframes_[i].easing);

                return interpolateValue(keyframes_[i].value, keyframes_[i + 1].value, eased_t);
            }
        }

        return keyframes_.back().value;
    }

    float AnimationTrack::startTime() const {
        if (keyframes_.empty()) {
            return 0.0f;
        }
        return keyframes_.front().time;
    }

    float AnimationTrack::endTime() const {
        if (keyframes_.empty()) {
            return 0.0f;
        }
        return keyframes_.back().time;
    }

    void AnimationTrack::sortKeyframes() { std::sort(keyframes_.begin(), keyframes_.end()); }

} // namespace lfs::sequencer
