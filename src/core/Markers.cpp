#include "core/Markers.h"
#include "core/SongNodes.h"

namespace harmonia::core::markers
{
    namespace
    {
        const juce::Identifier listType ("HMARKERS"), markerType ("HMARKER"), beatId ("beat"), nameId ("name");

        juce::ValueTree getList (const te::Edit& edit)
        {
            return edit.state.getChildWithName (listType);
        }

        juce::ValueTree getOrCreateList (te::Edit& edit)
        {
            return song_nodes::getOrCreate (edit, listType);
        }
    }

    std::vector<Marker> getAll (const te::Edit& edit)
    {
        std::vector<Marker> all;

        for (const auto& child : getList (edit))
            if (child.hasType (markerType))
                all.push_back ({ child, (double) child[beatId], child[nameId].toString() });

        std::stable_sort (all.begin(), all.end(), [] (const Marker& a, const Marker& b) { return a.beat < b.beat; });
        return all;
    }

    Marker add (te::Edit& edit, double beat, const juce::String& name)
    {
        juce::ValueTree marker (markerType);
        marker.setProperty (beatId, std::max (0.0, beat), nullptr);
        marker.setProperty (nameId, name, nullptr);
        getOrCreateList (edit).appendChild (marker, &edit.getUndoManager());
        return { marker, std::max (0.0, beat), name };
    }

    void move (te::Edit& edit, const Marker& marker, double beat)
    {
        auto state = marker.state;

        if (state.isValid())
            state.setProperty (beatId, std::max (0.0, beat), &edit.getUndoManager());
    }

    void rename (te::Edit& edit, const Marker& marker, const juce::String& name)
    {
        auto state = marker.state;

        if (state.isValid() && name.trim().isNotEmpty())
            state.setProperty (nameId, name.trim(), &edit.getUndoManager());
    }

    void remove (te::Edit& edit, const Marker& marker)
    {
        auto list = getList (edit);

        if (list.isValid() && marker.state.isAChildOf (list))
            list.removeChild (marker.state, &edit.getUndoManager());
    }

    std::optional<Marker> findAtOrBefore (const te::Edit& edit, double beat)
    {
        std::optional<Marker> found;

        for (const auto& m : getAll (edit))
            if (m.beat <= beat + 1.0e-9)
                found = m;

        return found;
    }

    std::optional<Marker> findAfter (const te::Edit& edit, double beat)
    {
        for (const auto& m : getAll (edit))
            if (m.beat > beat + 1.0e-9)
                return m;

        return std::nullopt;
    }

    std::pair<double, double> getSection (const te::Edit& edit, const Marker& marker, double songEndBeat)
    {
        if (const auto next = findAfter (edit, marker.beat))
            return { marker.beat, next->beat };

        return { marker.beat, std::max (songEndBeat, marker.beat) };
    }

    const juce::StringArray& getSuggestedNames()
    {
        static const juce::StringArray names { "Intro", "Verse", "Pre-chorus", "Chorus", "Bridge", "Drop",
                                               "Breakdown", "Build-up", "Solo", "Outro" };
        return names;
    }

    bool isMarkerTree (const juce::ValueTree& tree)
    {
        return tree.hasType (listType) || tree.hasType (markerType);
    }
}
