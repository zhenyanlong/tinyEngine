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
    firedEventClipHashes_.clear();
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
    firedEventClipHashes_.clear();
}

void SequencePlayer::pause()
{
    playing_ = false;
}

void SequencePlayer::stop()
{
    playing_ = false;
    currentTime_ = 0.0;
    firedEventClipHashes_.clear();
}

void SequencePlayer::seek(double t)
{
    if (!seq_) return;
    currentTime_ = std::clamp(t, 0.0, seq_->totalDuration > 0.0 ? seq_->totalDuration : 0.0);
    firedEventClipHashes_.clear();
}

void SequencePlayer::update(double dt, const FrameCallbacks& cb)
{
    if (!seq_ || !playing_) return;

    const double totalDur = seq_->totalDuration > 0.0
        ? seq_->totalDuration
        : seq_->computeTotalDuration();

    if (totalDur <= 0.0) return;

    currentTime_ += dt;

    if (currentTime_ >= totalDur) {
        if (loop_) {
            currentTime_ = std::fmod(currentTime_, totalDur);
            firedEventClipHashes_.clear();
        } else {
            currentTime_ = totalDur;
            playing_ = false;
            return;
        }
    }

    for (const auto& track : seq_->tracks) {
        switch (track.type) {
        case TrackType::AnimationClip:
            for (const auto& clip : track.animClips) {
                const double end = clip.startTime + clip.duration;
                if (currentTime_ >= clip.startTime && currentTime_ < end) {
                    const double localT = currentTime_ - clip.startTime;
                    if (cb.onAnimClipEval)
                        cb.onAnimClipEval(localT, clip.clipName, clip.clipOffset, clip.playSpeed);
                }
            }
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

        case TrackType::Event:
            for (size_t ci = 0; ci < track.eventClips.size(); ++ci) {
                const auto& clip = track.eventClips[ci];
                const double end = clip.startTime + clip.duration;

                size_t h = std::hash<std::string>{}(track.name)
                         ^ (std::hash<std::string>{}(clip.eventName) << 1)
                         ^ (std::hash<double>{}(clip.startTime) << 2);

                const bool inRange = currentTime_ >= clip.startTime && currentTime_ < end;
                const bool notFired = firedEventClipHashes_.find(h) == firedEventClipHashes_.end();

                if (inRange && notFired) {
                    firedEventClipHashes_.insert(h);
                    if (cb.onEvent)
                        cb.onEvent(clip.eventName);
                }

                if (!inRange) {
                    firedEventClipHashes_.erase(h);
                }
            }
            break;
        }
    }
}