#include "audio/Sampler.h"

namespace harmonia::audio::sampler
{
    namespace
    {
        const juce::Identifier fileId ("file"), rootId ("root");

        constexpr float silent = 1.0e-4f;                   // -80 dB: the note is over
        constexpr double minReleaseSeconds = 0.003;         // never a click when a key is let go

        /** One note, playing. */
        struct Voice
        {
            const SampleData* data = nullptr;
            int note = -1, channel = 1;
            bool active = false, releasing = false, heldByPedal = false, oneShot = false;
            juce::int64 age = 0;
            double position = 0.0, step = 1.0;
            float level = 1.0f, envelope = 1.0f, attackStep = 0.0f, releaseFactor = 1.0f;

            void start (const SampleData& sample, int key, int midiChannel, float velocity, const Settings& s, double rate)
            {
                data = &sample;
                note = key;
                channel = midiChannel;
                active = sample.audio.getNumSamples() > 0;
                releasing = heldByPedal = false;
                oneShot = s.oneShot;
                age = 0;
                position = 0.0;
                step = sample.sampleRate / rate * std::pow (2.0, ((double) (key - s.root) + (double) s.tune) / 12.0);
                level = s.gain * std::pow (juce::jlimit (0.0f, 1.0f, velocity), 1.5f);

                const auto attackSamples = s.attackSeconds * rate;
                envelope = attackSamples >= 1.0 ? 0.0f : 1.0f;
                attackStep = attackSamples >= 1.0 ? (float) (1.0 / attackSamples) : 0.0f;
                releaseFactor = (float) std::exp (std::log (0.001) / std::max (1.0, std::max (minReleaseSeconds, s.releaseSeconds) * rate));
            }

            void letGo()
            {
                releasing = true;
                heldByPedal = false;
            }

            void render (float* left, float* right, int numSamples, double bendRatio)
            {
                const auto end = (double) data->audio.getNumSamples();

                for (int i = 0; i < numSamples; ++i)
                {
                    if (position >= end)
                    {
                        active = false;
                        return;
                    }

                    const auto gain = level * envelope;
                    left[i] += gain * data->getSampleAt (0, position);
                    right[i] += gain * data->getSampleAt (1, position);
                    position += step * bendRatio;
                    ++age;

                    if (releasing)
                    {
                        envelope *= releaseFactor;

                        if (envelope < silent)
                        {
                            active = false;
                            return;
                        }
                    }
                    else if (envelope < 1.0f)
                    {
                        envelope = std::min (1.0f, envelope + attackStep);
                    }
                }
            }
        };

        bool isChannel (int channel)
        {
            return channel >= 1 && channel <= 16;
        }
    }

    //==============================================================================
    struct Processor::State
    {
        std::array<Voice, (size_t) maxVoices> voices;
        std::array<double, 17> bendRatio {};        // by channel (1..16)
        std::array<bool, 17> pedalDown {};

        // `playing` is the audio thread's. A new sound waits in `waiting` until beginBlock swaps
        // them; the old one then waits there for setSample (or the end) to let it go, so nothing
        // is freed on the audio thread.
        SampleData::Ptr playing, waiting;
        bool isNew = false;
        juce::SpinLock lock;

        State()     { bendRatio.fill (1.0); }
    };

    Processor::Processor()
        : state (std::make_unique<State>())
    {
    }

    Processor::~Processor() = default;

    void Processor::prepare (double rate)
    {
        sampleRate = rate > 0.0 ? rate : 44100.0;
        reset();
    }

    void Processor::reset()
    {
        for (auto& voice : state->voices)
            voice.active = false;

        state->bendRatio.fill (1.0);
        state->pedalDown.fill (false);
    }

    void Processor::setSample (SampleData::Ptr sample)
    {
        SampleData::Ptr old;

        {
            const juce::SpinLock::ScopedLockType scopedLock (state->lock);
            old = std::move (state->waiting);
            state->waiting = std::move (sample);
            state->isNew = true;
        }
    }

