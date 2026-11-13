#pragma once

#include "audio/ExportFormat.h"
#include "core/Session.h"

/** What every export is made of (audio/Export.cpp and audio/ExportJob.cpp build on it): which
    tracks are heard, the song's state changed for the time of one render, and a render done a
    bit at a time, so whoever asked for it can show how far it is and stop it. */
namespace harmonia::audio::export_render
{
    /** The tracks an export of the song plays: song, group and return tracks as they're heard
        (muted ones left out; while some are soloed, only those and the solo-safe ones), and the
        edit's other tracks (tempo, master...). */
    struct Audible
    {
        juce::Array<te::Track*> others;
        juce::Array<te::AudioTrack*> songTracks, groupTracks, returnTracks;
    };

    Audible findAudible (te::Edit&);

    /** The edit's tracks that aren't audio tracks (tempo, master...): every render has them. */
    juce::Array<te::Track*> getOtherTracks (te::Edit&);

    /** The song tracks, or the groups, top to bottom. */
    juce::Array<te::AudioTrack*> getTracks (te::Edit&, bool groups);

    /** Where the song's last clip ends (core::clips knows which clips count). */
    te::TimePosition getSongEnd (te::Edit&);

    //==============================================================================
    /** Property changes made straight in the edit's state for one render and put back after
        it: not undo steps, nor changes to save. */
    class TemporaryState final
    {
    public:
        explicit TemporaryState (te::Edit&);
        ~TemporaryState();

        /** Heard whatever its mute and the others' solo say. */
        void hear (te::AudioTrack&);

        /** Played straight out rather than into its group (which this render doesn't have). */
        void playToMaster (te::AudioTrack&);

        /** Its volume and pan left out: the sound as it comes to the fader. */
        void bypassFader (te::AudioTrack&);

        /** Its clips alone: none of its plugins (effects, sends, volume and pan). */
        void bypassPlugins (te::AudioTrack&);

        /** Silent, though still feeding the effects that listen to it: Tracktion keeps a muted
            track playing for them, without its sends to the return tracks. */
        void keepAsSource (te::AudioTrack&);

    private:
        te::Edit& edit;
        bool wasChanged;
        std::vector<std::tuple<juce::ValueTree, juce::Identifier, std::optional<juce::var>>> saved;

        /** A void value removes the property. */
        void set (juce::ValueTree, const juce::Identifier&, const juce::var&);

        JUCE_DECLARE_NON_COPYABLE (TemporaryState)
    };

    /** The tracks the effects of `heard` listen to (sidechain), added to `tracks` silent: a part
        of the song rendered alone sounds as it does in the whole song. */
    juce::Array<te::AudioTrack*> keepSources (TemporaryState&, const juce::Array<te::AudioTrack*>& heard,
                                              juce::Array<te::Track*>& tracks);

    //==============================================================================
    /** These tracks rendered into one file, a bit at a time, on the message thread. Unlike
        Renderer::renderToFile it changes nothing in the edit: that one solo-isolates and unmutes
        the rendered tracks for the time being, which Tracktion counts as a change to save (the
        project showed unsaved changes after an export). The tracks given here are all heard
        anyway. While it lives the engine plays nothing; between two runs the song must stay as
        it is. */
    class Render final
    {
    public:
        /** `sampleRate` 0: the audio device's. An MP3 is rendered as a WAV next to it first. */
        Render (te::Edit&, const juce::File&, const juce::Array<te::Track*>& tracks, te::TimeRange, bool withMaster,
                const juce::Array<te::AudioTrack*>& silentSources = {}, const ExportFormat& = {}, double sampleRate = 0);

        /** Unfinished, it takes what it wrote with it. */
        ~Render();

        /** Renders until the clock (juce::Time::getMillisecondCounterHiRes) says `untilMs`, and
            at least one block. True once there is nothing left to do: then see getError. */
        bool run (double untilMs);

        /** 0..1. */
        float getProgress() const noexcept;

        /** The sound is rendered; the MP3 is made of it next (that holds the thread up a while). */
        bool isEncoding() const noexcept                    { return stage == Stage::encoding; }

        /** Empty if all went well (once run said true). */
        const juce::String& getError() const noexcept       { return error; }

    private:
        enum class Stage { rendering, encoding, done };

        te::Edit& edit;
        const juce::File file;
        const ExportFormat format;
        std::unique_ptr<juce::TemporaryFile> wav;       // what an MP3 is made from
        juce::File rendered;                            // what the engine writes: the file, or that WAV
        std::optional<te::Edit::ScopedRenderStatus> renderStatus;
        std::unique_ptr<te::Renderer::RenderTask> task;
        std::atomic<float> progress { 0.0f };
        Stage stage = Stage::rendering;
        juce::String error;

        void finish (const juce::String& problem);
        void release (const juce::File&);

        JUCE_DECLARE_NON_COPYABLE (Render)
    };

    //==============================================================================
    /** Tracks rendered from the song's start to well past its end, a bit at a time, to be cut
        where their sound has died away (audio::export_tail). */
    class PastEndRender final
    {
    public:
        /** `sampleRate` 0: the audio device's. `forGain`: the file will be turned up or down as
            it is written (write), so it is rendered with nothing clipped and no dither yet. */
        PastEndRender (te::Edit&, const juce::File& target, const juce::Array<te::Track*>& tracks, bool withMaster,
                       const juce::Array<te::AudioTrack*>& silentSources, const ExportFormat&, double sampleRate = 0,
                       bool forGain = false);
        ~PastEndRender();

        /** As Render::run. */
        bool run (double untilMs);
        float getProgress() const noexcept;
        const juce::String& getError() const noexcept       { return error; }

        /** Where its sound ends (once run said true, without an error). */
        double getLastSoundSeconds() const noexcept         { return lastSoundSeconds; }

        /** Writes the target file of what was rendered, `seconds` long, turned by `gainDb` (only
            what was made `forGain`): an error, or empty. */
        juce::String write (double seconds, float gainDb = 0.0f);

        const juce::File& getTarget() const noexcept        { return target; }

        /** What was rendered, past the song's end and all (once run said true): to measure it. */
        juce::File getRendered() const                      { return wav != nullptr ? wav->getFile() : juce::File(); }

    private:
        te::Edit& edit;
        const juce::File target;
        const ExportFormat format;
        std::unique_ptr<juce::TemporaryFile> wav;           // gone with this
        std::unique_ptr<Render> render;
        double songEndSeconds = 0.0, lastSoundSeconds = 0.0;
        juce::String error;
        bool finished = false;

        JUCE_DECLARE_NON_COPYABLE (PastEndRender)
    };
}
