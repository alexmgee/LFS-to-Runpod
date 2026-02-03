/* SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "sequencer/animation_clip.hpp"
#include "sequencer/animation_track.hpp"
#include "sequencer/animation_value.hpp"
#include "sequencer/timeline.hpp"

#include <nanobind/nanobind.h>
#include <optional>

namespace nb = nanobind;

namespace lfs::python {

    class PyAnimationTrack {
    public:
        explicit PyAnimationTrack(sequencer::AnimationTrack* track) : track_(track) {}

        [[nodiscard]] uint64_t id() const { return track_->id(); }
        [[nodiscard]] std::string target_path() const { return track_->targetPath(); }
        [[nodiscard]] size_t keyframe_count() const { return track_->keyframeCount(); }

        void add_keyframe(float time, nb::object value, const std::string& easing = "ease_in_out");
        void remove_keyframe(size_t index);

        [[nodiscard]] nb::object evaluate(float time) const;
        [[nodiscard]] nb::list keyframes() const;

    private:
        sequencer::AnimationTrack* track_;
    };

    class PyAnimationClip {
    public:
        explicit PyAnimationClip(sequencer::AnimationClip* clip) : clip_(clip) {}

        [[nodiscard]] std::string name() const { return clip_->name(); }
        void set_name(const std::string& name) { clip_->setName(name); }

        PyAnimationTrack add_track(const std::string& value_type, const std::string& target_path);
        void remove_track(uint64_t id);

        [[nodiscard]] std::optional<PyAnimationTrack> get_track(uint64_t id);
        [[nodiscard]] std::optional<PyAnimationTrack> get_track_by_path(const std::string& path);

        [[nodiscard]] size_t track_count() const { return clip_->trackCount(); }
        [[nodiscard]] nb::list tracks() const;

        [[nodiscard]] nb::dict evaluate(float time) const;
        [[nodiscard]] float duration() const { return clip_->duration(); }

    private:
        sequencer::AnimationClip* clip_;
    };

    class PyTimeline {
    public:
        explicit PyTimeline(sequencer::Timeline* timeline) : timeline_(timeline) {}

        [[nodiscard]] PyAnimationClip animation_clip();
        [[nodiscard]] bool has_animation_clip() const { return timeline_->hasAnimationClip(); }

        [[nodiscard]] nb::dict evaluate_clip(float time) const;
        [[nodiscard]] float total_duration() const { return timeline_->totalDuration(); }

        [[nodiscard]] size_t keyframe_count() const { return timeline_->size(); }
        [[nodiscard]] float camera_duration() const { return timeline_->duration(); }

    private:
        sequencer::Timeline* timeline_;
    };

    void register_animation(nb::module_& m);

} // namespace lfs::python
