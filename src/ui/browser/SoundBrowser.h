#pragma once

#include "audio/FilePreview.h"
#include "ui/browser/SoundFavourites.h"

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

    /** A place on the left: one of the user's usual folders, a drive, a folder they added, or
        the favourite sounds (no folder: they are wherever they are). */
    struct Place
    {
        juce::String name;
        juce::File folder;
        bool added = false;         // added by the user: can be removed
        bool favourites = false;
    };

    /** The sound browser (lower panel, F6): places on the left, a folder's sounds on the right.
        A click on a sound plays it (again: stops); Up / Down move and play; double-click or Enter
        puts it into the song at the playhead; or drag it onto a track. A click opens a folder;
        Backspace or the Up button goes back up. The search looks in the subfolders too (each
        found file says which subfolder it is in). The places added and the last folder are kept
        in the settings.

        A click on the star of a row keeps the sound among the favourites (the first place on the
        left lists them, wherever they are). A loop that knows its tempo says it in its row, and
        with In tempo on it is played at the song's tempo: as it will sound in the song. */
    class SoundBrowser final : public juce::Component,
                               private juce::Timer
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

        /** The favourite sounds in place of a folder (Up, or any place, goes back to folders). */
        void showFavourites();
        bool isShowingFavourites() const noexcept       { return showingFavourites; }

        bool isFavourite (const juce::File&) const;
        void setFavourite (const juce::File&, bool shouldBe);

        /** What a click on a row's star does. */
        void toggleFavourite (int index);

        /** Whether a click at `x` of a row `rowWidth` wide is on its star. */
        static bool isOverStar (int x, int rowWidth) noexcept;

        /** Loops are played at the song's tempo (on at first; kept in the settings). */
        void setPlaysInTempo (bool);
        bool playsInTempo() const noexcept              { return inTempo; }

        /** The own tempo of a row's sound when it is a loop: what its row says. */
        std::optional<double> getLoopBpm (int index);

        /** What a row says of its tempo: "95 BPM", or, of a loop playing at the song's tempo,
            both; nothing for a sound that isn't a loop (or hasn't been looked at yet). */
        juce::String describeTempo (int index) const;

        /** The song's tempo where a sound would go: what loops are played at. */
        std::function<double()> getSongBpm;

        const std::vector<Place>& getPlaces() const noexcept    { return places; }
        void addPlace (const juce::File&);
        void removePlace (const juce::File&);

        /** A sound or MIDI file to put into the song (double-click or Enter). */
        std::function<void (const juce::File&)> onAddToSong;

        /** More items for a sound's right-click menu (what else the window can do with it). */
        std::function<void (juce::PopupMenu&, const juce::File&)> onSoundMenu;

        /** The right-click menu of a row (for tests). */
        juce::PopupMenu createMenu (int index);

        /** Rows of the file list (for tests: what is shown). */
        juce::ListBox& getList() noexcept;

    private:
        class FileList;
        class PlacesList;

        core::Session& session;
        audio::FilePreview& preview;
        Favourites favourites;
        juce::File folder;
        std::vector<Entry> entries;
        std::vector<Place> places;
        bool showingFavourites = false, inTempo = true;
        std::map<juce::String, std::optional<double>> loopBpms;     // of the sounds looked at so far, by path
        double loopBpmsFor = 0.0;                                   // the song tempo they were found for

        juce::TextButton upButton { "Up" }, addPlaceButton { "+ Folder" }, stopButton { "Stop" }, tempoButton { "In tempo" };
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
        void listen (const juce::File&);
        bool isSound (const juce::File&) const;
        double songBpm() const;
        std::optional<double> findLoopBpm (const juce::File&);
        juce::String describeFolderOf (const juce::File&) const;
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (SoundBrowser)
    };
}
