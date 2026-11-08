#include "core/Note.h"

namespace harmonia::core
{
    namespace
    {
        namespace ids
        {
            // Tracktion's own NOTE attributes (see tracktion_MidiList.cpp).
            const juce::Identifier pitch ("p"), start ("b"), length ("l"), velocity ("v"),
                                   release ("lift"), colour ("c"), mute ("m");

            // Harmonia's per-note attributes.
            const juce::Identifier pan ("hPan"), modX ("hModX"), modY ("hModY"),
                                   finePitch ("hFinePitch"), slide ("hSlide"), porta ("hPorta"),
                                   group ("hGroup");
        }

        const Note defaults;

        template <typename Value>
        void setOrRemove (juce::ValueTree& v, const juce::Identifier& id,
                          Value value, Value defaultValue, juce::UndoManager* um)
        {
            if (value == defaultValue)
                v.removeProperty (id, um);
            else if (v[id] != juce::var (value))
                v.setProperty (id, value, um);
        }

        template <typename Value>
        void setIfChanged (juce::ValueTree& v, const juce::Identifier& id, Value value, juce::UndoManager* um)
        {
            if (! v.hasProperty (id) || v[id] != juce::var (value))
                v.setProperty (id, value, um);
        }

        void writeHarmoniaProperties (juce::ValueTree& v, const Note& n, juce::UndoManager* um)
        {
            setOrRemove (v, ids::pan,       n.pan,            defaults.pan,            um);
            setOrRemove (v, ids::modX,      n.modX,           defaults.modX,           um);
            setOrRemove (v, ids::modY,      n.modY,           defaults.modY,           um);
            setOrRemove (v, ids::finePitch, n.finePitchCents, defaults.finePitchCents, um);
            setOrRemove (v, ids::slide,     n.slide,          defaults.slide,          um);
            setOrRemove (v, ids::porta,     n.porta,          defaults.porta,          um);
            setOrRemove (v, ids::group,     n.group,          defaults.group,          um);
        }
    }

    Note Note::clamped() const
    {
        Note n = *this;
        n.pitch           = juce::jlimit (0, 127, pitch);
        n.startBeat       = std::max (0.0, startBeat);
        n.lengthBeats     = std::max (0.0, lengthBeats);
        n.velocity        = juce::jlimit (0, 127, velocity);
        n.releaseVelocity = juce::jlimit (0, 127, releaseVelocity);
        n.colour          = juce::jlimit (0, note::numColours - 1, colour);
        n.pan             = juce::jlimit (-1.0f, 1.0f, pan);
        n.modX            = juce::jlimit (0.0f, 1.0f, modX);
        n.modY            = juce::jlimit (0.0f, 1.0f, modY);
        n.finePitchCents  = juce::jlimit (-note::maxFinePitchCents, note::maxFinePitchCents, finePitchCents);
        n.group           = std::max (0, group);
        return n;
    }

    namespace note
    {
        Note read (const te::MidiNote& source)
        {
            const auto& v = source.state;

            Note n;
            n.pitch           = source.getNoteNumber();
            n.startBeat       = source.getStartBeat().inBeats();
            n.lengthBeats     = source.getLengthBeats().inBeats();
            n.velocity        = source.getVelocity();
            n.releaseVelocity = source.getNoteOffVelocity();
            n.muted           = source.isMute();
            n.colour          = source.getColour();

            n.pan             = (float) v.getProperty (ids::pan,       defaults.pan);
            n.modX            = (float) v.getProperty (ids::modX,      defaults.modX);
            n.modY            = (float) v.getProperty (ids::modY,      defaults.modY);
            n.finePitchCents  = (float) v.getProperty (ids::finePitch, defaults.finePitchCents);
            n.slide           = (bool)  v.getProperty (ids::slide,     defaults.slide);
            n.porta           = (bool)  v.getProperty (ids::porta,     defaults.porta);
            n.group           = (int)   v.getProperty (ids::group,     defaults.group);
            return n;
        }

        void write (te::MidiNote& target, const Note& values, juce::UndoManager* um)
        {
            const auto n = values.clamped();
            auto& v = target.state;

            // Tracktion's setters keep its cached copies of these values up to date.
            target.setNoteNumber (n.pitch, um);
            target.setStartAndLength (te::BeatPosition::fromBeats (n.startBeat),
                                      te::BeatDuration::fromBeats (n.lengthBeats), um);
            target.setVelocity (n.velocity, um);
            target.setColour (n.colour, um);
            target.setMute (n.muted, um);

            // Tracktion sends note-off velocity 0 when "lift" is missing, so it is always stored.
            setIfChanged (v, ids::release, n.releaseVelocity, um);

            writeHarmoniaProperties (v, n, um);
        }

        juce::ValueTree createState (const Note& values)
        {
            const auto n = values.clamped();

            juce::ValueTree v (te::IDs::NOTE);
            v.setProperty (ids::pitch,    n.pitch,           nullptr);
            v.setProperty (ids::start,    n.startBeat,       nullptr);
            v.setProperty (ids::length,   n.lengthBeats,     nullptr);
            v.setProperty (ids::velocity, n.velocity,        nullptr);
            v.setProperty (ids::colour,   n.colour,          nullptr);
            v.setProperty (ids::release,  n.releaseVelocity, nullptr);

            if (n.muted)
                v.setProperty (ids::mute, 1, nullptr);

            writeHarmoniaProperties (v, n, nullptr);
            return v;
        }
    }
}
