#pragma once

#include "audio/FilePreview.h"

namespace harmonia::ui::browser
{
    /** One row of a folder in the browser. */
    struct Entry
    {
        enum class Kind { folder, sound, midi };

        juce::File file;
        Kind kind = Kind::folder;
    };

    /** What a folder shows: its folders, then its sounds and MIDI files, each by name; other files
        and hidden ones are left out. `filter` keeps the names that contain it (any case), and then
        the sounds and MIDI files in its subfolders are searched too (at most maxSearchResults, by
        name, those right in the folder first). */
    std::vector<Entry> listFolder (const juce::File& folder, const juce::String& filter,
                                   const std::function<bool (const juce::File&)>& isSound);

    inline constexpr int maxSearchResults = 500;

    /** How many files and folders a search looks at, at most (a search from a drive's root
        mustn't take forever). */
    inline constexpr int maxSearchScanned = 50000;

    /** A place on the left: one of the user's usual folders, a drive, or a folder they added. */
    struct Place
    {
        juce::String name;
        juce::File folder;
        bool added = false;     // added by the user: can be removed
    };

    /** The sound browser (lower panel, F6): places on the left, a folder's sounds on the right.
        A click on a sound plays it (again: stops); Up / Down move and play; double-click or Enter
        puts it into the song at the playhead; or drag it onto a track. A click opens a folder;
        Backspace or the Up button goes back up. The search looks in the subfolders too (each
        found file says which subfolder it is in). The places added and the last folder are kept
        in the settings. */
    class SoundBrowser final : public juce::Component
    {
    public:
        SoundBrowser (core::Session&, audio::FilePreview&);
        ~SoundBrowser() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        void setFolder (const juce::File&);
        juce::File getFolder() const                    { return folder; }
        void goUp();

        void setFilter (const juce::String&);
        juce::String getFilter() const;

        const std::vector<Entry>& getEntries() const noexcept   { return entries; }
        int indexOf (const juce::File&) const;

        /** What a click on a row does: opens a folder, plays a sound (or stops it if it is playing). */
        void clickEntry (int index);

        /** What a double-click does: opens a folder, or puts a sound or MIDI file into the song. */
        void openEntry (int index);

        const std::vector<Place>& getPlaces() const noexcept    { return places; }
        void addPlace (const juce::File&);
        void removePlace (const juce::File&);

        /** A sound or MIDI file to put into the song (double-click or Enter). */
        std::function<void (const juce::File&)> onAddToSong;

        /** Rows of the file list (for tests: what is shown). */
        juce::ListBox& getList() noexcept;

    private:
        class FileList;
        class PlacesList;

        core::Session& session;
        audio::FilePreview& preview;
        juce::File folder;
        std::vector<Entry> entries;
        std::vector<Place> places;

        juce::TextButton upButton { "Up" }, addPlaceButton { "+ Folder" }, stopButton { "Stop" };
        juce::TextEditor search;
        juce::Label pathLabel, placesLabel;
        std::unique_ptr<FileList> fileList;
        std::unique_ptr<PlacesList> placesList;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::uint32 folderOpenedAt = 0;      // a double-click's first click may have opened a folder

        void refresh();
        void rebuildPlaces();
        void savePlaces();
        void chooseFolderToAdd();
        void previewChanged();
        void previewIfSound (int index);
        bool isSound (const juce::File&) const;

        JUCE_DECLARE_NON_COPYABLE (SoundBrowser)
    };
}
