#include "audio/MidiRender.h"

namespace harmonia::audio::midi_render
{
    namespace
    {
        constexpr int drumChannel = 10;
        constexpr int numVoices = 24;
        constexpr double tailSeconds = 1.0;     // for the last notes to ring out
        constexpr double fadeSeconds = 0.05;
        constexpr float headroom = 0.8f;        // the loudest point, at most

        struct ToneSound final : juce::SynthesiserSound
        {
            bool appliesToNote (int) override                   { return true; }
            bool appliesToChannel (int channel) override        { return channel != drumChannel; }
        };

        struct DrumSound final : juce::SynthesiserSound
        {
            bool appliesToNote (int) override                   { return true; }
            bool appliesToChannel (int channel) override        { return channel == drumChannel; }
        };

        /** How a General MIDI drum sounds here: a falling tone, noise (bright for hats and
            cymbals), and how long it rings. */
        struct DrumShape
        {
            double toneHz = 0.0, decaySeconds = 0.15;
            float tone = 0.0f, noise = 0.0f;
            bool bright = false, kick = false;
        };

        DrumShape getDrumShape (int note)
        {
            switch (note)
            {
                case 35: case 36:                                   return { 50.0, 0.3, 1.0f, 0.0f, false, true };
                case 37: case 38: case 39: case 40:                 return { 190.0, 0.18, 0.4f, 0.6f, false, false };
                case 42: case 44:                                   return { 0.0, 0.05, 0.0f, 0.8f, true, false };
                case 46:                                            return { 0.0, 0.35, 0.0f, 0.8f, true, false };
                case 41: case 43: case 45: case 47: case 48: case 50:
                    return { 80.0 + (note - 41) * 12.0, 0.3, 0.9f, 0.1f, false, false };
                case 49: case 51: case 52: case 53: case 55: case 57: case 59:
                    return { 0.0, 0.9, 0.0f, 0.6f, true, false };
                default:                                            return { 0.0, 0.12, 0.0f, 0.7f, false, false };
            }
        }

        /** A soft triangle tone with an envelope; on the drum channel, a short hit. */
        class Voice final : public juce::SynthesiserVoice
        {
        public:
            bool canPlaySound (juce::SynthesiserSound* sound) override
            {
                return dynamic_cast<ToneSound*> (sound) != nullptr || dynamic_cast<DrumSound*> (sound) != nullptr;
            }

            void startNote (int note, float velocity, juce::SynthesiserSound* sound, int) override
            {
                const auto rate = getSampleRate();
                drum = dynamic_cast<DrumSound*> (sound) != nullptr;
                phase = drum ? 0.0 : random.nextDouble();      // notes started together don't add up to one spike
                seconds = 0.0;
                smoothed = 0.0f;
                lastNoise = 0.0f;
                finished = false;

                if (drum)
                {
                    shape = getDrumShape (note);
                    level = 0.3f + 0.5f * velocity;
                    gain = 1.0f;
                    decay = (float) std::pow (0.001, 1.0 / (shape.decaySeconds * rate));     // -60 dB when it ends
                }
                else
                {
                    level = 0.08f + 0.17f * velocity;
                    step = juce::MidiMessage::getMidiNoteInHertz (note) / rate;
                    envelope.setSampleRate (rate);
                    envelope.setParameters ({ 0.005f, 0.5f, 0.45f, 0.2f });
                    envelope.noteOn();
                }
            }

            void stopNote (float, bool allowTailOff) override
            {
                if (allowTailOff)
                {
                    if (! drum)         // a drum rings out on its own
                        envelope.noteOff();

                    return;
                }

                envelope.reset();
                clearCurrentNote();
            }

            void pitchWheelMoved (int) override {}
            void controllerMoved (int, int) override {}

            void renderNextBlock (juce::AudioBuffer<float>& out, int start, int count) override
            {
                if (! isVoiceActive())
                    return;

                for (int i = start; i < start + count; ++i)
                {
                    const auto sample = drum ? nextDrumSample() : nextToneSample();

                    if (finished)
                    {
                        clearCurrentNote();
                        return;
                    }

                    for (int channel = 0; channel < out.getNumChannels(); ++channel)
                        out.addSample (channel, i, sample);
                }
            }

