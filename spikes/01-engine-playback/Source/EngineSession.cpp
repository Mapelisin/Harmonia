#include "EngineSession.h"

namespace harmonia::spike
{
    namespace
    {
        constexpr double tempoBpm = 120.0;
        constexpr int beatsPerBar = 4;

        // Am - F - C - G, bass note + triad, one bar each.
        constexpr int progression[4][4] = {
            { 45, 57, 60, 64 },
            { 41, 57, 60, 65 },
            { 48, 55, 60, 64 },
            { 43, 55, 59, 62 },
        };
    }

    EngineSession::EngineSession (te::Engine& e)
        : engine (e),
          edit (te::Edit::createSingleTrackEdit (e))
    {
        createChordProgression();
        useBuiltInSynth();
        edit->getTransport().ensureContextAllocated();
    }

    EngineSession::~EngineSession()
    {
        edit->getTransport().stop (false, true);

        if (auto p = getInstrument())
            p->hideWindowForShutdown();
    }

    te::AudioTrack& EngineSession::getTrack() const
    {
        auto tracks = te::getAudioTracks (*edit);
        jassert (! tracks.isEmpty());
        return *tracks.getFirst();
    }

    void EngineSession::createChordProgression()
    {
        edit->tempoSequence.getTempo (0)->setBpm (tempoBpm);

        const auto numBars = (int) std::size (progression);
        const auto endBeat = te::BeatPosition::fromBeats (numBars * beatsPerBar);
        const auto endTime = edit->tempoSequence.toTime (endBeat);

        auto clip = getTrack().insertMIDIClip ("Chords", { te::TimePosition(), endTime }, nullptr);
        auto& sequence = clip->getSequence();

        for (int bar = 0; bar < numBars; ++bar)
            for (auto noteNumber : progression[bar])
                sequence.addNote (noteNumber,
                                  te::BeatPosition::fromBeats (bar * beatsPerBar),
                                  te::BeatDuration::fromBeats (beatsPerBar - 0.1),
                                  70, 0, nullptr);

        auto& transport = edit->getTransport();
        transport.setLoopRange ({ te::TimePosition(), endTime });
        transport.looping = true;
    }

    void EngineSession::removeInstrument()
    {
        for (auto p : getTrack().pluginList.getPlugins())
        {
            if (p->isSynth())
            {
                p->hideWindowForShutdown();
                p->deleteFromParent();
            }
        }
    }

    juce::String EngineSession::setInstrument (te::Plugin::Ptr plugin)
    {
        if (plugin == nullptr)
            return "Plugin could not be created";

        if (auto external = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
            if (external->getAudioPluginInstance() == nullptr)
                return "Plugin failed to load: " + external->getLoadError();

        const bool wasPlaying = isPlaying();
        stop();
        removeInstrument();
        getTrack().pluginList.insertPlugin (plugin, 0, nullptr);
        edit->getTransport().ensureContextAllocated (true);

        if (wasPlaying)
            play();

        return {};
    }

    juce::String EngineSession::useBuiltInSynth()
    {
        return setInstrument (edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}));
    }

    juce::String EngineSession::useExternalInstrument (const juce::PluginDescription& desc)
    {
        return setInstrument (edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc));
    }

    te::Plugin* EngineSession::getInstrument() const
    {
        for (auto p : getTrack().pluginList.getPlugins())
            if (p->isSynth())
                return p;

        return nullptr;
    }

    juce::String EngineSession::getInstrumentName() const
    {
        if (auto p = getInstrument())
            return p->getName();

        return "none";
    }

    void EngineSession::play()
    {
        auto& transport = edit->getTransport();
        transport.ensureContextAllocated();
        transport.play (false);
    }

    void EngineSession::stop()
    {
        edit->getTransport().stop (false, false);
    }

    bool EngineSession::isPlaying() const
    {
        return edit->getTransport().isPlaying();
    }

    double EngineSession::getPositionSeconds() const
    {
        return edit->getTransport().getPosition().inSeconds();
    }

    double EngineSession::getLoopLengthSeconds() const
    {
        return edit->getTransport().getLoopRange().getLength().inSeconds();
    }

    bool EngineSession::renderToFile (const juce::File& file)
    {
        stop();
        file.deleteFile();
        return te::Renderer::renderToFile (*edit, file, false) && file.existsAsFile();
    }
}
