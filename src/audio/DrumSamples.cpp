#include "audio/DrumSamples.h"
#include "audio/DrumKit.h"
#include "core/Drums.h"

namespace harmonia::audio::drum_kit
{
    namespace
    {
        const juce::Identifier samplesTag ("SAMPLES");
        const juce::Identifier sampleTag ("SAMPLE");
        const juce::Identifier noteId ("note"), fileId ("file"), gainId ("gain"), tuneId ("tune");

        constexpr double stopSeconds = 0.004;       // how fast a stopped hit goes
        constexpr float silent = 1.0e-4f;

        bool isKey (int note)
        {
            return note >= 0 && note < 128;
        }

        juce::ValueTree findNode (const te::Plugin& drums, int note)
        {
            return drums.state.getChildWithName (samplesTag).getChildWithProperty (noteId, note);
        }
    }

    //==============================================================================
    void SampleVoice::start (int key, const KeySample& sample, float velocity, float tune, float decay, double rate)
    {
        data = sample.data.get();
        note = key;
        active = data != nullptr && data->audio.getNumSamples() > 0;
        age = 0;
        position = 0.0;
        step = (data != nullptr ? data->sampleRate : rate) / rate * std::pow (2.0, (double) tune / 12.0);
        level = sample.gain * std::pow (juce::jlimit (0.0f, 1.0f, velocity), 1.5f);
        fade = 1.0f;
        fading = false;

        // Decay under 100%: only that much of the sample, its last third fading out.
        const auto length = data != nullptr ? (double) data->audio.getNumSamples() : 0.0;
        endAt = length * juce::jlimit (0.05, 1.0, (double) decay);
        fadeFrom = decay < 1.0f ? endAt * (2.0 / 3.0) : endAt;
    }

    void SampleVoice::stop (double rate)
    {
        fading = true;
        fadeFactor = (float) std::exp (std::log (0.001) / std::max (1.0, stopSeconds * rate));
    }

    void SampleVoice::render (float* left, float* right, int numSamples)
    {
        if (! active || data == nullptr)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            if (position >= endAt)
            {
                active = false;
                return;
            }

            auto gain = level * fade;

            if (position > fadeFrom)
                gain *= (float) ((endAt - position) / std::max (1.0, endAt - fadeFrom));

            left[i] += gain * data->getSampleAt (0, position);
            right[i] += gain * data->getSampleAt (1, position);

            position += step;
            ++age;

            if (fading)
            {
                fade *= fadeFactor;

                if (fade < silent)
                {
                    active = false;
                    return;
                }
            }
        }
    }

    //==============================================================================
    const juce::Identifier& getSamplesTag()
    {
        return samplesTag;
    }

    std::optional<SampleInfo> getSample (const te::Plugin& drums, int note)
    {
        const auto node = findNode (drums, note);

        if (! node.isValid())
            return std::nullopt;

        SampleInfo info;
        info.file = juce::File (node[fileId].toString());
        info.gainDb = (float) node.getProperty (gainId, 0.0f);
        info.tune = (float) node.getProperty (tuneId, 0.0f);
        info.missing = ! info.file.existsAsFile();
        return info;
    }

    std::vector<int> getSampledKeys (const te::Plugin& drums)
    {
        std::vector<int> keys;

        for (const auto& node : drums.state.getChildWithName (samplesTag))
            if (isKey ((int) node[noteId]))
                keys.push_back ((int) node[noteId]);

        std::sort (keys.begin(), keys.end());
        return keys;
    }

    juce::String setSample (te::Plugin& drums, int note, const juce::File& file)
    {
        if (! isKey (note))
            return "Not a key of the drum machine";

        if (const auto error = SampleData::check (drums.edit.engine, file); error.isNotEmpty())
            return error;

        auto* undoManager = &drums.edit.getUndoManager();
        undoManager->beginNewTransaction ("Sample for " + (core::drums::getName (note).isNotEmpty() ? core::drums::getName (note)
                                                                                                  : juce::String ("a key")));

        auto samples = drums.state.getOrCreateChildWithName (samplesTag, undoManager);
        auto node = samples.getChildWithProperty (noteId, note);

        if (node.isValid())
        {
            // Another file: its own volume and tuning start afresh.
            node.setProperty (fileId, file.getFullPathName(), undoManager);
            node.removeProperty (gainId, undoManager);
            node.removeProperty (tuneId, undoManager);
        }
        else
        {
            samples.appendChild (juce::ValueTree (sampleTag, { { noteId, note }, { fileId, file.getFullPathName() } }), undoManager);
        }

        return {};
    }

    void clearSample (te::Plugin& drums, int note)
    {
        auto node = findNode (drums, note);

        if (! node.isValid())
            return;

        auto* undoManager = &drums.edit.getUndoManager();
        undoManager->beginNewTransaction ("Built-in sound for " + (core::drums::getName (note).isNotEmpty() ? core::drums::getName (note)
                                                                                                          : juce::String ("a key")));
        node.getParent().removeChild (node, undoManager);
    }

    void setSampleGainDb (te::Plugin& drums, int note, float db)
    {
        if (auto node = findNode (drums, note); node.isValid())
            node.setProperty (gainId, juce::jlimit (minSampleGainDb, maxSampleGainDb, db), &drums.edit.getUndoManager());
    }

    void setSampleTune (te::Plugin& drums, int note, float semitones)
    {
        if (auto node = findNode (drums, note); node.isValid())
            node.setProperty (tuneId, juce::jlimit (-maxSampleTune, maxSampleTune, semitones), &drums.edit.getUndoManager());
    }

    juce::String getKeyName (const te::Plugin& drums, int note)
    {
        if (const auto sample = getSample (drums, note); sample.has_value() && ! sample->missing)
            return sample->file.getFileNameWithoutExtension();

        return core::drums::getName (note);
    }
}
