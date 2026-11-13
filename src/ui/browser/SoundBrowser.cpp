#include "ui/browser/SoundBrowser.h"
#include "core/AudioTempo.h"
#include "core/MidiFiles.h"
#include "ui/FileDrag.h"
#include "ui/Theme.h"

namespace harmonia::ui::browser
{
    namespace
    {
        constexpr int listRowHeight = 22, placesWidth = 180, hintHeight = 18;
        constexpr int starWidth = 24, rowRightMargin = 6, sizeWidth = 72, tempoWidth = 120;
        constexpr juce::uint32 doubleClickAfterFolderMs = 800;
        constexpr int tempoTimerMs = 30;
        constexpr double tempoSliceMs = 4.0;        // how long the loops' tempi are looked for at a time
        constexpr size_t maxKnownTempi = 20000;
        const char* const placesKey = "soundBrowserPlaces";
        const char* const folderKey = "soundBrowserFolder";
        const char* const inTempoKey = "soundBrowserInTempo";

        juce::String describeSize (juce::int64 bytes)
        {
            if (bytes < 1024 * 1024)
                return juce::String (juce::jmax ((juce::int64) 1, bytes / 1024)) + " KB";

            return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB";
        }

        /** Small pictures in front of the names: a folder, a sound's waveform, a MIDI file's notes. */
        void drawIcon (juce::Graphics& g, juce::Rectangle<float> area, Entry::Kind kind, bool playing)
        {
            const auto box = area.withSizeKeepingCentre (14.0f, 11.0f);

            if (playing)
            {
                juce::Path triangle;
                triangle.addTriangle (box.getX() + 2.0f, box.getY(), box.getX() + 2.0f, box.getBottom(), box.getRight(), box.getCentreY());
                g.setColour (theme::colour::accent);
                g.fillPath (triangle);
                return;
            }

            switch (kind)
            {
                case Entry::Kind::folder:
                    g.setColour (theme::colour::warning.withAlpha (0.85f));
                    g.fillRoundedRectangle (box.withTrimmedTop (2.0f), 1.5f);
                    g.fillRoundedRectangle (box.withWidth (6.0f).withHeight (4.0f), 1.0f);
                    break;

                case Entry::Kind::sound:
                {
                    g.setColour (theme::colour::accent.withAlpha (0.8f));
                    const float heights[] { 0.4f, 0.9f, 0.6f, 1.0f, 0.5f };

                    for (int i = 0; i < 5; ++i)
                    {
                        const auto h = box.getHeight() * heights[i];
                        g.fillRect (box.getX() + (float) i * 3.0f, box.getCentreY() - h * 0.5f, 2.0f, h);
                    }

                    break;
                }

                case Entry::Kind::midi:
                    g.setColour (theme::colour::textSecondary);
                    g.fillRect (box.getX(), box.getY() + 1.0f, 6.0f, 2.0f);
                    g.fillRect (box.getX() + 4.0f, box.getCentreY() - 1.0f, 7.0f, 2.0f);
                    g.fillRect (box.getX() + 8.0f, box.getBottom() - 3.0f, 6.0f, 2.0f);
                    break;
            }
        }

        /** The star of a favourite (filled), or where a click makes one (an outline). */
        void drawStar (juce::Graphics& g, juce::Rectangle<float> area, bool favourite)
        {
            juce::Path star;
            star.addStar (area.getCentre(), 5, 2.6f, 6.0f);

            if (favourite)
            {
                g.setColour (theme::colour::warning);
                g.fillPath (star);
            }
            else
            {
                g.setColour (theme::colour::textSecondary.withAlpha (0.45f));
                g.strokePath (star, juce::PathStrokeType (1.0f));
            }
        }

        void paintStripe (juce::Graphics& g, int row, int width, int height, bool selected)
        {
            if (selected)
                g.fillAll (theme::colour::accent.withAlpha (0.22f));
            else if (row % 2 == 1)
                g.fillAll (theme::colour::surface.withAlpha (0.6f));

            juce::ignoreUnused (width, height);
        }
    }

