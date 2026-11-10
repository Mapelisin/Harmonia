#include "ui/audioeditor/AudioEditorModel.h"
#include "core/AudioClips.h"
#include "core/AudioTempo.h"

namespace harmonia::ui::audioeditor
{
    namespace clips = core::clips;
    namespace audio_tempo = core::audio_tempo;

    AudioEditorModel::AudioEditorModel (arrangement::ArrangementModel& a)
        : arrangement (a)
    {
    }

    te::AudioClipBase* AudioEditorModel::getClip() const
    {
        return arrangement.getFocusedAudioClip();
    }

    te::WaveAudioClip* AudioEditorModel::getWaveClip() const
    {
        return dynamic_cast<te::WaveAudioClip*> (getClip());
    }

    //==============================================================================
    double AudioEditorModel::getFileSeconds() const
    {
        auto* clip = getClip();

        if (clip == nullptr || clips::isReversing (*clip))
            return 0.0;

        const te::AudioFile file (clip->edit.engine, clip->getCurrentSourceFile());
        return file.isValid() ? file.getLength() : 0.0;
    }

    juce::Range<double> AudioEditorModel::getPlayedSeconds() const
    {
        auto* clip = getClip();

        if (clip == nullptr)
            return {};

        // A looping clip: the part of the file that repeats.
        if (const auto loop = core::audio_clips::getLoopFileSeconds (*clip); ! loop.isEmpty())
            return loop;

        return { beatToFileSeconds (clips::getStartBeat (*clip)), beatToFileSeconds (clips::getEndBeat (*clip)) };
    }

    double AudioEditorModel::getFadeInEndSeconds() const
    {
        auto* clip = getClip();
        return clip != nullptr ? beatToFileSeconds (clips::getStartBeat (*clip) + clips::getFadeInBeats (*clip)) : 0.0;
    }

    double AudioEditorModel::getFadeOutStartSeconds() const
    {
        auto* clip = getClip();
        return clip != nullptr ? beatToFileSeconds (clips::getEndBeat (*clip) - clips::getFadeOutBeats (*clip)) : 0.0;
    }

