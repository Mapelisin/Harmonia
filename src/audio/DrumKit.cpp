#include "audio/DrumKit.h"

namespace harmonia::audio::drum_kit
{
    namespace
    {
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        constexpr float silent = 1.0e-4f;               // -80 dB: that part of the hit is over
        constexpr double fadeSeconds = 0.004;           // how fast a stopped hit goes
        constexpr double clickSeconds = 0.002;
        constexpr double burstGapSeconds = 0.011;       // between a clap's hands
        constexpr double burstSeconds = 0.03;           // how fast each of them dies away
        constexpr float kitGain = 0.6f;                 // a hit peaks about here: a whole groove stays under 0 dB

        // The classic drum machine cymbal: six square waves, out of tune with each other.
        constexpr float metalHz[] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
        constexpr float cowbellHz[] = { 540.0f, 800.0f };

        /** How a drum is made. A decay is how long that part takes to fall by 60 dB. */
        struct Recipe
        {
            // A sine sliding from startHz down to endHz (most of the way in `sweep` seconds), and a
            // second, steady one.
            float tone = 0.0f, startHz = 0.0f, endHz = 0.0f, sweep = 0.02f, toneDecay = 0.2f;
            float tone2 = 0.0f, tone2Hz = 0.0f;

            // Noise, kept between lowHz and highHz (0: no limit there).
            float noise = 0.0f, noiseDecay = 0.1f, noiseLowHz = 0.0f, noiseHighHz = 0.0f;
            int bursts = 1;                     // a clap: quick hits before the tail

            // Metal: the six squares (a cowbell: its two), above metalLowHz (a cowbell: around it).
            float metal = 0.0f, metalDecay = 0.1f, metalScale = 1.0f, metalLowHz = 6000.0f;
            bool cowbell = false;

            float click = 0.0f;                 // a knock at the very start
            float drive = 0.0f;                 // saturation, for weight
            float highHz = 0.0f;                // the whole hit through a low-pass (0: none)
            float pan = 0.0f;                   // -1 left .. 1 right, as the audience hears the kit
            float gain = 1.0f;                  // the whole hit
        };