    void Processor::beginBlock()
    {
        const juce::SpinLock::ScopedTryLockType scopedLock (state->lock);

        if (! scopedLock.isLocked() || ! state->isNew)
            return;

        std::swap (state->playing, state->waiting);
        state->isNew = false;

        // What the notes were playing may go with the old sound.
        for (auto& voice : state->voices)
            if (voice.data != state->playing.get())
                voice.active = false;
    }

    void Processor::noteOn (int note, float velocity, int channel, const Settings& settings)
    {
        auto* sample = state->playing.get();

        if (sample == nullptr || velocity <= 0.0f || note < 0 || note > 127)
            return;

        channel = isChannel (channel) ? channel : 1;

        // The same key again: what it was playing fades out under the new note.
        for (auto& voice : state->voices)
            if (voice.active && voice.note == note && voice.channel == channel)
                voice.letGo();

        // A free voice, else the oldest one.
        Voice* chosen = nullptr;

        for (auto& voice : state->voices)
        {
            if (! voice.active)
            {
                chosen = &voice;
                break;
            }

            if (chosen == nullptr || voice.age > chosen->age)
                chosen = &voice;
        }

        chosen->start (*sample, note, channel, velocity, settings, sampleRate);
    }

    void Processor::noteOff (int note, int channel)
    {
        channel = isChannel (channel) ? channel : 1;

        for (auto& voice : state->voices)
        {
            if (! voice.active || voice.releasing || voice.oneShot || voice.note != note || voice.channel != channel)
                continue;

            if (state->pedalDown[(size_t) channel])
                voice.heldByPedal = true;
            else
                voice.letGo();
        }
    }

    void Processor::setPitchBend (int channel, float semitones)
    {
        if (isChannel (channel))
            state->bendRatio[(size_t) channel] = std::pow (2.0, (double) semitones / 12.0);
    }

    void Processor::setSustain (int channel, bool down)
    {
        if (! isChannel (channel))
            return;

        state->pedalDown[(size_t) channel] = down;

        if (! down)
            for (auto& voice : state->voices)
                if (voice.active && voice.heldByPedal && voice.channel == channel)
                    voice.letGo();
    }

    void Processor::allNotesOff()
    {
        state->pedalDown.fill (false);

        for (auto& voice : state->voices)
            if (voice.active)
                voice.letGo();
    }

    void Processor::render (float* left, float* right, int numSamples)
    {
        for (auto& voice : state->voices)
            if (voice.active)
                voice.render (left, right, numSamples, state->bendRatio[(size_t) voice.channel]);
    }

    int Processor::getNumPlaying() const
    {
        return (int) std::count_if (state->voices.begin(), state->voices.end(), [] (const Voice& v) { return v.active; });
    }

    //==============================================================================
    /** Keeps the sound part's sound as the song says: reads the file when it changes (message
        thread; a moment after the change, or at once when asked). */
    class Plugin::Loader final : private juce::ValueTree::Listener,
                                 private juce::AsyncUpdater
    {
    public:
        Loader (Plugin& p, Processor& pr)
            : plugin (p), processor (pr)
        {
            plugin.state.addListener (this);
            plugin.rootNow = getRoot (plugin);
            load();
        }

        ~Loader() override
        {
            cancelPendingUpdate();
            plugin.state.removeListener (this);
        }

        void updateNow()
        {
            cancelPendingUpdate();
            load();
        }

        SampleData::Ptr getSample() const       { return sample; }

    private:
        Plugin& plugin;
        Processor& processor;
        juce::String loadedPath;
        SampleData::Ptr sample;

        void load()
        {
            const auto file = getFile (plugin);
            const auto path = file == juce::File() ? juce::String() : file.getFullPathName();

            // The same file as before: nothing to read (unless it couldn't be read then).
            if (path == loadedPath && (sample != nullptr || path.isEmpty()))
                return;

            juce::String error;
            sample = path.isNotEmpty() ? SampleData::load (plugin.edit.engine, file, error) : SampleData::Ptr();
            loadedPath = path;
            processor.setSample (sample);
        }

        void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override
        {
            if (tree == plugin.state && property == fileId)
                triggerAsyncUpdate();

            if (tree == plugin.state && property == rootId)
                plugin.rootNow = getRoot (plugin);
        }

        void handleAsyncUpdate() override
        {
            load();
        }
    };

