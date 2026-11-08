#include "ui/arrangement/Arrangement.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/Tracks.h"
#include "ui/ComputerKeyboard.h"
#include "ui/Theme.h"

namespace harmonia::ui::arrangement
{
    namespace snap = core::snap;

    namespace
    {
        // The scroll range goes this far past the end of the song.
        constexpr double extraBars = 16.0;
    }

    Arrangement::Arrangement (ArrangementModel& m, audio::Transport& t)
        : model (m), transport (t)
    {
        model.addListener (this);

        addTrackButton.setTooltip ("Add a track: an instrument track for MIDI clips, or an audio track for sound files");
        addTrackButton.onClick = [this] { showAddTrackMenu (model, addTrackButton); };

        const auto& modes = ArrangementModel::getSnapModes();

        for (size_t i = 0; i < modes.size(); ++i)
            snapBox.addItem ("Snap: " + snap::getName (modes[i]), (int) i + 1);

        snapBox.setTooltip ("What clips snap to when moved or resized (Alt: no snap). Line follows the zoom.");
        snapBox.onChange = [this, &modes]
        {
            if (const auto index = snapBox.getSelectedId() - 1; index >= 0)
                model.setSnap (modes[(size_t) index]);
        };

        clipArea.onViewChanged = [this] { viewChanged(); };
        headers.onViewChanged = [this] { viewChanged(); };
        clipArea.getGestures().onClipMenuRequested = [this] (te::Clip& clip, juce::Point<float> position)
        {
            showClipMenu (clip, position);
        };

        ruler.onPositionChosen = [this] (double beat)
        {
            if (onPositionChosen != nullptr)
                onPositionChosen (beat);
        };

        horizontalScroll.addListener (this);
        verticalScroll.addListener (this);
        horizontalScroll.setAutoHide (false);
        verticalScroll.setAutoHide (false);

        for (auto* c : std::initializer_list<juce::Component*> { &addTrackButton, &snapBox })
            c->setWantsKeyboardFocus (false);

        for (auto* c : std::initializer_list<juce::Component*> { &clipArea, &ruler, &headers, &addTrackButton, &snapBox,
                                                                 &horizontalScroll, &verticalScroll })
            addAndMakeVisible (c);

        setWantsKeyboardFocus (true);
        arrangementChanged();
    }

    Arrangement::~Arrangement()
    {
        model.removeListener (this);
    }