        Recipe getBaseRecipe (Sound sound, int note)
        {
            Recipe r;

            switch (sound)
            {
                case Sound::kick:
                    r.tone = 1.0f; r.startHz = 170.0f; r.endHz = note == 35 ? 46.0f : 52.0f; r.sweep = 0.025f; r.toneDecay = 0.5f;
                    r.click = 0.3f; r.drive = 0.6f;
                    break;

                case Sound::snare:
                    r.tone = 0.4f; r.startHz = note == 40 ? 280.0f : 250.0f; r.endHz = note == 40 ? 210.0f : 190.0f; r.sweep = 0.012f;
                    r.toneDecay = 0.15f; r.tone2 = 0.4f; r.tone2Hz = 330.0f;
                    r.noise = 0.5f; r.noiseDecay = note == 40 ? 0.3f : 0.25f; r.noiseLowHz = 1200.0f; r.noiseHighHz = 9000.0f;
                    break;

                case Sound::rim:
                    r.tone = 0.4f; r.startHz = r.endHz = 1700.0f; r.toneDecay = 0.03f; r.tone2 = 0.8f; r.tone2Hz = 520.0f;
                    r.click = 0.3f;
                    break;

                case Sound::clap:
                    r.noise = 0.85f; r.noiseDecay = 0.2f; r.noiseLowHz = 900.0f; r.noiseHighHz = 2600.0f; r.bursts = 4;
                    break;

                case Sound::closedHat:
                case Sound::pedalHat:
                case Sound::openHat:
                    r.metal = sound == Sound::pedalHat ? 0.65f : 0.8f;
                    r.metalDecay = sound == Sound::closedHat ? 0.07f : sound == Sound::pedalHat ? 0.1f : 0.5f;
                    r.metalLowHz = 7000.0f;
                    r.noise = 0.2f; r.noiseDecay = sound == Sound::openHat ? 0.4f : 0.05f; r.noiseLowHz = 8000.0f;
                    r.pan = 0.3f;
                    break;

                case Sound::tom:
                {
                    // Floor tom (41) low and to the left, high tom (50) high and to the right.
                    constexpr int notes[] = { 41, 43, 45, 47, 48, 50 };
                    constexpr float hz[] = { 82.0f, 98.0f, 110.0f, 131.0f, 147.0f, 165.0f };
                    int i = 0;

                    while (i < 5 && notes[i] < note)
                        ++i;

                    r.tone = 0.9f; r.endHz = hz[i]; r.startHz = hz[i] * 1.6f; r.sweep = 0.04f; r.toneDecay = 0.5f;
                    r.noise = 0.08f; r.noiseDecay = 0.06f; r.noiseLowHz = 1500.0f; r.click = 0.1f;
                    r.pan = -0.35f + 0.14f * (float) i;
                    break;
                }

                case Sound::crash:
                {
                    const auto splash = note == 55;
                    r.metal = 0.35f; r.metalDecay = splash ? 0.7f : 1.8f; r.metalLowHz = 5000.0f; r.metalScale = note == 52 ? 0.8f : 1.1f;
                    r.noise = 0.3f; r.noiseDecay = splash ? 0.6f : 1.5f; r.noiseLowHz = 5000.0f;
                    r.pan = note == 57 ? -0.2f : 0.35f;
                    break;
                }

                case Sound::ride:
                    r.metal = 0.3f; r.metalDecay = 1.5f; r.metalLowHz = 3500.0f; r.metalScale = 1.35f;
                    r.noise = 0.08f; r.noiseDecay = 0.8f; r.noiseLowHz = 7000.0f;
                    r.tone = 0.08f; r.startHz = r.endHz = 3100.0f; r.toneDecay = 1.0f;
                    r.pan = -0.35f;
                    break;

                case Sound::rideBell:
                    r.tone = 0.3f; r.startHz = r.endHz = 760.0f; r.toneDecay = 1.2f; r.tone2 = 0.7f; r.tone2Hz = 1140.0f;
                    r.metal = 0.2f; r.metalDecay = 0.9f; r.metalLowHz = 3500.0f; r.metalScale = 1.35f;
                    r.pan = -0.35f;
                    break;

                case Sound::cowbell:
                    r.metal = 0.6f; r.metalDecay = 0.35f; r.metalLowHz = 800.0f; r.cowbell = true; r.pan = 0.15f;
                    break;

                case Sound::tambourine:
                    r.metal = 0.25f; r.metalDecay = 0.25f; r.metalLowHz = 8000.0f; r.metalScale = 1.6f;
                    r.noise = 0.3f; r.noiseDecay = 0.2f; r.noiseLowHz = 7000.0f; r.pan = 0.25f;
                    break;

                case Sound::shaker:
                    r.noise = 0.35f; r.noiseDecay = note == 69 ? 0.09f : 0.06f; r.noiseLowHz = 6000.0f; r.pan = -0.2f;
                    break;

                case Sound::woodblock:
                    r.tone = 0.6f; r.startHz = r.endHz = note == 75 ? 2500.0f : note == 76 ? 1900.0f : 1450.0f; r.toneDecay = 0.06f;
                    r.click = 0.15f; r.pan = 0.2f;
                    break;

                case Sound::hand:
                    if (note == 80 || note == 81)       // triangle
                    {
                        r.tone = 0.25f; r.startHz = r.endHz = 4200.0f; r.tone2 = 0.5f; r.tone2Hz = 5700.0f;
                        r.toneDecay = note == 81 ? 1.2f : 0.12f; r.pan = 0.3f;
                    }
                    else                                // bongos, congas, timbales, agogo...: higher up the keys
                    {
                        const auto hz = juce::jlimit (120.0f, 700.0f, 180.0f + (float) (note - 58) * 18.0f);
                        r.tone = 0.6f; r.startHz = hz * 1.15f; r.endHz = hz; r.sweep = 0.02f; r.toneDecay = 0.2f;
                        r.noise = 0.1f; r.noiseDecay = 0.03f; r.noiseLowHz = 2000.0f;
                        r.pan = (float) (note % 5 - 2) * 0.12f;
                    }
                    break;

                case Sound::none:
                    break;
            }

            return r;
        }

