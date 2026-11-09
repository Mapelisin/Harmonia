#include "core/Takes.h"
#include "core/AudioClips.h"

namespace harmonia::core::takes
{
    namespace
    {
        const juce::Identifier takesTag ("HTAKES");

        juce::ValueTree getTakes (const te::Clip& clip)
        {
            return clip.state.getChildWithName (takesTag);
        }

        juce::File fileOf (te::Edit& edit, const juce::ValueTree& take)
        {
            return te::SourceFileReference::findFileFromString (edit, take[te::IDs::source].toString());
        }

        double lengthOf (te::Engine& engine, const juce::File& file)
        {
            return te::AudioFile (engine, file).getLength();
        }

        /** The last take about as long as the longest (the last pass is cut short by Stop). */
        int lastFullTake (te::Edit& edit, const juce::ValueTree& takes)
        {
            std::vector<double> lengths;

            for (const auto& take : takes)
                lengths.push_back (lengthOf (edit.engine, fileOf (edit, take)));

            const auto longest = lengths.empty() ? 0.0 : *std::max_element (lengths.begin(), lengths.end());

            for (int i = (int) lengths.size(); --i >= 0;)
                if (lengths[(size_t) i] >= longest - 0.05)
                    return i;

            return 0;
        }
    }

    int getCount (const te::Clip& clip)
    {
        return getTakes (clip).getNumChildren();
    }

    juce::File getFile (const te::Clip& clip, int index)
    {
        const auto take = getTakes (clip).getChild (index);
        return take.isValid() ? fileOf (clip.edit, take) : juce::File();
    }

    juce::String getName (int index)
    {
        return "Take " + juce::String (index + 1);
    }

    int getCurrent (const te::Clip& clip)
    {
        auto* audioClip = dynamic_cast<const te::AudioClipBase*> (&clip);

        if (audioClip == nullptr)
            return -1;

        const auto playing = const_cast<te::AudioClipBase*> (audioClip)->getSourceFileReference().getFile();

        for (int i = 0; i < getCount (clip); ++i)
            if (getFile (clip, i) == playing)
                return i;

        return -1;
    }

    bool choose (te::AudioClipBase& clip, int index)
    {
        const auto file = getFile (clip, index);

        if (! file.existsAsFile())
            return false;

        if (index != getCurrent (clip))
        {
            clip.getSourceFileReference().setToFile (file, te::SourceFileReference::PathStyle::alwaysAbsolute, false);
            audio_clips::prepareForPlayback (clip);
        }

        return true;
    }

    void keepOnlyCurrent (te::AudioClipBase& clip)
    {
        if (auto takes = getTakes (clip); takes.isValid())
            clip.state.removeChild (takes, &clip.edit.getUndoManager());
    }

    int adoptRecorded (te::Edit& edit)
    {
        auto* undoManager = &edit.getUndoManager();
        int adopted = 0;

        for (auto* track : te::getAudioTracks (edit))
        {
            for (auto* clip : track->getClips())
            {
                auto* audioClip = dynamic_cast<te::WaveAudioClip*> (clip);
                const auto recorded = clip->state.getChildWithName (te::IDs::TAKES);

                if (audioClip == nullptr || ! recorded.isValid())
                    continue;

                // The engine lists the first take twice (first, and again after the others).
                juce::ValueTree takes (takesTag);
                juce::Array<juce::File> listed;

                for (const auto& take : recorded)
                    if (const auto file = fileOf (edit, take); file.existsAsFile() && listed.addIfNotAlreadyThere (file))
                        takes.appendChild (juce::ValueTree (te::IDs::TAKE, { { te::IDs::source, file.getFullPathName() } }), nullptr);

                audioClip->clearTakes();
                clip->state.removeChild (getTakes (*clip), undoManager);

                // A single pass isn't worth a list.
                if (takes.getNumChildren() < 2)
                    continue;

                clip->state.appendChild (takes, undoManager);
                choose (*audioClip, lastFullTake (edit, takes));
                ++adopted;
            }
        }

        return adopted;
    }
}