    double AudioEditorModel::beatToFileSeconds (double beat) const
    {
        auto* clip = getClip();

        if (clip == nullptr)
            return 0.0;

        // A looping clip: as its first time round lies in the song.
        if (const auto loop = core::audio_clips::getLoopFileSeconds (*clip); ! loop.isEmpty())
        {
            const auto rounds = (beat - core::audio_clips::getLoopSongStartBeat (*clip)) / core::audio_clips::getLoopLengthBeats (*clip);
            return loop.getStart() + rounds * loop.getLength();
        }

        // As the arrangement draws it: song time from where the file would begin, at the clip's speed.
        const auto songTime = clip->edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat));
        return (songTime - clip->getPosition().getStartOfSource()).inSeconds() * audio_tempo::getPlaybackSpeed (*clip);
    }

    double AudioEditorModel::fileSecondsToBeat (double seconds) const
    {
        auto* clip = getClip();

        if (clip == nullptr)
            return 0.0;

        if (const auto loop = core::audio_clips::getLoopFileSeconds (*clip); ! loop.isEmpty())
            return core::audio_clips::getLoopSongStartBeat (*clip)
                 + (seconds - loop.getStart()) / loop.getLength() * core::audio_clips::getLoopLengthBeats (*clip);

        const auto speed = std::max (1.0e-6, audio_tempo::getPlaybackSpeed (*clip));
        const auto songTime = clip->getPosition().getStartOfSource() + te::TimeDuration::fromSeconds (seconds / speed);
        return clip->edit.tempoSequence.toBeats (songTime).inBeats();
    }

    double AudioEditorModel::getPixelsPerBeat (double pixelsPerSecond) const
    {
        auto* clip = getClip();

        if (clip == nullptr)
            return pixelsPerSecond * 0.5;

        const auto start = clips::getStartBeat (*clip);
        return pixelsPerSecond * std::max (1.0e-6, beatToFileSeconds (start + 1.0) - beatToFileSeconds (start));
    }

    juce::String AudioEditorModel::describeFile() const
    {
        auto* clip = getClip();

        if (clip == nullptr)
            return {};

        const auto original = clip->getOriginalFile();

        if (! original.existsAsFile())
            return "File missing: " + original.getFullPathName();

        const te::AudioFile file (clip->edit.engine, original);

        if (! file.isValid())
            return original.getFileName() + "  (can't be read)";

        const auto channels = file.getNumChannels();
        const auto rate = file.getSampleRate();
        const auto kHz = rate / 1000.0;
        return original.getFileName() + "   " + juce::String (kHz, std::abs (kHz - std::round (kHz)) < 0.01 ? 0 : 1) + " kHz   "
             + (channels == 1 ? juce::String ("mono") : channels == 2 ? juce::String ("stereo") : juce::String (channels) + " channels")
             + "   " + juce::String (file.getLength(), 2) + " s";
    }

    //==============================================================================
    void AudioEditorModel::beginChange (const juce::String& name)
    {
        arrangement.getProject().beginGesture (name);
    }

    void AudioEditorModel::trimStart (double beat, double minLengthBeats)
    {
        if (auto* clip = getClip())
            clips::setStart (*clip, std::max (0.0, beat), minLengthBeats);
    }

    void AudioEditorModel::trimEnd (double beat, double minLengthBeats)
    {
        if (auto* clip = getClip())
            clips::setEnd (*clip, beat, minLengthBeats);
    }

    void AudioEditorModel::setFadeInBeats (double beats)
    {
        if (auto* clip = getClip())
            clips::setFadeInBeats (*clip, beats);
    }

    void AudioEditorModel::setFadeOutBeats (double beats)
    {
        if (auto* clip = getClip())
            clips::setFadeOutBeats (*clip, beats);
    }

    void AudioEditorModel::setGainDb (float db, float maxDb)
    {
        if (auto* clip = getClip())
            clips::setGainDb (*clip, db, maxDb);
    }

    void AudioEditorModel::setPitch (float semitones)
    {
        if (auto* clip = getClip())
            audio_tempo::setPitch (*clip, semitones);
    }

    void AudioEditorModel::setLoopBpm (double bpm)
    {
        if (auto* wave = getWaveClip(); wave != nullptr && audio_tempo::followsTempo (*wave))
            audio_tempo::setFollowsTempo (*wave, juce::jlimit (audio_tempo::minLoopBpm, audio_tempo::maxLoopBpm, bpm));
    }

    bool AudioEditorModel::normalize()
    {
        auto* clip = getClip();

        if (clip == nullptr)
            return false;

        beginChange ("Normalize");
        return clips::normalize (*clip);
    }

    void AudioEditorModel::setReversed (bool shouldReverse)
    {
        auto* clip = getClip();

        if (clip == nullptr || clips::isReversed (*clip) == shouldReverse)
            return;

        beginChange (shouldReverse ? "Reverse" : "Play forwards");
        clips::setReversed (*clip, shouldReverse);
    }

    void AudioEditorModel::setFadeShape (clips::FadeShape shape)
    {
        auto* clip = getClip();

        if (clip == nullptr || clips::getFadeShape (*clip) == shape)
            return;

        beginChange ("Fade shape");
        clips::setFadeShape (*clip, shape);
    }

    arrangement::ArrangementModel::TempoFound AudioEditorModel::setFollowsTempo (bool shouldFollow)
    {
        if (auto* wave = getWaveClip())
            return arrangement.setFollowsTempo (*wave, shouldFollow);

        return {};
    }

    bool AudioEditorModel::slide (double beats)
    {
        auto* wave = getWaveClip();
        return wave != nullptr && arrangement.slideSound (*wave, beats);
    }

    bool AudioEditorModel::canSlide() const
    {
        auto* wave = getWaveClip();
        return wave != nullptr && audio_tempo::followsTempo (*wave) && ! core::audio_clips::isLooping (*wave);
    }

    void AudioEditorModel::halveLoopBpm()
    {
        if (auto* wave = getWaveClip())
            if (const auto bpm = audio_tempo::getLoopBpm (*wave); bpm.has_value() && *bpm / 2.0 >= audio_tempo::minLoopBpm - 0.05)
                arrangement.setLoopBpm (*wave, std::max (audio_tempo::minLoopBpm, *bpm / 2.0));
    }

    void AudioEditorModel::doubleLoopBpm()
    {
        if (auto* wave = getWaveClip())
            if (const auto bpm = audio_tempo::getLoopBpm (*wave); bpm.has_value() && *bpm * 2.0 <= audio_tempo::maxLoopBpm + 0.05)
                arrangement.setLoopBpm (*wave, std::min (audio_tempo::maxLoopBpm, *bpm * 2.0));
    }

    bool AudioEditorModel::canFollowTempo() const
    {
        return getWaveClip() != nullptr;
    }
}