        Recipe getRecipe (Sound sound, int note, Kit kit)
        {
            auto r = getBaseRecipe (sound, note);

            if (kit == Kit::boomy)
            {
                switch (sound)
                {
                    case Sound::kick:
                        r.startHz = 120.0f; r.endHz = note == 35 ? 43.0f : 47.0f; r.sweep = 0.07f; r.toneDecay = 1.2f; r.click = 0.08f;
                        r.drive = 0.35f;
                        break;

                    case Sound::snare:
                        r.startHz = r.endHz = 238.0f; r.tone2Hz = 476.0f; r.tone2 = 0.35f; r.toneDecay = 0.1f;
                        r.noiseDecay = 0.16f; r.noiseLowHz = 1800.0f; r.noiseHighHz = 0.0f;
                        break;

                    case Sound::clap:
                        r.noiseDecay = 0.28f; r.noiseLowHz = 1000.0f; r.noiseHighHz = 2200.0f;
                        break;

                    case Sound::closedHat:
                    case Sound::pedalHat:
                        r.metalDecay *= 0.7f; r.metalLowHz = 8500.0f; r.noise *= 0.5f;
                        break;

                    case Sound::openHat:
                        r.metalDecay = 0.6f; r.metalLowHz = 8500.0f;
                        break;

                    case Sound::tom:
                        r.toneDecay = 0.8f; r.sweep = 0.07f; r.noise = 0.0f; r.startHz = r.endHz * 1.3f;
                        break;

                    case Sound::none: case Sound::rim: case Sound::crash: case Sound::ride: case Sound::rideBell:
                    case Sound::cowbell: case Sound::tambourine: case Sound::shaker: case Sound::woodblock: case Sound::hand:
                        break;
                }
            }
            else if (kit == Kit::soft)
            {
                r.highHz = 4500.0f;
                r.gain = 1.4f;                  // as loud as the others, with the top rolled off
                r.click *= 0.3f;
                r.drive = 0.0f;
                r.metal *= 0.7f;
                r.noise *= 0.85f;

                if (sound == Sound::kick)
                {
                    r.startHz = 130.0f; r.endHz = 55.0f; r.toneDecay = 0.35f;
                }
                else if (sound == Sound::snare)
                {
                    r.noiseHighHz = 5000.0f; r.noiseDecay = 0.18f;
                }
                else if (sound == Sound::crash || sound == Sound::ride)
                {
                    r.metalDecay *= 0.7f; r.noiseDecay *= 0.7f;
                }
            }

            return r;
        }

        /** Falls by 60 dB over `seconds`: the factor for each sample. */
        float decayFactor (double seconds, double sampleRate)
        {
            return (float) std::exp (std::log (0.001) / std::max (1.0, seconds * sampleRate));
        }

        /** A state-variable filter (topology-preserving: steady even near the top of the range). */
        struct Filter
        {
            float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f, ic1 = 0.0f, ic2 = 0.0f;

            void set (double hz, double q, double sampleRate)
            {
                const auto g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, 0.45 * sampleRate, hz) / sampleRate);
                k = (float) (1.0 / q);
                a1 = 1.0f / (1.0f + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
                ic1 = ic2 = 0.0f;
            }

            struct Out { float low, band, high; };

