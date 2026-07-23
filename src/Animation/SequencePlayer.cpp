#include "SequencePlayer.hpp"

#include <algorithm>
#include <iostream>
#include <cmath>

void SequencePlayer::load(const Sequence& seq)
{
    ownedSeq_ = seq;
    seq_ = &ownedSeq_;
    currentTime_ = 0.0;
    playing_ = false;
    loop_ = false;
}

void SequencePlayer::setSequenceRef(Sequence& seq)
{
    if (seq_ == &seq)
        return;
    seq_ = &seq;
}

void SequencePlayer::loadSequence(const std::string& seqJsonRelPath)
{
    Sequence seq;
    std::string err;
    if (!SequenceAssetLoader::loadSequence(seqJsonRelPath, seq, &err)) {
        std::cerr << "[SequencePlayer] Failed to load " << seqJsonRelPath
                  << ": " << err << "\n";
        return;
    }
    load(seq);
}

void SequencePlayer::play(bool loop)
{
    if (!seq_) return;
    playing_ = true;
    loop_ = loop;
}

void SequencePlayer::pause()
{
    playing_ = false;
}

void SequencePlayer::stop()
{
    playing_ = false;
    currentTime_ = 0.0;
}

void SequencePlayer::seek(double t)
{
    if (!seq_) return;
    const double maxDur = seq_->totalDuration > 0.0
        ? seq_->totalDuration
        : seq_->computeTotalDuration();
    currentTime_ = std::clamp(t, 0.0, maxDur > 0.0 ? maxDur : 0.0);
}

void SequencePlayer::update(double dt, const FrameCallbacks& cb)
{
    if (!seq_) return;

    const double totalDur = seq_->totalDuration > 0.0
        ? seq_->totalDuration
        : seq_->computeTotalDuration();

    if (totalDur <= 0.0 && !seq_->tracks.empty()) {
        for (const auto& track : seq_->tracks) {
            if (track.type == TrackType::TransformKeyframe && !track.keyframeTrack.keyframes.empty()) {
                auto result = track.keyframeTrack.evaluate(currentTime_);
                if (result.valid && cb.onTransformKeyframeEval) {
                    cb.onTransformKeyframeEval(currentTime_, track.keyframeTrack.targetEntityId, result);
                }
            }
            if (track.type == TrackType::AnimatorKeyframe && !track.animatorTrack.keyframes.empty()) {
                auto result = track.animatorTrack.evaluate(currentTime_);
                if (cb.onAnimatorKeyframeEval) {
                    cb.onAnimatorKeyframeEval(currentTime_, track.animatorTrack.targetEntityId, result);
                }
            }
        }
        return;
    }

    if (totalDur <= 0.0) return;

    if (playing_) {
        currentTime_ += dt;

        if (currentTime_ >= totalDur) {
            if (loop_) {
                currentTime_ = std::fmod(currentTime_, totalDur);
            } else {
                currentTime_ = totalDur;
                playing_ = false;
            }
        }
    }

    for (const auto& track : seq_->tracks) {
        switch (track.type) {
        case TrackType::AnimationClip:
            // Deprecated: Animator state/clip selection is driven exclusively by
            // AnimatorKeyframe events. Retained only for legacy JSON round trips.
            break;

        case TrackType::CameraPath:
            for (const auto& clip : track.cameraPathClips) {
                const double end = clip.startTime + clip.duration;
                if (currentTime_ >= clip.startTime && currentTime_ < end) {
                    const double localT = currentTime_ - clip.startTime;
                    if (cb.onCameraPathEval)
                        cb.onCameraPathEval(localT, clip.pathAssetRelPath);
                }
            }
            break;

        case TrackType::TransformTween:
            for (const auto& clip : track.tweenClips) {
                const double end = clip.startTime + clip.duration;
                if (currentTime_ >= clip.startTime && currentTime_ < end) {
                    if (cb.onTransformTweenEval)
                        cb.onTransformTweenEval(currentTime_ - clip.startTime, clip);
                }
            }
            break;

        case TrackType::TransformKeyframe:
            if (!track.keyframeTrack.keyframes.empty()) {
                auto result = track.keyframeTrack.evaluate(currentTime_);
                if (result.valid && cb.onTransformKeyframeEval) {
                    cb.onTransformKeyframeEval(currentTime_, track.keyframeTrack.targetEntityId, result);
                }
            }
            break;

        case TrackType::AnimatorKeyframe:
            if (!track.animatorTrack.keyframes.empty()) {
                auto result = track.animatorTrack.evaluate(currentTime_);
                if (cb.onAnimatorKeyframeEval) {
                    cb.onAnimatorKeyframeEval(currentTime_, track.animatorTrack.targetEntityId, result);
                }
            }
            break;

        case TrackType::Event:
            // Deprecated: independent Event tracks must never broadcast into Animators.
            break;

        case TrackType::Group:
            break;
        }
    }
}
