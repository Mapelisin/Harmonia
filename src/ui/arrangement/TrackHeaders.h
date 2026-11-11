#pragma once

#include "ui/arrangement/ArrangementModel.h"
#include "ui/arrangement/ArrangementView.h"

namespace harmonia::ui::arrangement
{
    /** "Instrument track" / "Drum track" / "Audio track". */
    juce::PopupMenu makeAddTrackMenu (ArrangementModel&);

    /** makeAddTrackMenu, shown under `target`. */
    void showAddTrackMenu (ArrangementModel&, juce::Component& target);

    /** Where a song track plays: the master, one of the group tracks, or a new group. With
        `forSelection` the choice is for every selected track when `track` is one of several
        (ArrangementModel::getTracksFor): the arrangement's menus ask for that, the mixer's
        doesn't. */
    juce::PopupMenu makeOutputMenu (ArrangementModel&, te::AudioTrack&, bool forSelection = false);

    /** The column left of the clips: one header per track with its name (double-click to
        rename), mute, solo and volume; audio tracks also have record arm, their input and its
        level while armed. Click a header to make the track current; right-click for its menu.
        Ctrl + click and Shift + click select several tracks: mute, solo and the menu of one of
        them are then for them all (ArrangementModel::getTracksFor).
        The first track of a folder has a fold button; a collapsed folder's row shows the folder:
        its name, how many tracks, and mute and solo for all of them. */
    class TrackHeaders final : public juce::Component
    {
    public:
        TrackHeaders (ArrangementModel&, ArrangementView&);
        ~TrackHeaders() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        /** Re-reads the tracks (rebuilding headers if tracks came or went) and their values. */
        void refresh();

        /** Scrolled with the wheel. */
        std::function<void()> onViewChanged;

        /** Something to tell the user (a problem if `isProblem`). */
        std::function<void (const juce::String&, bool isProblem)> onMessage;

        /** Dragging a header up or down moves its track: while dragged (y in this component),
            a line shows the gap it would go into. */
        void dragHeader (int trackIndex, float y);
        void endHeaderDrag (int trackIndex, float y);

        /** The gap (0 = above the first track) a dragged header would go into, if one is dragged. */
        std::optional<int> getDropGap() const noexcept     { return dropGap; }

        /** A press of the mouse on a track's header (the headers call this). The left button:
            ArrangementModel::clickTrack with Ctrl and Shift. The right button (the menu follows):
            one of several selected tracks keeps the selection, any other becomes the only one. */
        void headerPressed (te::AudioTrack&, const juce::ModifierKeys&);

        /** The track menu's commands (also for tests). */
        void renderToAudio (te::AudioTrack&);

        /** Freezes the track, or unfreezes a frozen one; says what happened. */
        void toggleFrozen (te::AudioTrack&);

        /** The track's menu: right-click on its header, or Track in the menu bar. For the row
            of a collapsed folder, the folder's menu. */
        juce::PopupMenu createTrackMenu (te::AudioTrack&);

        /** The track's Folder submenu: a new folder with it, into another, out of its own; its
            folder collapsed, renamed, removed. */
        juce::PopupMenu createFolderMenu (te::AudioTrack&);

        /** Opens the editor of the track's name in its header. */
        void startRenaming (te::AudioTrack&);

        void paintOverChildren (juce::Graphics&) override;

        /** Number of headers (for tests). */
        int getNumHeaders() const noexcept      { return headers.size(); }

        /** The record arm button of a track's header (for tests); nullptr if it has none. */
        juce::Button* getArmButton (int index) const;

        /** A header's buttons and name as shown (for tests): the fold button is there only for
            the first track of a folder. */
        juce::Button* getFoldButton (int index) const;
        juce::Button* getMuteButton (int index) const;
        juce::Button* getSoloButton (int index) const;
        juce::String getShownName (int index) const;

        /** The user wants to rename this folder (the owner asks for the name). */
        std::function<void (const core::folders::Folder&)> onRenameFolder;

    private:
        class Header;

        ArrangementModel& model;
        ArrangementView& view;
        juce::OwnedArray<Header> headers;
        std::optional<int> dropGap;

        int gapAt (float y) const;
    };
}