            Out process (float in)
            {
                const auto v3 = in - ic2;
                const auto v1 = a1 * ic1 + a2 * v3;
                const auto v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                return { v2, v1, in - k * v1 - v2 };
            }
        };
    }

    //==============================================================================
    const std::vector<Kit>& getKits()
    {
        static const std::vector<Kit> all { Kit::punchy, Kit::boomy, Kit::soft };
        return all;
    }

    juce::String getName (Kit kit)
    {
        switch (kit)
        {
            case Kit::punchy:   return "Punchy";
            case Kit::boomy:    return "808";
            case Kit::soft:     return "Lo-fi";
        }

        return {};
    }

    Sound getSound (int note)
    {
        switch (note)
        {
            case 35: case 36:                                   return Sound::kick;
            case 37:                                            return Sound::rim;
            case 38: case 40:                                   return Sound::snare;
            case 39:                                            return Sound::clap;
            case 42:                                            return Sound::closedHat;
            case 44:                                            return Sound::pedalHat;
            case 46:                                            return Sound::openHat;
            case 41: case 43: case 45: case 47: case 48: case 50: return Sound::tom;
            case 49: case 52: case 55: case 57:                 return Sound::crash;
            case 51: case 59:                                   return Sound::ride;
            case 53:                                            return Sound::rideBell;
            case 54:                                            return Sound::tambourine;
            case 56:                                            return Sound::cowbell;
            case 69: case 70:                                   return Sound::shaker;
            case 75: case 76: case 77:                          return Sound::woodblock;
            default:                                            return note >= 35 && note <= 81 ? Sound::hand : Sound::none;
        }
    }

    //==============================================================================
    /** One hit, ringing out. */
    struct Voice
    {
        Recipe r;
        int note = -1;
        bool active = false;
        juce::int64 age = 0;            // samples since the hit
        float level = 0.0f, left = 1.0f, right = 1.0f;

        double phase = 0.0, phase2 = 0.0, step2 = 0.0;
        float startHz = 0.0f, endHz = 0.0f, pitch = 1.0f, pitchFactor = 0.0f, toneLevel = 0.0f, toneFactor = 0.0f;

        juce::uint32 seed = 1;
        float noiseLevel = 0.0f, noiseFactor = 0.0f, burstFactor = 0.0f;
        int burstsLeft = 0;
        juce::int64 nextBurst = 0, burstGap = 0;
        Filter noiseLow, noiseHigh;

        std::array<double, 6> metalPhase {}, metalStep {};
        int metalCount = 0;
        float metalLevel = 0.0f, metalFactor = 0.0f;
        Filter metal1, metal2;

        Filter output;
        float fade = 1.0f, fadeFactor = 1.0f, driveIn = 1.0f, driveOut = 1.0f;
        bool fading = false;
        juce::int64 clickSamples = 0;

        void start (int key, const Recipe& recipe, float velocity, float tune, float decay, double rate, juce::uint32 newSeed)
        {
            r = recipe;
            note = key;
            active = true;
            age = 0;
            level = kitGain * r.gain * std::pow (juce::jlimit (0.0f, 1.0f, velocity), 1.5f);
            left = r.pan > 0.0f ? 1.0f - r.pan : 1.0f;
            right = r.pan < 0.0f ? 1.0f + r.pan : 1.0f;
            seed = newSeed | 1u;
            fade = 1.0f;
            fading = false;
            fadeFactor = decayFactor (fadeSeconds, rate);
            clickSamples = (juce::int64) (clickSeconds * rate);

            const auto tuning = std::pow (2.0f, tune / 12.0f);
            decay = juce::jlimit (0.1f, 4.0f, decay);

            phase = phase2 = 0.0;
            startHz = r.startHz * tuning;
            endHz = r.endHz * tuning;
            step2 = r.tone2Hz * tuning / rate;
            pitch = 1.0f;
            pitchFactor = (float) std::exp (-1.0 / std::max (1.0, r.sweep * rate));
            toneLevel = r.tone > 0.0f ? 1.0f : 0.0f;
            toneFactor = decayFactor (r.toneDecay * decay, rate);

            noiseLevel = r.noise > 0.0f ? 1.0f : 0.0f;
            noiseFactor = decayFactor (r.noiseDecay * decay, rate);
            burstFactor = decayFactor (burstSeconds, rate);
            burstsLeft = r.noise > 0.0f ? std::max (0, r.bursts - 1) : 0;
            burstGap = (juce::int64) (burstGapSeconds * rate);
            nextBurst = burstGap;
            noiseLow.set (r.noiseLowHz, 0.707, rate);
            noiseHigh.set (r.noiseHighHz, 0.707, rate);

            metalCount = r.cowbell ? 2 : 6;

            for (int i = 0; i < metalCount; ++i)
            {
                metalPhase[(size_t) i] = 0.0;
                metalStep[(size_t) i] = (r.cowbell ? cowbellHz[i] : metalHz[i] * r.metalScale) * tuning / rate;
            }

            metalLevel = r.metal > 0.0f ? 1.0f : 0.0f;
            metalFactor = decayFactor (r.metalDecay * decay, rate);
            metal1.set (r.metalLowHz * (r.cowbell ? tuning : 1.0f), r.cowbell ? 1.5 : 0.707, rate);
            metal2.set (r.metalLowHz, 0.707, rate);

            output.set (r.highHz, 0.707, rate);
            driveIn = 1.0f + 3.0f * r.drive;
            driveOut = 1.0f / std::tanh (driveIn);
        }

        void stop()
        {
            fading = true;
        }

        float nextNoise()
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return (float) (seed * (2.0 / 4294967296.0) - 1.0);
        }

        float next (double rate)
        {
            auto sample = 0.0f;

            if (toneLevel > 0.0f)
            {
                const auto hz = endHz + (startHz - endHz) * pitch;
                pitch *= pitchFactor;
                phase += hz / rate;
                phase -= std::floor (phase);
                auto tone = std::sin (phase * twoPi);

                if (r.tone2 > 0.0f)
                {
                    phase2 += step2;
                    phase2 -= std::floor (phase2);
                    tone += r.tone2 * std::sin (phase2 * twoPi);
                }

                sample += r.tone * toneLevel * (float) tone;
                toneLevel = toneLevel * toneFactor < silent ? 0.0f : toneLevel * toneFactor;
            }

            if (noiseLevel > 0.0f || burstsLeft > 0)
            {
                if (burstsLeft > 0 && age >= nextBurst)
                {
                    noiseLevel = 1.0f;
                    --burstsLeft;
                    nextBurst += burstGap;
                }

                auto noise = nextNoise();

                if (r.noiseLowHz > 0.0f)
                    noise = noiseLow.process (noise).high;

                if (r.noiseHighHz > 0.0f)
                    noise = noiseHigh.process (noise).low;

                sample += r.noise * noiseLevel * noise;
                noiseLevel *= burstsLeft > 0 ? burstFactor : noiseFactor;

                if (noiseLevel < silent && burstsLeft == 0)
                    noiseLevel = 0.0f;
            }

            if (metalLevel > 0.0f)
            {
                auto metal = 0.0f;

                for (size_t i = 0; i < (size_t) metalCount; ++i)
                {
                    metalPhase[i] += metalStep[i];
                    metalPhase[i] -= std::floor (metalPhase[i]);
                    metal += metalPhase[i] < 0.5 ? 1.0f : -1.0f;
                }

                metal /= (float) metalCount;
                metal = r.cowbell ? 0.7f * metal1.process (metal).band
                                  : metal2.process (metal1.process (metal).high).high;

                sample += r.metal * metalLevel * metal;
                metalLevel = metalLevel * metalFactor < silent ? 0.0f : metalLevel * metalFactor;
            }

            if (age < clickSamples)
                sample += r.click * (1.0f - (float) age / (float) clickSamples) * nextNoise();

            if (r.drive > 0.0f)
                sample = std::tanh (sample * driveIn) * driveOut;

            if (r.highHz > 0.0f)
                sample = output.process (sample).low;

            if (fading)
                fade *= fadeFactor;

            ++age;

            if ((toneLevel <= 0.0f && noiseLevel <= 0.0f && metalLevel <= 0.0f && burstsLeft == 0 && age > clickSamples)
                || fade < silent)
                active = false;

            return sample * level * fade;
        }
    };

    struct Processor::Voices
    {
        std::array<Voice, (size_t) maxVoices> voices;
        juce::uint32 hits = 0;
    };

    Processor::Processor()
        : voices (std::make_unique<Voices>())
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
        for (auto& voice : voices->voices)
            voice.active = false;

        voices->hits = 0;       // every render of the song sounds the same
    }

    void Processor::stopAll()
    {
        for (auto& voice : voices->voices)
            if (voice.active)
                voice.stop();
    }

    void Processor::noteOn (int note, float velocity, Kit kit, float tune, float decay)
    {
        const auto sound = getSound (note);

        if (sound == Sound::none || velocity <= 0.0f)
            return;

        // The same drum hit again stops it ringing; a closed (or pedal) hat stops the open one.
        const auto chokes = sound == Sound::closedHat || sound == Sound::pedalHat;

        for (auto& voice : voices->voices)
            if (voice.active && (voice.note == note || (chokes && getSound (voice.note) == Sound::openHat)))
                voice.stop();

        // A free voice, else the oldest one.
        Voice* chosen = nullptr;

        for (auto& voice : voices->voices)
        {
            if (! voice.active)
            {
                chosen = &voice;
                break;
            }

            if (chosen == nullptr || voice.age > chosen->age)
                chosen = &voice;
        }

        const auto seed = 0x9e3779b9u * (juce::uint32) (note + 1) + 0x85ebca6bu * ++voices->hits;
        chosen->start (note, getRecipe (sound, note, kit), velocity, tune, decay, sampleRate, seed);
    }

    void Processor::render (float* left, float* right, int numSamples)
    {
        for (auto& voice : voices->voices)
        {
            for (int i = 0; i < numSamples && voice.active; ++i)
            {
                const auto sample = voice.next (sampleRate);
                left[i] += sample * voice.left;
                right[i] += sample * voice.right;
            }
        }
    }

    int Processor::getNumPlaying() const
    {
        return (int) std::count_if (voices->voices.begin(), voices->voices.end(), [] (const Voice& v) { return v.active; });
    }

    //==============================================================================
    const char* Plugin::xmlTypeName = "harmoniaDrums";

    Plugin::Plugin (te::PluginCreationInfo info)
        : te::Plugin (info)
    {
        auto* undoManager = getUndoManager();
        kitValue.referTo (state, "kit", undoManager, (float) defaultKit);
        tuneValue.referTo (state, "tune", undoManager, defaultTune);
        decayValue.referTo (state, "decay", undoManager, defaultDecay);

        kit = addParam ("kit", "Kit", { 0.0f, (float) getKits().size() - 1.0f, 1.0f },
                        [] (float value) { return drum_kit::getName (getKits()[(size_t) juce::jlimit (0, (int) getKits().size() - 1,
                                                                                                    juce::roundToInt (value))]); },
                        [] (const juce::String& text)
                        {
                            for (size_t i = 0; i < getKits().size(); ++i)
                                if (text.trim() == drum_kit::getName (getKits()[i]))
                                    return (float) i;

                            return (float) defaultKit;
                        });
        tune = addParam ("tune", "Tune", { -12.0f, 12.0f, 1.0f },
                         [] (float value)
                         {
                             const auto semitones = juce::roundToInt (value);
                             return (semitones > 0 ? "+" : "") + juce::String (semitones) + " st";
                         },
                         [] (const juce::String& text) { return text.getFloatValue(); });
        decay = addParam ("decay", "Decay", { 25.0f, 200.0f },
                          [] (float value) { return juce::String (juce::roundToInt (value)) + "%"; },
                          [] (const juce::String& text) { return text.getFloatValue(); });

        kit->attachToCurrentValue (kitValue);
        tune->attachToCurrentValue (tuneValue);
        decay->attachToCurrentValue (decayValue);
    }

    Plugin::~Plugin()
    {
        notifyListenersOfDeletion();

        kit->detachFromCurrentValue();
        tune->detachFromCurrentValue();
        decay->detachFromCurrentValue();
    }

    void Plugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
    {
        te::copyPropertiesToCachedValues (v, kitValue, tuneValue, decayValue);

        for (auto* parameter : getAutomatableParameters())
            parameter->updateFromAttachedValue();
    }

    Kit Plugin::getKit() const
    {
        return getKits()[(size_t) juce::jlimit (0, (int) getKits().size() - 1, juce::roundToInt (kit->getCurrentValue()))];
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

        const auto kitNow = getKit();
        const auto tuneNow = tune->getCurrentValue();
        const auto decayNow = decay->getCurrentValue() / 100.0f;
        auto done = 0;

        if (auto* midi = context.bufferForMidiMessages)
        {
            if (midi->isAllNotesOff)
                processor.stopAll();

            for (auto& message : *midi)
            {
                // Timestamps are seconds from the buffer's first sample (as the 4OSC reads them).
                const auto at = juce::jlimit (start, start + count - 1, juce::roundToInt (message.getTimeStamp() * sampleRate)) - start;

                if (at > done)
                {
                    renderUpTo (done, at);
                    done = at;
                }

                if (message.isNoteOn())
                    processor.noteOn (message.getNoteNumber(), message.getFloatVelocity(), kitNow, tuneNow, decayNow);
                else if (message.isAllNotesOff() || message.isAllSoundOff())
                    processor.stopAll();
            }
        }

        if (done < count)
            renderUpTo (done, count);
    }

    void registerType (te::Engine& engine)
    {
        engine.getPluginManager().createBuiltInType<Plugin>();
    }
}
