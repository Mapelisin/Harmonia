#include "ui/arrangement/Arrangement.h"
#include "ui/TrackColours.h"
#include "audio/AudioInputs.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/MidiFiles.h"
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
        headers.onMessage = [this] (const juce::String& text, bool isProblem) { tell (text, isProblem); };
        clipArea.getGestures().onClipMenuRequested = [this] (te::Clip& clip, juce::Point<float> position)
        {
            showClipMenu (clip, position);
        };

        addMarkerButton.setTooltip ("Add a marker (a named place: Verse, Chorus...) where the playhead is. "
                                    "Double-click the strip above the bar numbers to add one there.");
        addMarkerButton.onClick = [this] { addMarkerAtPlayhead(); };
        addAndMakeVisible (addMarkerButton);
        ruler.onRenameMarker = [this] (const core::markers::Marker& marker) { askToRenameMarker (marker); };

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

        g.setColour (theme::colour::textSecondary);
        g.setFont (juce::FontOptions (theme::size::smallFontSize));
        g.drawText ("Markers", juce::Rectangle<int> (8, 0, 60, (int) ArrangementRuler::markerLaneHeight),
                    juce::Justification::centredLeft, false);
    }

    void Arrangement::resized()
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop (rulerHeight);
        auto corner = top.removeFromLeft (headerWidth);
        auto markerRow = corner.removeFromTop ((int) ArrangementRuler::markerLaneHeight);
        addMarkerButton.setBounds (markerRow.removeFromRight (72).reduced (4, 1));
        corner = corner.reduced (4, 1);
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
                case 'C': if (! mods.isShiftDown()) { copySelected(); return true; } return false;
                case 'X': if (! mods.isShiftDown()) { cutSelected(); return true; } return false;
                case 'V': if (! mods.isShiftDown()) { pasteAtPlayhead(); return true; } return false;
                case 'E': if (! mods.isShiftDown()) { splitAtPlayhead(); return true; } return false;
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
    double Arrangement::getPlayheadBeat() const
    {
        return model.getEdit().tempoSequence.toBeats (te::TimePosition::fromSeconds (transport.getPositionSeconds())).inBeats();
    }

    void Arrangement::tell (const juce::String& text, bool isProblem)
    {
        if (onMessage != nullptr)
            onMessage (text, isProblem);
    }

    void Arrangement::copySelected()
    {
        const auto count = model.copySelected();

        if (count == 0)
            tell ("Nothing to copy: select clips first", true);
        else
            tell ("Copied " + juce::String (count) + (count == 1 ? " clip" : " clips")
                      + " (Ctrl+V pastes at the playhead, on the current track)", false);
    }

    void Arrangement::cutSelected()
    {
        const auto count = model.cutSelected();

        if (count == 0)
            tell ("Nothing to cut: select clips first", true);
        else
            tell ("Cut " + juce::String (count) + (count == 1 ? " clip" : " clips") + " (Ctrl+V puts them back somewhere)", false);
    }

    void Arrangement::pasteAtPlayhead()
    {
        if (! ArrangementModel::canPaste())
        {
            tell ("Nothing to paste: copy clips first (Ctrl+C)", true);
            return;
        }

        const auto beat = std::max (0.0, snap::snapFloor (getPlayheadBeat(), model.getSnap(), model.getSnapContext()));
        const auto count = model.paste (beat);
        const auto copied = ArrangementModel::getNumCopiedClips();

        if (count == 0)
            tell ("Nothing pasted: MIDI clips go on instrument tracks, audio clips on audio tracks", true);
        else if (count < copied)
            tell ("Pasted " + juce::String (count) + " of " + juce::String (copied)
                      + " clips: the others found no track of their kind below", true);
        else
            tell ("Pasted " + juce::String (count) + (count == 1 ? " clip" : " clips"), false);
    }

    void Arrangement::splitAtPlayhead()
    {
        if (model.splitAt (getPlayheadBeat()) == 0)
            tell ("Nothing to split: put the playhead over a clip of the current track (click the ruler), "
                  "or select the clips to split", true);
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

        if (onViewChanged != nullptr)
            onViewChanged();
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

        if (onViewChanged != nullptr)
            onViewChanged();
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

        std::optional<ClipArea::RecordingMarker> recording;

        if (transport.isRecording() && beat.has_value())
        {
            ClipArea::RecordingMarker marker;
            marker.startBeat = model.getEdit().tempoSequence.toBeats (te::TimePosition::fromSeconds (transport.getRecordStartSeconds())).inBeats();
            marker.endBeat = *beat;
            const auto tracks = model.getTracks();

            for (int i = 0; i < tracks.size(); ++i)
                if (audio::AudioInputs::isArmed (*tracks[i]))
                    marker.trackIndices.push_back (i);

            if (! marker.trackIndices.empty() && marker.endBeat > marker.startBeat)
                recording = marker;
        }

        clipArea.setRecording (recording);
    }

    //==============================================================================
    bool Arrangement::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (const auto& path : files)
            if (model.getProject().canImport (juce::File (path)) || core::midi_files::isMidiFile (juce::File (path)))
                return true;

        return false;
    }

    bool Arrangement::areAllMidi (const juce::StringArray& files)
    {
        for (const auto& path : files)
            if (! core::midi_files::isMidiFile (juce::File (path)))
                return false;

        return ! files.isEmpty();
    }

    ClipArea::DropMarker Arrangement::getDropTarget (juce::Point<int> position, bool midi) const
    {
        const auto local = clipArea.getLocalPoint (this, position).toFloat();
        const auto numTracks = model.getNumTracks();
        auto track = view.yToTrack (local.y);

        if (track < 0 || track >= numTracks || local.y < 0.0f)
            track = -1;

        ClipArea::DropMarker marker;
        marker.trackIndex = track;
        marker.beat = std::max (0.0, snap::snapFloor (view.xToBeat (std::max (0.0f, local.x)), model.getSnap(), model.getSnapContext()));
        marker.newTrack = track < 0 || core::tracks::isAudio (*model.getTrack (track)) != ! midi;
        marker.midi = midi;
        return marker;
    }

    void Arrangement::fileDragEnter (const juce::StringArray& files, int x, int y)
    {
        clipArea.setDropMarker (getDropTarget ({ x, y }, areAllMidi (files)));
    }

    void Arrangement::fileDragMove (const juce::StringArray& files, int x, int y)
    {
        clipArea.setDropMarker (getDropTarget ({ x, y }, areAllMidi (files)));
    }

    void Arrangement::fileDragExit (const juce::StringArray&)
    {
        clipArea.setDropMarker (std::nullopt);
    }

    void Arrangement::filesDropped (const juce::StringArray& paths, int x, int y)
    {
        clipArea.setDropMarker (std::nullopt);
        const auto target = getDropTarget ({ x, y }, areAllMidi (paths));

        juce::Array<juce::File> files;

        for (const auto& path : paths)
            files.add (juce::File (path));

        const auto error = model.importFiles (files, target.trackIndex, target.beat);

        // Delete, Ctrl+B... act on the dropped clips straight away.
        if (isShowing())
            grabKeyboardFocus();

        if (onMessage == nullptr)
            return;

        if (error.isNotEmpty())
            onMessage (error, true);
        else
            onMessage (files.size() == 1 ? "Added " + files.getFirst().getFileName()
                                         : "Added " + juce::String (files.size()) + " files", false);
    }

    //==============================================================================
    void Arrangement::showClipMenu (te::Clip& clip, juce::Point<float> position)
    {
        const auto screen = clipArea.localPointToGlobal (position.toInt());
        createClipMenu (clip).showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screen.x, screen.y, 1, 1 }));
    }

    juce::PopupMenu Arrangement::createClipMenu (te::Clip& clip)
    {
        const te::Clip::Ptr target (&clip);
        const auto isValid = [target] { return target != nullptr && core::clips::isInEdit (*target); };
        auto* midi = dynamic_cast<te::MidiClip*> (&clip);

        juce::PopupMenu menu;
        menu.addSectionHeader (clip.getName());
        menu.addItem ("Rename...", [this, target, isValid] { if (isValid()) askToRename (*target); });

        // The colour goes to the whole selection if the clip is in it.
        juce::Array<te::Clip::Ptr> coloured;

        if (model.isSelected (clip))
            for (auto* selected : model.getSelectedClips())
                coloured.add (selected);
        else
            coloured.add (target);

        menu.addSubMenu (coloured.size() > 1 ? "Colour (" + juce::String (coloured.size()) + " clips)" : juce::String ("Colour"),
                         track_colours::createMenu (core::clips::getColour (clip), "Same as the track",
                                                    [this, coloured] (std::optional<juce::Colour> colour)
                                                    {
                                                        juce::Array<te::Clip*> clips;

                                                        for (const auto& c : coloured)
                                                            if (c != nullptr && core::clips::isInEdit (*c))
                                                                clips.add (c.get());

                                                        model.setClipColour (clips, colour);
                                                    }));

        if (midi != nullptr)
        {
            menu.addItem ("Make unique", core::clip_links::isLinked (*midi), false, [this, target, isValid]
            {
                if (auto* m = dynamic_cast<te::MidiClip*> (target.get()); m != nullptr && isValid())
                    model.makeUnique (*m);
            });
            const auto looping = core::clips::isLooping (clip);
            menu.addItem ("Loop (repeats when made longer)", true, looping, [this, target, isValid]
            {
                if (auto* m = dynamic_cast<te::MidiClip*> (target.get()); m != nullptr && isValid())
                    model.setLooping (*m, ! core::clips::isLooping (*m));
            });
            menu.addItem ("Make the repeats real notes", looping, false, [this, target, isValid]
            {
                if (auto* m = dynamic_cast<te::MidiClip*> (target.get()); m != nullptr && isValid())
                    model.bakeLoop (*m);
            });
            menu.addItem ("Export as MIDI file...", [this, target, isValid]
            {
                if (auto* m = dynamic_cast<te::MidiClip*> (target.get()); m != nullptr && isValid())
                    exportClipAsMidi (*m);
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

            // Fades and gain (also with the handles on the clip).
            const auto withAudioClip = [this, target, isValid] (const juce::String& gesture, std::function<void (te::AudioClipBase&)> change)
            {
                return [this, target, isValid, gesture, change]
                {
                    if (auto* a = dynamic_cast<te::AudioClipBase*> (target.get()); a != nullptr && isValid())
                    {
                        model.getProject().beginGesture (gesture);
                        change (*a);
                    }
                };
            };

            juce::PopupMenu shapes;
            const auto currentShape = core::clips::getFadeShape (*audio);

            for (auto shape : core::clips::getFadeShapes())
                shapes.addItem (core::clips::getFadeShapeName (shape), true, shape == currentShape,
                                withAudioClip ("Fade shape", [shape] (te::AudioClipBase& a) { core::clips::setFadeShape (a, shape); }));

            const auto hasFades = core::clips::getFadeInBeats (*audio) > 0.0 || core::clips::getFadeOutBeats (*audio) > 0.0;
            menu.addSubMenu ("Fade shape", shapes);
            menu.addItem ("Remove fades", hasFades, false, withAudioClip ("Remove fades", [] (te::AudioClipBase& a)
            {
                core::clips::setFadeInBeats (a, 0.0);
                core::clips::setFadeOutBeats (a, 0.0);
            }));

            const auto gain = core::clips::getGainDb (*audio);
            menu.addItem ("Reset gain to 0 dB (now " + juce::String (gain, 1) + " dB)", std::abs (gain) > 0.05f, false,
                          withAudioClip ("Reset clip gain", [] (te::AudioClipBase& a) { core::clips::setGainDb (a, 0.0f); }));
            menu.addSeparator();
            menu.addItem ("Duplicate (Ctrl+B)", [this] { model.duplicateSelected (false); });
        }

        const auto playhead = getPlayheadBeat();
        const auto playheadInside = core::clips::getStartBeat (clip) < playhead && playhead < core::clips::getEndBeat (clip);
        menu.addItem ("Split at the playhead (Ctrl+E)", playheadInside, false, [this, target, isValid]
        {
            if (isValid())
            {
                model.setSelection ({ target.get() });
                splitAtPlayhead();
            }
        });
        menu.addItem ("Copy (Ctrl+C)", [this] { copySelected(); });
        menu.addItem ("Cut (Ctrl+X)", [this] { cutSelected(); });
        menu.addItem ("Paste at the playhead (Ctrl+V)", ArrangementModel::canPaste(), false, [this] { pasteAtPlayhead(); });

        menu.addSeparator();
        menu.addItem ("Delete (Del)", [this] { model.deleteSelected(); });
        return menu;
    }

    void Arrangement::exportClipAsMidi (te::MidiClip& clip)
    {
        const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        const auto name = juce::File::createLegalFileName (clip.getName().isNotEmpty() ? clip.getName() : juce::String ("Clip"));
        midiChooser = std::make_unique<juce::FileChooser> ("Export clip as MIDI", folder.getChildFile (name + ".mid"), "*.mid");

        const te::Clip::Ptr target (&clip);
        juce::Component::SafePointer<Arrangement> safe (this);

        midiChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safe, target] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            auto* midi = dynamic_cast<te::MidiClip*> (target.get());

            if (safe == nullptr || file == juce::File() || midi == nullptr || ! core::clips::isInEdit (*midi))
                return;

            if (! core::midi_files::isMidiFile (file))
                file = file.withFileExtension ("mid");

            const auto error = core::midi_files::writeClip (*midi, file);
            safe->tell (error.isNotEmpty() ? error : "Exported " + file.getFileName(), error.isNotEmpty());
        });
    }

    void Arrangement::addMarkerAtPlayhead()
    {
        model.addMarker (std::max (0.0, core::snap::snapNearest (getPlayheadBeat(), core::snap::Mode::beat, model.getSnapContext())),
                         model.getNewMarkerName());
    }

    void Arrangement::askToRenameMarker (const core::markers::Marker& marker)
    {
        auto* window = new juce::AlertWindow ("Rename marker", {}, juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", marker.name);
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);
        juce::Component::SafePointer<Arrangement> safeThis (this);

        window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, marker, safeWindow] (int result)
        {
            if (result == 1 && safeThis != nullptr && safeWindow != nullptr)
                safeThis->model.renameMarker (marker, safeWindow->getTextEditorContents ("name"));
        }), true);
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
