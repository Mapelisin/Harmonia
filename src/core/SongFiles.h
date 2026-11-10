#pragma once

#include "core/Project.h"

/** The sound files a song uses: audio clips and their takes (frozen tracks' files among them), the
    drum machine's samples, the sampler's sound. Which are missing, bringing them all into the
    song's own folder, and finding the missing ones again.

    A song keeps a file's place, not the file. The song's own sound folder is "<song> Audio" next
    to the project file (new recordings go there too): what is in it is saved relative to the
    project file, so the project file and that folder can be moved or copied together. */
namespace harmonia::core::song_files
{
    /** What the instruments' nodes that name a sound are called (audio::drum_kit and
        audio::sampler keep to these). */
    namespace ids
    {
        inline const juce::Identifier drumSample ("SAMPLE");            // a child of the drum machine's SAMPLES
        inline const juce::Identifier file ("file");                    // on a drum sample, on the sampler
        inline constexpr const char* samplerType = "harmoniaSampler";   // the sampler's plugin type
    }

    /** Calls `fn` with every node of a song's tree that names a sound file, and the property that
        names it. */
    void forEachReference (juce::ValueTree tree, const std::function<void (juce::ValueTree&, const juce::Identifier&)>& fn);

    /** The file a song's node names (a path may be relative to the project file). */
    juce::File resolve (const te::Edit&, const juce::String& path);

    /** A place in the song that names a sound file. */
    struct Reference
    {
        enum class Kind { clip, take, drumSample, samplerSound };

        Kind kind = Kind::clip;
        juce::ValueTree node;
        juce::Identifier property;
        juce::File file;
        juce::String usedBy;        // "Vocal on Audio 1", "a take of Vocal on Audio 1", "a drum of Drums", "the sampler of Lead"

        bool isMissing() const      { return ! file.existsAsFile(); }
    };

    std::vector<Reference> getAll (te::Edit&);

    /** A file the song names that isn't there, and what uses it. */
    struct Missing
    {
        juce::File file;
        juce::StringArray usedBy;
    };

    /** Each missing file once, by name. */
    std::vector<Missing> getMissing (te::Edit&);

    //==============================================================================
    /** "<song> Audio" next to the project file: the song's own sound folder (no file: none). */
    juce::File getFolder (const juce::File& projectFile);

    /** What collect() did. */
    struct Collected
    {
        juce::File folder;
        int copied = 0;             // files copied into the folder
        int already = 0;            // files that were there
        int missing = 0;            // files that aren't anywhere: left as they are
        int changed = 0;            // places of the song that use another file now
        juce::int64 bytes = 0;      // copied
        juce::StringArray problems; // files that couldn't be copied

        /** "Copied 3 files (12.4 MB) into ...; 5 were there already; 1 is missing". */
        juce::String describe() const;
    };

    /** Copies every sound the song uses that isn't in its own sound folder into it (a name taken
        by another sound gets a number; the same sound isn't copied twice) and makes the song use
        the copies. One undo step ("Collect files"); the files copied stay. The originals aren't
        touched. `projectFile` is where the song is saved. */
    Collected collect (Project&, const juce::File& projectFile);

    /** What findIn() did. */
    struct Found
    {
        int found = 0;
        int stillMissing = 0;
    };

    /** Looks for the missing files by their names in `folder` and the folders inside it, and makes
        the song use those found. One undo step ("Find missing files"). */
    Found findIn (Project&, const juce::File& folder);

    /** Makes every place of the song that names `from` name `to` (into the current undo step).
        Returns how many places. */
    int repoint (Project&, const juce::File& from, const juce::File& to);
}