    std::vector<Entry> listFolder (const juce::File& folder, const juce::String& filter,
                                   const std::function<bool (const juce::File&)>& isSound)
    {
        std::vector<Entry> folders, files;

        if (! folder.isDirectory())
            return {};

        const auto text = filter.trim();
        const auto searching = text.isNotEmpty();
        const auto types = juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles;
        int scanned = 0;

        // A search goes down into the subfolders too.
        for (const auto& item : juce::RangedDirectoryIterator (folder, searching, "*", types))
        {
            if (++scanned > maxSearchScanned || (int) files.size() >= maxSearchResults)
                break;

            const auto file = item.getFile();
            const auto inFolder = file.getParentDirectory() == folder;

            if (searching && ! file.getFileName().containsIgnoreCase (text))
                continue;

            if (item.isDirectory())
            {
                if (inFolder)
                    folders.push_back ({ file, Entry::Kind::folder });
            }
            else if (core::midi_files::isMidiFile (file))
                files.push_back ({ file, Entry::Kind::midi });
            else if (isSound != nullptr && isSound (file))
                files.push_back ({ file, Entry::Kind::sound });
        }

        const auto byName = [&folder] (const Entry& a, const Entry& b)
        {
            const auto aHere = a.file.getParentDirectory() == folder, bHere = b.file.getParentDirectory() == folder;

            if (aHere != bHere)
                return aHere;

            if (const auto order = a.file.getFileName().compareNatural (b.file.getFileName()); order != 0)
                return order < 0;

            return a.file.getFullPathName().compareNatural (b.file.getFullPathName()) < 0;
        };

        std::sort (folders.begin(), folders.end(), byName);
        std::sort (files.begin(), files.end(), byName);
        folders.insert (folders.end(), files.begin(), files.end());
        return folders;
    }

    //==============================================================================
    class SoundBrowser::FileList final : public juce::ListBox,
                                         private juce::ListBoxModel
    {
    public:
        explicit FileList (SoundBrowser& b)
            : owner (b)
        {
            setModel (this);
            setRowHeight (listRowHeight);
            setMultipleSelectionEnabled (true);
            setColour (juce::ListBox::backgroundColourId, theme::colour::background);
            setColour (juce::ListBox::outlineColourId, theme::colour::border);
            setOutlineThickness (1);
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            const auto before = getSelectedRow();
            const auto used = juce::ListBox::keyPressed (key);

            // Up / Down: hear each sound as you go.
            if (used && getSelectedRow() != before && ! key.getModifiers().isShiftDown())
                owner.previewIfSound (getSelectedRow());

            return used;
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            juce::ListBox::paintOverChildren (g);

            if (! owner.entries.empty())
                return;

            g.setColour (theme::colour::textSecondary);
            g.setFont (juce::FontOptions (theme::size::fontSize));
            g.drawText (owner.getFilter().isNotEmpty() ? "Nothing here matches the search"
                            : owner.showingFavourites ? "No favourites yet: click the star in a sound's row"
                                                      : "No sounds or MIDI files in this folder",
                        getLocalBounds(), juce::Justification::centred);
        }

    private:
        SoundBrowser& owner;

        int getNumRows() override
        {
            return (int) owner.entries.size();
        }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool isRowSelected) override
        {
            if (! juce::isPositiveAndBelow (row, (int) owner.entries.size()))
                return;

            const auto& entry = owner.entries[(size_t) row];
            const auto playing = owner.preview.isPlaying() && owner.preview.getFile() == entry.file;
            paintStripe (g, row, width, height, isRowSelected);

            auto area = juce::Rectangle<int> (0, 0, width, height).withTrimmedLeft (8).withTrimmedRight (rowRightMargin);
            drawIcon (g, area.removeFromLeft (18).toFloat(), entry.kind, playing);
            area.removeFromLeft (6);

            if (entry.kind != Entry::Kind::folder)
            {
                drawStar (g, area.removeFromRight (starWidth).toFloat(), owner.isFavourite (entry.file));

                g.setColour (theme::colour::textSecondary);
                g.setFont (juce::FontOptions (theme::size::smallFontSize));
                g.drawText (entry.kind == Entry::Kind::midi ? "MIDI  " + describeSize (entry.file.getSize())
                                                            : describeSize (entry.file.getSize()),
                            area.removeFromRight (sizeWidth), juce::Justification::centredRight, false);

                // A loop: its tempo (and the song's, while it plays at that one).
                if (const auto tempo = owner.describeTempo (row); tempo.isNotEmpty())
                {
                    g.setColour (playing ? theme::colour::accent : theme::colour::textSecondary);
                    g.drawText (tempo, area.removeFromRight (tempoWidth), juce::Justification::centredRight, false);
                }
            }

            // Found in a subfolder (or a favourite): which folder, after the name.
            const auto where = owner.describeFolderOf (entry.file);
            const juce::Font nameFont { juce::FontOptions (theme::size::fontSize) };
            const auto nameWidth = juce::jmin ((float) area.getWidth(),
                                               juce::GlyphArrangement::getStringWidth (nameFont, entry.file.getFileName()) + 2.0f);

            g.setColour (playing ? theme::colour::accent : theme::colour::textPrimary);
            g.setFont (nameFont);
            g.drawText (entry.file.getFileName(), area.removeFromLeft (juce::roundToInt (nameWidth)), juce::Justification::centredLeft, true);

            if (where.isNotEmpty() && area.getWidth() > 30)
            {
                g.setColour (theme::colour::textSecondary);
                g.setFont (juce::FontOptions (theme::size::smallFontSize));
                g.drawText (juce::String::fromUTF8 ("  \xc2\xb7  ") + where, area, juce::Justification::centredLeft, true);
            }
        }

