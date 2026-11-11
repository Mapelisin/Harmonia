#pragma once

#include "core/Project.h"

/** Songs to start new songs from. A template is a song kept in the templates folder
    (Documents/Harmonia/Templates) with everything it had when it was saved: tracks, instruments,
    effects, sends, groups, the tempo, and its clips too. A new song from a template is untitled
    and has nothing unsaved; the template itself is never changed by working on that song. One
    template can be the one every new song starts from. */
namespace harmonia::core::templates
{
    juce::File getDefaultFolder();

    /** A template's name: its file's, without the extension. */
    juce::String getName (const juce::File&);

    /** The templates in the folder, by name. */
    juce::Array<juce::File> list (const juce::File& folder = getDefaultFolder());

    /** The file a template called `name` is kept in (what a file name can't have is left out);
        none if nothing is left of the name. */
    juce::File getFileFor (const juce::String& name, const juce::File& folder = getDefaultFolder());

    /** Saves the song as a template called `name`, in place of one of that name. The song stays
        as it is: its own file, its unsaved changes. The sounds it uses stay where they are (the
        template names them by their full path). Returns an error message, empty on success. */
    juce::String save (Project&, const juce::String& name, const juce::File& folder = getDefaultFolder());

    /** A new song from a template: untitled, nothing unsaved, nothing to undo. Nothing if the
        template can't be opened (`error` says why). */
    std::unique_ptr<Project> createSong (Session&, const juce::File& templateFile, juce::String& error);

    /** The template new songs start from (File > New, and when Harmonia starts), kept in the
        settings. None: the empty song; a template that is gone counts as none. */
    juce::File getStartTemplate (Session&);
    void setStartTemplate (Session&, const juce::File&);

    /** A new song from the template new songs start from. Nothing if there is none (`problem`
        is then empty: the caller makes the empty song), or if it can't be opened (`problem`
        says so). */
    std::unique_ptr<Project> createStartSong (Session&, juce::String& problem);
}
