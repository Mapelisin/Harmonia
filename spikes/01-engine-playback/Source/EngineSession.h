#pragma once

#include "Common.h"

namespace harmonia::spike
{
    /** Owns one Edit with a single instrument track that loops a 4-bar chord progression. */
    class EngineSession
    {
    public:
        explicit EngineSession (te::Engine&);
        ~EngineSession();

        te::Edit& getEdit() noexcept { return *edit; }

        void play();
        void stop();
        bool isPlaying() const;
        double getPositionSeconds() const;
        double getLoopLengthSeconds() const;

        /** Replaces the current instrument with the built-in 4OSC synth. */
        juce::String useBuiltInSynth();

        /** Replaces the current instrument with an external plugin.
            Returns an empty string on success, otherwise the error. */
        juce::String useExternalInstrument (const juce::PluginDescription&);

        te::Plugin* getInstrument() const;
        juce::String getInstrumentName() const;

        /** Renders the whole edit to a WAV file on the calling thread. */
        bool renderToFile (const juce::File&);

    private:
        te::Engine& engine;
        std::unique_ptr<te::Edit> edit;

        te::AudioTrack& getTrack() const;
        void createChordProgression();
        void removeInstrument();
        juce::String setInstrument (te::Plugin::Ptr);
    };
}