        void listBoxItemClicked (int row, const juce::MouseEvent& e) override
        {
            if (! juce::isPositiveAndBelow (row, (int) owner.entries.size()))
                return;

            const auto rowWidth = e.eventComponent != nullptr ? e.eventComponent->getWidth() : getVisibleRowWidth();

            if (e.mods.isPopupMenu())
                showMenu (row);
            else if (owner.entries[(size_t) row].kind != Entry::Kind::folder && isOverStar (e.x, rowWidth))
                owner.toggleFavourite (row);
            else if (! e.mods.isShiftDown() && ! e.mods.isCommandDown())
                owner.clickEntry (row);
        }

        void listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) override
        {
            // The first click of this double-click opened a folder: this row is in the new one.
            if (juce::Time::getMillisecondCounter() - owner.folderOpenedAt < doubleClickAfterFolderMs)
                return;

            // Two clicks on the star are two clicks on the star.
            if (juce::isPositiveAndBelow (row, (int) owner.entries.size()) && owner.entries[(size_t) row].kind != Entry::Kind::folder
                && isOverStar (e.x, e.eventComponent != nullptr ? e.eventComponent->getWidth() : getVisibleRowWidth()))
                return;

            owner.openEntry (row);
        }

        void returnKeyPressed (int row) override
        {
            owner.openEntry (row);
        }

        void backgroundClicked (const juce::MouseEvent&) override
        {
            deselectAllRows();
        }

        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
        {
            juce::StringArray paths;

            for (int i = 0; i < rows.size(); ++i)
            {
                const auto row = rows[i];

                if (juce::isPositiveAndBelow (row, (int) owner.entries.size())
                    && owner.entries[(size_t) row].kind != Entry::Kind::folder)
                    paths.add (owner.entries[(size_t) row].file.getFullPathName());
            }

            return paths.isEmpty() ? juce::var() : file_drag::describe (paths);
        }

        juce::String getTooltipForRow (int row) override
        {
            if (! juce::isPositiveAndBelow (row, (int) owner.entries.size()))
                return {};

            return owner.entries[(size_t) row].kind == Entry::Kind::folder
                       ? "Click: open"
                       : "Click: listen. Double-click or drag onto a track: add to the song. The star: keep it in Favourites";
        }

        void showMenu (int row)
        {
            owner.createMenu (row).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        }
    };

    juce::PopupMenu SoundBrowser::createMenu (int row)
    {
        juce::PopupMenu menu;

        if (! juce::isPositiveAndBelow (row, (int) entries.size()))
            return menu;

        const auto entry = entries[(size_t) row];
        juce::Component::SafePointer<SoundBrowser> safe (this);

        if (entry.kind == Entry::Kind::folder)
        {
            menu.addItem ("Open", [safe, entry] { if (safe != nullptr) safe->setFolder (entry.file); });
            menu.addItem ("Add to places", [safe, entry] { if (safe != nullptr) safe->addPlace (entry.file); });
        }
        else
        {
            menu.addItem ("Listen", [safe, entry] { if (safe != nullptr) safe->listen (entry.file); });

            menu.addItem ("Add to the song (at the playhead)", [safe, entry]
            {
                if (safe != nullptr && safe->onAddToSong != nullptr)
                    safe->onAddToSong (entry.file);
            });

            const auto favourite = isFavourite (entry.file);
            menu.addItem (favourite ? "Remove from favourites" : "Add to favourites", [safe, entry, favourite]
            {
                if (safe != nullptr)
                    safe->setFavourite (entry.file, ! favourite);
            });

            if (entry.kind == Entry::Kind::sound && onSoundMenu != nullptr)
                onSoundMenu (menu, entry.file);
        }

        menu.addItem ("Show in Explorer", [entry] { entry.file.revealToUser(); });
        return menu;
    }

    //==============================================================================
    class SoundBrowser::PlacesList final : public juce::ListBox,
                                           private juce::ListBoxModel
    {
    public:
        explicit PlacesList (SoundBrowser& b)
            : owner (b)
        {
            setModel (this);
            setRowHeight (listRowHeight);
            setColour (juce::ListBox::backgroundColourId, theme::colour::surface);
            setColour (juce::ListBox::outlineColourId, theme::colour::border);
            setOutlineThickness (1);
        }

    private:
        SoundBrowser& owner;

        int getNumRows() override
        {
            return (int) owner.places.size();
        }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
        {
            if (! juce::isPositiveAndBelow (row, (int) owner.places.size()))
                return;

            const auto& place = owner.places[(size_t) row];
            const auto here = ! owner.showingFavourites && ! place.favourites
                                  && (owner.folder == place.folder || owner.folder.isAChildOf (place.folder));

            // The nearest place that holds the current folder is marked.
            auto best = -1;

            for (size_t i = 0; i < owner.places.size(); ++i)
            {
                const auto& p = owner.places[i];

                if ((owner.folder == p.folder || owner.folder.isAChildOf (p.folder))
                    && (best < 0 || p.folder.isAChildOf (owner.places[(size_t) best].folder)))
                    best = (int) i;
            }

            paintStripe (g, 0, width, height, place.favourites ? owner.showingFavourites : here && best == row);
            auto area = juce::Rectangle<int> (0, 0, width, height).withTrimmedLeft (8).withTrimmedRight (6);

            if (place.favourites)
                drawStar (g, area.removeFromLeft (18).toFloat(), true);
            else
                drawIcon (g, area.removeFromLeft (18).toFloat(), Entry::Kind::folder, false);

            area.removeFromLeft (6);
            g.setColour (place.added ? theme::colour::textPrimary : theme::colour::textPrimary.withAlpha (0.85f));
            g.setFont (juce::FontOptions (theme::size::fontSize));
            g.drawText (place.name, area, juce::Justification::centredLeft, true);
        }

        void listBoxItemClicked (int row, const juce::MouseEvent& e) override
        {
            if (! juce::isPositiveAndBelow (row, (int) owner.places.size()))
                return;

            const auto place = owner.places[(size_t) row];

            if (place.favourites)
            {
                owner.showFavourites();
                return;
            }

            if (! e.mods.isPopupMenu())
            {
                owner.setFolder (place.folder);
                return;
            }

            juce::Component::SafePointer<SoundBrowser> safe (&owner);
            juce::PopupMenu menu;
            menu.addItem ("Open", [safe, place] { if (safe != nullptr) safe->setFolder (place.folder); });
            menu.addItem ("Remove from places", place.added, false, [safe, place] { if (safe != nullptr) safe->removePlace (place.folder); });
            menu.addItem ("Show in Explorer", [place] { place.folder.revealToUser(); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        }

        juce::String getTooltipForRow (int row) override
        {
            if (! juce::isPositiveAndBelow (row, (int) owner.places.size()))
                return {};

            const auto& place = owner.places[(size_t) row];
            return place.favourites ? juce::String ("The sounds you marked with a star, wherever they are")
                                    : place.folder.getFullPathName();
        }
    };

    //==============================================================================
    SoundBrowser::SoundBrowser (core::Session& s, audio::FilePreview& p)
        : session (s), preview (p), favourites (s),
          fileList (std::make_unique<FileList> (*this)),
          placesList (std::make_unique<PlacesList> (*this))
    {
        setOpaque (true);

        placesLabel.setText ("Places", juce::dontSendNotification);
        placesLabel.setFont (juce::FontOptions (theme::size::smallFontSize, juce::Font::bold));
        placesLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (placesLabel);

        pathLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        pathLabel.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (pathLabel);

        upButton.setTooltip ("The folder this one is in (Backspace)");
        upButton.onClick = [this] { goUp(); };
        addAndMakeVisible (upButton);

        addPlaceButton.setTooltip ("Add a folder with your sounds to the places");
        addPlaceButton.onClick = [this] { chooseFolderToAdd(); };
        addAndMakeVisible (addPlaceButton);

        stopButton.setTooltip ("Stop listening (Esc)");
        stopButton.onClick = [this] { preview.stop(); };
        addAndMakeVisible (stopButton);

        inTempo = session.getEngine().getPropertyStorage().getPropertiesFile().getBoolValue (inTempoKey, true);
        tempoButton.setTooltip ("Loops play at the song's tempo, as they will sound in the song (their pitch stays)");
        tempoButton.setClickingTogglesState (true);
        tempoButton.setToggleState (inTempo, juce::dontSendNotification);
        tempoButton.onClick = [this] { setPlaysInTempo (tempoButton.getToggleState()); };
        addAndMakeVisible (tempoButton);

        search.setTextToShowWhenEmpty ("Search here and in subfolders", theme::colour::textSecondary);
        search.onTextChange = [this] { refresh(); };
        search.onEscapeKey = [this] { setFilter ({}); fileList->grabKeyboardFocus(); };
        search.onReturnKey = [this] { fileList->grabKeyboardFocus(); };
        addAndMakeVisible (search);

        for (auto* c : std::initializer_list<juce::Component*> { &upButton, &addPlaceButton, &stopButton, &tempoButton })
            c->setWantsKeyboardFocus (false);

        addAndMakeVisible (*placesList);
        addAndMakeVisible (*fileList);

        preview.onStateChanged = [this] { previewChanged(); };

        rebuildPlaces();

        // Where it was last time, else the Music folder.
        const juce::File last (session.getEngine().getPropertyStorage().getPropertiesFile().getValue (folderKey));
        const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        setFolder (last.isDirectory() ? last : music.isDirectory() ? music : juce::File::getSpecialLocation (juce::File::userHomeDirectory));
        previewChanged();
    }

    SoundBrowser::~SoundBrowser()
    {
        stopTimer();
        preview.onStateChanged = nullptr;
    }

    juce::ListBox& SoundBrowser::getList() noexcept
    {
        return *fileList;
    }

    void SoundBrowser::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);

        g.setColour (theme::colour::textSecondary);
        g.setFont (juce::FontOptions (theme::size::smallFontSize));
        g.drawText (juce::String::fromUTF8 ("Click: listen  \xc2\xb7  Up / Down: listen to the next  \xc2\xb7  "
                                            "Double-click, Enter or drag onto a track: add to the song  \xc2\xb7  "
                                            "Star: favourite  \xc2\xb7  Backspace: folder up"),
                    getLocalBounds().removeFromBottom (hintHeight).withTrimmedLeft (placesWidth + theme::size::padding * 2),
                    juce::Justification::centredLeft, true);
    }

    void SoundBrowser::resized()
    {
        auto area = getLocalBounds().reduced (theme::size::padding, theme::size::padding / 2);
        area.removeFromBottom (hintHeight - theme::size::padding / 2);

        auto left = area.removeFromLeft (placesWidth);
        area.removeFromLeft (theme::size::padding);

        auto top = left.removeFromTop (theme::size::buttonHeight);
        addPlaceButton.setBounds (top.removeFromRight (70));
        placesLabel.setBounds (top);
        left.removeFromTop (4);
        placesList->setBounds (left);

        auto bar = area.removeFromTop (theme::size::buttonHeight);
        upButton.setBounds (bar.removeFromLeft (44));
        bar.removeFromLeft (6);
        stopButton.setBounds (bar.removeFromRight (54));
        bar.removeFromRight (6);
        tempoButton.setBounds (bar.removeFromRight (76));
        bar.removeFromRight (6);
        search.setBounds (bar.removeFromRight (juce::jmin (240, bar.getWidth() / 2)));
        bar.removeFromRight (6);
        pathLabel.setBounds (bar);
        area.removeFromTop (4);
        fileList->setBounds (area);
    }

    bool SoundBrowser::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::backspaceKey)
        {
            goUp();
            return true;
        }

        if (key == juce::KeyPress::escapeKey && preview.isPlaying())
        {
            preview.stop();
            return true;
        }

        return false;
    }

    //==============================================================================
    void SoundBrowser::setFolder (const juce::File& newFolder)
    {
        if (! newFolder.isDirectory())
            return;

        folder = newFolder;
        showingFavourites = false;
        search.setText ({}, juce::dontSendNotification);

        auto& settings = session.getEngine().getPropertyStorage().getPropertiesFile();
        settings.setValue (folderKey, folder.getFullPathName());
        settings.saveIfNeeded();

        refresh();
        fileList->deselectAllRows();
        fileList->scrollToEnsureRowIsOnscreen (0);
        placesList->repaint();
    }

    void SoundBrowser::showFavourites()
    {
        showingFavourites = true;
        search.setText ({}, juce::dontSendNotification);
        refresh();
        fileList->deselectAllRows();
        fileList->scrollToEnsureRowIsOnscreen (0);
        placesList->repaint();
    }

    bool SoundBrowser::isFavourite (const juce::File& file) const
    {
        return favourites.contains (file);
    }

    void SoundBrowser::setFavourite (const juce::File& file, bool shouldBe)
    {
        if (favourites.contains (file) == shouldBe)
            return;

        favourites.set (file, shouldBe);

        // Among the favourites, one that no longer is leaves the list.
        if (showingFavourites)
            refresh();
        else
            fileList->repaint();
    }

    void SoundBrowser::toggleFavourite (int index)
    {
        if (juce::isPositiveAndBelow (index, (int) entries.size()) && entries[(size_t) index].kind != Entry::Kind::folder)
        {
            const auto file = entries[(size_t) index].file;     // a copy: the list may change
            setFavourite (file, ! isFavourite (file));
        }
    }

    bool SoundBrowser::isOverStar (int x, int rowWidth) noexcept
    {
        return x >= rowWidth - rowRightMargin - starWidth && x < rowWidth - rowRightMargin;
    }

    juce::String SoundBrowser::describeFolderOf (const juce::File& file) const
    {
        const auto parent = file.getParentDirectory();

        if (showingFavourites)
            return parent.getFullPathName();

        return parent != folder ? parent.getRelativePathFrom (folder).replaceCharacter ('\\', '/') : juce::String();
    }

    void SoundBrowser::goUp()
    {
        // From the favourites: back to the folder it was in.
        if (showingFavourites)
        {
            setFolder (folder);
            return;
        }

        const auto parent = folder.getParentDirectory();

        if (parent == folder || ! parent.isDirectory())
            return;

        const auto child = folder;
        setFolder (parent);

        // Where you came from stays in view and selected.
        if (const auto row = indexOf (child); row >= 0)
        {
            fileList->selectRow (row);
            fileList->scrollToEnsureRowIsOnscreen (row);
        }
    }

    void SoundBrowser::setFilter (const juce::String& text)
    {
        search.setText (text, juce::dontSendNotification);
        refresh();
    }

    juce::String SoundBrowser::getFilter() const
    {
        return search.getText();
    }

    int SoundBrowser::indexOf (const juce::File& file) const
    {
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].file == file)
                return (int) i;

        return -1;
    }

    bool SoundBrowser::isSound (const juce::File& file) const
    {
        return preview.canPlay (file);
    }

    void SoundBrowser::refresh()
    {
        if (showingFavourites)
        {
            const auto text = search.getText().trim();
            entries.clear();

            for (const auto& file : favourites.getFiles())
                if (text.isEmpty() || file.getFileName().containsIgnoreCase (text))
                    entries.push_back ({ file, core::midi_files::isMidiFile (file) ? Entry::Kind::midi : Entry::Kind::sound });

            pathLabel.setText ("Favourites", juce::dontSendNotification);
            pathLabel.setTooltip ("The sounds you marked with a star, wherever they are");
            upButton.setEnabled (true);
            search.setTextToShowWhenEmpty ("Search the favourites", theme::colour::textSecondary);
        }
        else
        {
            entries = listFolder (folder, search.getText(), [this] (const juce::File& f) { return isSound (f); });
            pathLabel.setText (folder.getFullPathName(), juce::dontSendNotification);
            pathLabel.setTooltip (folder.getFullPathName());
            upButton.setEnabled (folder.getParentDirectory() != folder);
            search.setTextToShowWhenEmpty ("Search here and in subfolders", theme::colour::textSecondary);
        }

        search.repaint();
        fileList->updateContent();
        fileList->repaint();

        // A length that is a whole number of bars depends on the song's tempo: looked at anew
        // when that has changed.
        if (const auto bpm = songBpm(); bpm != loopBpmsFor || loopBpms.size() > maxKnownTempi)
        {
            loopBpms.clear();
            loopBpmsFor = bpm;
        }

        startTimer (tempoTimerMs);
    }

    //==============================================================================
    double SoundBrowser::songBpm() const
    {
        const auto bpm = getSongBpm != nullptr ? getSongBpm() : 0.0;
        return bpm > 0.0 ? bpm : 120.0;
    }

    std::optional<double> SoundBrowser::findLoopBpm (const juce::File& file)
    {
        const auto path = file.getFullPathName();

        if (const auto known = loopBpms.find (path); known != loopBpms.end())
            return known->second;

        const auto bpm = core::audio_tempo::findLoopBpm (session.getEngine(), file, loopBpmsFor > 0.0 ? loopBpmsFor : songBpm());
        loopBpms[path] = bpm;
        return bpm;
    }

    std::optional<double> SoundBrowser::getLoopBpm (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) entries.size()) || entries[(size_t) index].kind != Entry::Kind::sound)
            return std::nullopt;

        return findLoopBpm (entries[(size_t) index].file);
    }

    juce::String SoundBrowser::describeTempo (int index) const
    {
        if (! juce::isPositiveAndBelow (index, (int) entries.size()) || entries[(size_t) index].kind != Entry::Kind::sound)
            return {};

        const auto& file = entries[(size_t) index].file;
        const auto format = [] (double bpm) { return core::audio_tempo::formatBpm (bpm); };

        // Playing at the song's tempo: its own, and that one.
        if (preview.isPlaying() && preview.getFile() == file)
            if (const auto tempo = preview.getTempo(); tempo.has_value())
                return format (tempo->own) == format (tempo->heard)
                           ? format (tempo->own) + " BPM"
                           : format (tempo->own) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + format (tempo->heard) + " BPM";

        const auto known = loopBpms.find (file.getFullPathName());
        return known != loopBpms.end() && known->second.has_value() ? format (*known->second) + " BPM" : juce::String();
    }

    void SoundBrowser::timerCallback()
    {
        // The tempi the rows say are read from the files a few at a time (those in view first):
        // a folder of thousands of loops mustn't hold the window.
        const auto until = juce::Time::getMillisecondCounterHiRes() + tempoSliceMs;
        const auto count = (int) entries.size();
        const auto first = juce::jmax (0, fileList->getRowContainingPosition (fileList->getWidth() / 2, 2));
        auto found = false;

        for (int n = 0; n < count; ++n)
        {
            const auto& entry = entries[(size_t) ((first + n) % count)];

            if (entry.kind != Entry::Kind::sound || loopBpms.contains (entry.file.getFullPathName()))
                continue;

            if (found && juce::Time::getMillisecondCounterHiRes() > until)
            {
                fileList->repaint();
                return;     // the rest next time
            }

            findLoopBpm (entry.file);
            found = true;
        }

        stopTimer();

        if (found)
            fileList->repaint();
    }

    void SoundBrowser::setPlaysInTempo (bool shouldPlayInTempo)
    {
        tempoButton.setToggleState (shouldPlayInTempo, juce::dontSendNotification);

        if (inTempo == shouldPlayInTempo)
            return;

        inTempo = shouldPlayInTempo;

        auto& settings = session.getEngine().getPropertyStorage().getPropertiesFile();
        settings.setValue (inTempoKey, inTempo);
        settings.saveIfNeeded();

        // A loop that is playing: again, the new way.
        if (const auto playing = preview.getFile(); preview.isPlaying() && findLoopBpm (playing).has_value())
            listen (playing);
    }

    void SoundBrowser::listen (const juce::File& file)
    {
        preview.play (file, inTempo ? std::optional<double> (songBpm()) : std::nullopt);
    }

    //==============================================================================
    void SoundBrowser::clickEntry (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) entries.size()))
            return;

        const auto entry = entries[(size_t) index];     // a copy: opening a folder changes the list

        if (entry.kind == Entry::Kind::folder)
        {
            folderOpenedAt = juce::Time::getMillisecondCounter();
            setFolder (entry.file);
            return;
        }

        if (preview.isPlaying() && preview.getFile() == entry.file)
            preview.stop();
        else
            listen (entry.file);
    }

    void SoundBrowser::previewIfSound (int index)
    {
        if (juce::isPositiveAndBelow (index, (int) entries.size()) && entries[(size_t) index].kind != Entry::Kind::folder)
            listen (entries[(size_t) index].file);
    }

    void SoundBrowser::openEntry (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) entries.size()))
            return;

        const auto entry = entries[(size_t) index];

        if (entry.kind == Entry::Kind::folder)
        {
            setFolder (entry.file);
            return;
        }

        preview.stop();

        if (onAddToSong != nullptr)
            onAddToSong (entry.file);
    }

    void SoundBrowser::previewChanged()
    {
        stopButton.setEnabled (preview.isPlaying());
        fileList->repaint();
    }

    //==============================================================================
    void SoundBrowser::rebuildPlaces()
    {
        places.clear();
        places.push_back ({ "Favourites", {}, false, true });

        const auto add = [this] (const juce::String& name, const juce::File& f, bool added)
        {
            if (f.isDirectory() && std::none_of (places.begin(), places.end(), [&f] (const Place& p) { return p.folder == f; }))
                places.push_back ({ name, f, added });
        };

        add ("Desktop", juce::File::getSpecialLocation (juce::File::userDesktopDirectory), false);
        add ("Music", juce::File::getSpecialLocation (juce::File::userMusicDirectory), false);
        add ("Documents", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), false);
        add ("Downloads", juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"), false);

        const auto saved = juce::StringArray::fromLines (session.getEngine().getPropertyStorage().getPropertiesFile().getValue (placesKey));

        for (const auto& path : saved)
            if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
                add (juce::File (path).getFileName(), juce::File (path), true);

        juce::Array<juce::File> roots;
        juce::File::findFileSystemRoots (roots);

        for (const auto& root : roots)
            add (root.getFullPathName().trimCharactersAtEnd ("\\/"), root, false);

        placesList->updateContent();
        placesList->repaint();
    }

    void SoundBrowser::savePlaces()
    {
        juce::StringArray paths;

        for (const auto& place : places)
            if (place.added)
                paths.add (place.folder.getFullPathName());

        auto& settings = session.getEngine().getPropertyStorage().getPropertiesFile();
        settings.setValue (placesKey, paths.joinIntoString ("\n"));
        settings.saveIfNeeded();
    }

    void SoundBrowser::addPlace (const juce::File& f)
    {
        if (! f.isDirectory())
            return;

        // Kept even if it is one of the usual places' subfolders; not twice.
        if (std::none_of (places.begin(), places.end(), [&f] (const Place& p) { return p.folder == f; }))
        {
            places.push_back ({ f.getFileName(), f, true, false });
            savePlaces();
            rebuildPlaces();
        }

        setFolder (f);
    }

    void SoundBrowser::removePlace (const juce::File& f)
    {
        const auto removed = std::remove_if (places.begin(), places.end(), [&f] (const Place& p) { return p.added && p.folder == f; });

        if (removed == places.end())
            return;

        places.erase (removed, places.end());
        savePlaces();
        rebuildPlaces();
    }

    void SoundBrowser::chooseFolderToAdd()
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a folder with sounds", folder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<SoundBrowser> (this)] (const juce::FileChooser& fc)
        {
            if (safe != nullptr && fc.getResult().isDirectory())
                safe->addPlace (fc.getResult());
        });
    }
}