    //==============================================================================
    const char* Plugin::xmlTypeName = "harmoniaSampler";

    Plugin::Plugin (te::PluginCreationInfo info)
        : te::Plugin (info)
    {
        auto* undoManager = getUndoManager();
        volumeValue.referTo (state, "volume", undoManager, 0.0f);
        tuneValue.referTo (state, "tune", undoManager, 0.0f);
        attackValue.referTo (state, "attack", undoManager, 0.0f);
        releaseValue.referTo (state, "release", undoManager, 100.0f);
        modeValue.referTo (state, "mode", undoManager, 0.0f);

        const auto number = [] (const juce::String& text) { return text.getFloatValue(); };

        volume = addParam ("volume", "Volume", { -24.0f, 12.0f },
                           [] (float value) { return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB"; }, number);
        tune = addParam ("tune", "Tune", { -24.0f, 24.0f, 1.0f },
                         [] (float value)
                         {
                             const auto semitones = juce::roundToInt (value);
                             return (semitones > 0 ? "+" : "") + juce::String (semitones) + " st";
                         }, number);

        juce::NormalisableRange<float> attackRange (0.0f, 2000.0f), releaseRange (0.0f, 5000.0f);
        attackRange.setSkewForCentre (200.0f);
        releaseRange.setSkewForCentre (500.0f);
        const auto milliseconds = [] (float value) { return juce::String (juce::roundToInt (value)) + " ms"; };
        attack = addParam ("attack", "Attack", attackRange, milliseconds, number);
        release = addParam ("release", "Release", releaseRange, milliseconds, number);

        mode = addParam ("mode", "Mode", { 0.0f, 1.0f, 1.0f },
                         [] (float value) { return value >= 0.5f ? juce::String ("One shot") : juce::String ("Hold the key"); },
                         [] (const juce::String& text) { return text.trim().startsWithIgnoreCase ("One") ? 1.0f : 0.0f; });

        volume->attachToCurrentValue (volumeValue);
        tune->attachToCurrentValue (tuneValue);
        attack->attachToCurrentValue (attackValue);
        release->attachToCurrentValue (releaseValue);
        mode->attachToCurrentValue (modeValue);

        loader = std::make_unique<Loader> (*this, processor);
    }

    Plugin::~Plugin()
    {
        notifyListenersOfDeletion();
        loader.reset();

        for (auto* parameter : { &volume, &tune, &attack, &release, &mode })
            (*parameter)->detachFromCurrentValue();
    }

    void Plugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        te::copyPropertiesToCachedValues (v, volumeValue, tuneValue, attackValue, releaseValue, modeValue);

        for (auto* parameter : getAutomatableParameters())
            parameter->updateFromAttachedValue();

        // Which sound and which root key are settings too (a preset has them).
        if (v != state)
        {
            auto* undoManager = getUndoManager();

            for (const auto& id : { fileId, rootId })
            {
                if (v.hasProperty (id))
                    state.setProperty (id, v[id], undoManager);
                else
                    state.removeProperty (id, undoManager);
            }
        }
    }

    void Plugin::updateSampleNow()
    {
        loader->updateNow();
    }

    SampleData::Ptr Plugin::getSample() const
    {
        return loader->getSample();
    }

    Settings Plugin::getSettings() const
    {
        Settings settings;
        settings.gain = juce::Decibels::decibelsToGain (volume->getCurrentValue());
        settings.tune = tune->getCurrentValue();
        settings.attackSeconds = attack->getCurrentValue() / 1000.0;
        settings.releaseSeconds = release->getCurrentValue() / 1000.0;
        settings.oneShot = mode->getCurrentValue() >= 0.5f;
        settings.root = rootNow.load();
        return settings;
    }

    void Plugin::initialise (const te::PluginInitialisationInfo& info)
    {
        sampleRate = info.sampleRate;
        processor.prepare (info.sampleRate);
    }

    void Plugin::reset()
    {
        processor.reset();
    }

    void Plugin::midiPanic()
    {
        processor.reset();
    }

    void Plugin::applyToBuffer (const te::PluginRenderContext& context)
    {
        if (context.destBuffer == nullptr || context.bufferNumSamples <= 0)
            return;

        auto& buffer = *context.destBuffer;
        const auto start = context.bufferStartSample, count = context.bufferNumSamples;

        for (int c = 0; c < buffer.getNumChannels(); ++c)
            buffer.clear (c, start, count);

        if (buffer.getNumChannels() == 0)
            return;

        auto* left = buffer.getWritePointer (0, start);
        auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;

        const auto renderUpTo = [&] (int from, int to)
        {
            if (right != nullptr)
            {
                processor.render (left + from, right + from, to - from);
                return;
            }

            // One channel: both sides, then halved.
            float other[256];

            for (int at = from; at < to; at += (int) std::size (other))
            {
                const auto n = std::min ((int) std::size (other), to - at);
                std::fill (other, other + n, 0.0f);
                processor.render (left + at, other, n);

                for (int i = 0; i < n; ++i)
                    left[at + i] = 0.5f * (left[at + i] + other[i]);
            }
        };

        processor.beginBlock();
        const auto settings = getSettings();
        auto done = 0;

        if (auto* midi = context.bufferForMidiMessages)
        {
            if (midi->isAllNotesOff)
                processor.allNotesOff();

            for (auto& message : *midi)
            {
                // Timestamps are seconds from the buffer's first sample (as the 4OSC reads them).
                const auto at = juce::jlimit (start, start + count - 1, juce::roundToInt (message.getTimeStamp() * sampleRate)) - start;

                if (at > done)
                {
                    renderUpTo (done, at);
                    done = at;
                }

                const auto channel = message.getChannel();

                if (message.isNoteOn())
                    processor.noteOn (message.getNoteNumber(), message.getFloatVelocity(), channel, settings);
                else if (message.isNoteOff())
                    processor.noteOff (message.getNoteNumber(), channel);
                else if (message.isPitchWheel())
                    processor.setPitchBend (channel, bendSemitones * (float) (message.getPitchWheelValue() - 8192) / 8192.0f);
                else if (message.isSustainPedalOn() || message.isSustainPedalOff())
                    processor.setSustain (channel, message.isSustainPedalOn());
                else if (message.isAllNotesOff() || message.isAllSoundOff())
                    processor.allNotesOff();
            }
        }

        if (done < count)
            renderUpTo (done, count);
    }

    //==============================================================================
    juce::File getFile (const te::Plugin& sampler)
    {
        const auto path = sampler.state[fileId].toString();
        return path.isNotEmpty() ? juce::File (path) : juce::File();
    }

    bool isFileMissing (const te::Plugin& sampler)
    {
        const auto file = getFile (sampler);
        return file != juce::File() && ! file.existsAsFile();
    }

    juce::String setFile (te::Plugin& sampler, const juce::File& file)
    {
        if (const auto error = SampleData::check (sampler.edit.engine, file); error.isNotEmpty())
            return error;

        auto* undoManager = &sampler.edit.getUndoManager();
        undoManager->beginNewTransaction ("Sampler: " + file.getFileNameWithoutExtension());
        sampler.state.setProperty (fileId, file.getFullPathName(), undoManager);
        return {};
    }

    int getRoot (const te::Plugin& sampler)
    {
        return juce::jlimit (0, 127, (int) sampler.state.getProperty (rootId, defaultRoot));
    }

    void setRoot (te::Plugin& sampler, int note)
    {
        sampler.state.setProperty (rootId, juce::jlimit (0, 127, note), &sampler.edit.getUndoManager());
    }

    void registerType (te::Engine& engine)
    {
        engine.getPluginManager().createBuiltInType<Plugin>();
    }
}