        private:
            juce::ADSR envelope;
            juce::Random random;
            DrumShape shape;
            double phase = 0.0, step = 0.0, seconds = 0.0;
            float level = 0.0f, gain = 0.0f, decay = 0.0f, smoothed = 0.0f, lastNoise = 0.0f;
            bool drum = false, finished = false;

            float nextToneSample()
            {
                const auto amplitude = envelope.getNextSample();

                if (! envelope.isActive())
                {
                    finished = true;
                    return 0.0f;
                }

                const auto triangle = (float) (4.0 * std::abs (phase - 0.5) - 1.0);
                phase += step;

                if (phase >= 1.0)
                    phase -= 1.0;

                smoothed += 0.5f * (triangle - smoothed);       // a little softer than a bare triangle
                return smoothed * amplitude * level;
            }

            float nextDrumSample()
            {
                if (gain < 0.001f)
                {
                    finished = true;
                    return 0.0f;
                }

                const auto rate = getSampleRate();
                auto sample = 0.0f;

                if (shape.tone > 0.0f)
                {
                    // A kick falls from a knock to a low thump.
                    const auto hz = shape.kick ? shape.toneHz + 70.0 * std::exp (-seconds * 30.0) : shape.toneHz;
                    phase += hz / rate;

                    if (phase >= 1.0)
                        phase -= 1.0;

                    sample += shape.tone * (float) std::sin (phase * juce::MathConstants<double>::twoPi);
                }

                if (shape.noise > 0.0f)
                {
                    const auto noise = random.nextFloat() * 2.0f - 1.0f;
                    sample += shape.noise * (shape.bright ? 0.5f * (noise - lastNoise) : noise);
                    lastNoise = noise;
                }

                seconds += 1.0 / rate;
                const auto result = sample * gain * level;
                gain *= decay;
                return result;
            }
        };
    }

    juce::AudioBuffer<float> render (const juce::MidiFile& file, double sampleRate, double limitSeconds)
    {
        if (sampleRate <= 0.0 || limitSeconds <= 0.0)
            return {};

        auto midi = file;
        midi.convertTimestampTicksToSeconds();

        juce::MidiBuffer events;
        auto lastEvent = 0.0;
        auto hasNotes = false;

        for (int t = 0; t < midi.getNumTracks(); ++t)
        {
            for (const auto* event : *midi.getTrack (t))
            {
                const auto& message = event->message;

                if (! message.isNoteOnOrOff() && ! message.isSustainPedalOn() && ! message.isSustainPedalOff())
                    continue;

                const auto time = message.getTimeStamp();

                if (time < 0.0)
                    continue;

                lastEvent = std::max (lastEvent, time);     // a note held past the limit plays to it

                if (time >= limitSeconds)
                    continue;

                hasNotes = hasNotes || message.isNoteOn();
                events.addEvent (message, (int) std::round (time * sampleRate));
            }
        }

        if (! hasNotes)
            return {};

        juce::Synthesiser synth;

        for (int i = 0; i < numVoices; ++i)
            synth.addVoice (new Voice());

        synth.addSound (new ToneSound());
        synth.addSound (new DrumSound());
        synth.setCurrentPlaybackSampleRate (sampleRate);

        const auto length = (int) std::ceil (std::min (lastEvent + tailSeconds, limitSeconds) * sampleRate);
        juce::AudioBuffer<float> out (2, length);
        out.clear();

        // In one go: the synthesiser plays every event it is given, so a block must get all of them.
        synth.renderNextBlock (out, events, 0, length);

        // No click where it stops; no clipping however many notes play at once.
        const auto fade = std::min (length, (int) (fadeSeconds * sampleRate));

        for (int channel = 0; channel < out.getNumChannels(); ++channel)
            out.applyGainRamp (channel, length - fade, fade, 1.0f, 0.0f);

        if (const auto peak = out.getMagnitude (0, length); peak > headroom)
            out.applyGain (headroom / peak);

        return out;
    }

    juce::AudioBuffer<float> render (const juce::File& file, double sampleRate, double limitSeconds)
    {
        juce::FileInputStream in (file);
        juce::MidiFile midi;

        if (! in.openedOk() || ! midi.readFrom (in))
            return {};

        return render (midi, sampleRate, limitSeconds);
    }
}
