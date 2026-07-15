#pragma once

#include "Sequence.hpp"
#include "SequenceAssetLoader.hpp"

#include <functional>
#include <set>
#include <string>
#include <vector>

class SequencePlayer {
public:
    void load(const Sequence& seq);
    void loadSequence(const std::string& seqJsonRelPath);
    void setSequenceRef(Sequence& seq);

    void play(bool loop = false);
    void pause();
    void stop();
    void seek(double t);

    struct FrameCallbacks {
        std::function<void(double t, const std::string& clipName, double clipOffset, double playSpeed)> onAnimClipEval;
        std::function<void(double t, const std::string& pathAssetRelPath)> onCameraPathEval;
        std::function<void(double t, const TransformTweenClip& clip)> onTransformTweenEval;
        std::function<void(double t, uint64_t entityId, const TransformKeyframeTrack::EvalResult& result)> onTransformKeyframeEval;
        std::function<void(double t, uint64_t entityId, const AnimatorKeyframeTrack::EvalResult& result)> onAnimatorKeyframeEval;
        std::function<void(const std::string& eventName)> onEvent;
    };

    void update(double dt, const FrameCallbacks& cb);

    double currentTime()   const { return currentTime_; }
    double totalDuration() const { return seq_ ? seq_->totalDuration : 0.0; }
    bool   isPlaying()     const { return playing_; }
    bool   isLooping()     const { return loop_; }
    const Sequence* currentSequence() const { return seq_; }

private:
    const Sequence* seq_ = nullptr;
    Sequence        ownedSeq_;
    double          currentTime_ = 0.0;
    bool            playing_ = false;
    bool            loop_    = false;
    std::set<size_t> firedEventClipHashes_;
};