#include "core/ClipLinks.h"

namespace harmonia::core::clip_links
{
    namespace
    {
        const juce::Identifier linkGroupId ("hLinkGroup");

        juce::String getGroupId (const te::MidiClip& clip)
        {
            return clip.state[linkGroupId].toString();
        }

        juce::UndoManager* undoManagerFor (te::MidiClip& clip)
        {
            return &clip.edit.getUndoManager();
        }

        /** Makes `target` equal to `source` (properties and children, recursively), touching
            only what differs. Children are matched by position. */
        void mirror (const juce::ValueTree& source, juce::ValueTree& target, juce::UndoManager* um)
        {
            for (int i = target.getNumProperties(); --i >= 0;)
            {
                const auto name = target.getPropertyName (i);

                if (! source.hasProperty (name))
                    target.removeProperty (name, um);
            }

            for (int i = 0; i < source.getNumProperties(); ++i)
            {
                const auto name = source.getPropertyName (i);

                if (! target.hasProperty (name) || target[name] != source[name])
                    target.setProperty (name, source[name], um);
            }

            const auto numChildren = source.getNumChildren();

            for (int i = 0; i < numChildren; ++i)
            {
                const auto sourceChild = source.getChild (i);

                if (i >= target.getNumChildren())
                {
                    target.addChild (sourceChild.createCopy(), -1, um);
                }
                else if (auto targetChild = target.getChild (i); targetChild.hasType (sourceChild.getType()))
                {
                    mirror (sourceChild, targetChild, um);
                }
                else
                {
                    target.removeChild (i, um);
                    target.addChild (sourceChild.createCopy(), i, um);
                }
            }

            while (target.getNumChildren() > numChildren)
                target.removeChild (target.getNumChildren() - 1, um);
        }
    }

    bool isLinked (const te::MidiClip& clip)
    {
        return getGroupId (clip).isNotEmpty();
    }

    juce::Array<te::MidiClip*> getGroup (te::MidiClip& clip)
    {
        const auto groupId = getGroupId (clip);

        if (groupId.isEmpty())
            return { &clip };

        juce::Array<te::MidiClip*> group;

        for (auto* track : te::getClipTracks (clip.edit))
            for (auto* c : track->getClips())
                if (auto* midiClip = dynamic_cast<te::MidiClip*> (c))
                    if (getGroupId (*midiClip) == groupId)
                        group.add (midiClip);

        return group;
    }

    te::MidiClip* duplicate (te::MidiClip& clip, double startBeat, bool linked)
    {
        auto* um = undoManagerFor (clip);
        auto copy = te::duplicateClip (clip);
        auto* midiCopy = dynamic_cast<te::MidiClip*> (copy.get());

        if (midiCopy == nullptr)
            return nullptr;

        const auto startTime = clip.edit.tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat));
        midiCopy->setStart (startTime, false, true);

        if (linked)
        {
            if (! isLinked (clip))
                clip.state.setProperty (linkGroupId, juce::Uuid().toString(), um);

            midiCopy->state.setProperty (linkGroupId, getGroupId (clip), um);
        }
        else
        {
            midiCopy->state.removeProperty (linkGroupId, um);
        }

        return midiCopy;
    }

    void unlink (te::MidiClip& clip)
    {
        if (! isLinked (clip))
            return;

        auto* um = undoManagerFor (clip);
        auto group = getGroup (clip);

        clip.state.removeProperty (linkGroupId, um);
        group.removeFirstMatchingValue (&clip);

        if (group.size() == 1)
            group.getFirst()->state.removeProperty (linkGroupId, um);
    }

    void syncFrom (te::MidiClip& source)
    {
        if (! isLinked (source))
            return;

        auto* um = undoManagerFor (source);
        const auto& sourceSequence = source.getSequence().state;

        for (auto* copy : getGroup (source))
        {
            if (copy == &source)
                continue;

            auto targetSequence = copy->getSequence().state;
            mirror (sourceSequence, targetSequence, um);
        }
    }
}