    void Arrangement::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::surface);
        g.setColour (theme::colour::border);
        g.fillRect (0, rulerHeight - 1, headerWidth, 1);
    }

    void Arrangement::resized()
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop (rulerHeight);
        auto corner = top.removeFromLeft (headerWidth).reduced (4, 1);
        addTrackButton.setBounds (corner.removeFromLeft (64));
        corner.removeFromLeft (4);
        snapBox.setBounds (corner);
        ruler.setBounds (top.withTrimmedRight (scrollBarSize));

        auto bottom = area.removeFromBottom (scrollBarSize);
        bottom.removeFromLeft (headerWidth);
        horizontalScroll.setBounds (bottom.withTrimmedRight (scrollBarSize));

        headers.setBounds (area.removeFromLeft (headerWidth));
        verticalScroll.setBounds (area.removeFromRight (scrollBarSize));
        clipArea.setBounds (area);

        view.width = (float) area.getWidth();
        view.height = (float) area.getHeight();
        viewChanged();
    }

    //==============================================================================
    bool Arrangement::keyPressed (const juce::KeyPress& key)
    {
        return handleShortcut (key);
    }

    bool Arrangement::handleShortcut (const juce::KeyPress& key)
    {
        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode();
        const auto letter = keys::getLetter (key);   // the same keys with any layout

        if (mods.isCommandDown() && ! mods.isAltDown())
        {
            switch (letter)
            {
                case 'B': model.duplicateSelected (! mods.isShiftDown()); return true;
                case 'A': if (! mods.isShiftDown()) { model.selectAll(); return true; } return false;
                case 'D': if (! mods.isShiftDown()) { model.deselectAll(); return true; } return false;
                default:  return false;
            }
        }

        if (mods.isAnyModifierKeyDown())
            return false;

        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        {
            model.deleteSelected();
            return true;
        }

        if (code == juce::KeyPress::escapeKey)
        {
            model.deselectAll();
            return true;
        }

        return false;
    }

    //==============================================================================
    void Arrangement::arrangementChanged()
    {
        const auto& modes = ArrangementModel::getSnapModes();
        const auto index = (int) (std::find (modes.begin(), modes.end(), model.getSnap()) - modes.begin());
        snapBox.setSelectedId (index + 1, juce::dontSendNotification);

        headers.refresh();
        updateContentSize();
        updateScrollBars();
        clipArea.repaint();
        ruler.repaint();
    }

    void Arrangement::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
    {
        if (bar == &horizontalScroll)
            view.leftBeat = newRangeStart;
        else
            view.scrollY = (float) newRangeStart;

        view.clamp();
        headers.resized();
        clipArea.repaint();
        ruler.repaint();
    }

    void Arrangement::viewChanged()
    {
        model.setPixelsPerBeat (view.pixelsPerBeat);
        updateContentSize();
        view.clamp();
        updateScrollBars();
        headers.resized();
        clipArea.repaint();
        ruler.repaint();
    }

    void Arrangement::updateContentSize()
    {
        view.numTracks = model.getNumTracks();
        const auto beatsPerBar = model.getBeatsPerBar();
        const auto songBeats = std::max (model.getSongEndBeat(), model.getLoopRegion().has_value() ? model.getLoopRegion()->end : 0.0);
        view.contentBeats = std::max ((double) view.width / view.pixelsPerBeat, songBeats + extraBars * beatsPerBar);
    }

    void Arrangement::updateScrollBars()
    {
        horizontalScroll.setRangeLimits (0.0, view.contentBeats, juce::dontSendNotification);
        horizontalScroll.setCurrentRange (view.leftBeat, (double) view.width / view.pixelsPerBeat, juce::dontSendNotification);
        horizontalScroll.setSingleStepSize (model.getBeatsPerBar());

        const auto contentHeight = (double) (view.getContentHeight() + view.trackHeight);
        verticalScroll.setRangeLimits (0.0, std::max (contentHeight, (double) view.height), juce::dontSendNotification);
        verticalScroll.setCurrentRange ((double) view.scrollY, (double) view.height, juce::dontSendNotification);
        verticalScroll.setSingleStepSize ((double) view.trackHeight);
    }

    void Arrangement::updatePlayhead()
    {
        std::optional<double> beat;

        if (transport.isPlaying() || transport.getPositionSeconds() > 0.0)
            beat = model.getEdit().tempoSequence.toBeats (te::TimePosition::fromSeconds (transport.getPositionSeconds())).inBeats();

        clipArea.setPlayheadBeat (beat);
        ruler.setPlayheadBeat (beat);
    }

    //==============================================================================
    bool Arrangement::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (const auto& path : files)
            if (model.getProject().canImport (juce::File (path)))
                return true;

        return false;
    }

    ClipArea::DropMarker Arrangement::getDropTarget (juce::Point<int> position) const
    {
        const auto local = clipArea.getLocalPoint (this, position).toFloat();
        const auto numTracks = model.getNumTracks();
        auto track = view.yToTrack (local.y);

        if (track < 0 || track >= numTracks || local.y < 0.0f)
            track = -1;

        ClipArea::DropMarker marker;
        marker.trackIndex = track;
        marker.beat = std::max (0.0, snap::snapFloor (view.xToBeat (std::max (0.0f, local.x)), model.getSnap(), model.getSnapContext()));
        marker.newTrack = track < 0 || ! core::tracks::isAudio (*model.getTrack (track));
        return marker;
    }

    void Arrangement::fileDragEnter (const juce::StringArray&, int x, int y)
    {
        clipArea.setDropMarker (getDropTarget ({ x, y }));
    }

    void Arrangement::fileDragMove (const juce::StringArray&, int x, int y)
    {
        clipArea.setDropMarker (getDropTarget ({ x, y }));
    }

    void Arrangement::fileDragExit (const juce::StringArray&)
    {
        clipArea.setDropMarker (std::nullopt);
    }

    void Arrangement::filesDropped (const juce::StringArray& paths, int x, int y)
    {
        clipArea.setDropMarker (std::nullopt);
        const auto target = getDropTarget ({ x, y });

        juce::Array<juce::File> files;

        for (const auto& path : paths)
            files.add (juce::File (path));

        const auto error = model.importAudio (files, target.trackIndex, target.beat);

        // Delete, Ctrl+B... act on the dropped clips straight away.
        if (isShowing())
            grabKeyboardFocus();

        if (onMessage == nullptr)
            return;

        if (error.isNotEmpty())
            onMessage (error, true);
        else
            onMessage (files.size() == 1 ? "Added " + files.getFirst().getFileName()
                                         : "Added " + juce::String (files.size()) + " audio files", false);
    }

    //==============================================================================
    void Arrangement::showClipMenu (te::Clip& clip, juce::Point<float> position)
    {
        const te::Clip::Ptr target (&clip);
        const auto isValid = [target] { return target != nullptr && core::clips::isInEdit (*target); };
        auto* midi = dynamic_cast<te::MidiClip*> (&clip);

        juce::PopupMenu menu;
        menu.addSectionHeader (clip.getName());
        menu.addItem ("Rename...", [this, target, isValid] { if (isValid()) askToRename (*target); });

        if (midi != nullptr)
        {
            menu.addItem ("Make unique", core::clip_links::isLinked (*midi), false, [this, target, isValid]
            {
                if (auto* m = dynamic_cast<te::MidiClip*> (target.get()); m != nullptr && isValid())
                    model.makeUnique (*m);
            });
            menu.addSeparator();
            menu.addItem ("Duplicate, linked (Ctrl+B)", [this] { model.duplicateSelected (true); });
            menu.addItem ("Duplicate (Ctrl+Shift+B)", [this] { model.duplicateSelected (false); });
        }
        else if (auto* audio = dynamic_cast<te::AudioClipBase*> (&clip))
        {
            const auto file = audio->getCurrentSourceFile();
            menu.addItem ("Show file in folder", file.exists(), false, [file] { file.revealToUser(); });
            menu.addSeparator();
            menu.addItem ("Duplicate (Ctrl+B)", [this] { model.duplicateSelected (false); });
        }

        menu.addSeparator();
        menu.addItem ("Delete (Del)", [this] { model.deleteSelected(); });

        const auto screen = clipArea.localPointToGlobal (position.toInt());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screen.x, screen.y, 1, 1 }));
    }

    void Arrangement::askToRename (te::Clip& clip)
    {
        auto* window = new juce::AlertWindow ("Rename clip", {}, juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", clip.getName());
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        const te::Clip::Ptr target (&clip);
        juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);
        juce::Component::SafePointer<Arrangement> safeThis (this);

        window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, target, safeWindow] (int result)
        {
            if (result == 1 && safeThis != nullptr && safeWindow != nullptr && core::clips::isInEdit (*target))
                safeThis->model.renameClip (*target, safeWindow->getTextEditorContents ("name"));
        }), true);
    }
}
