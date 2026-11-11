#pragma once

#include "audio/Transport.h"
#include "ui/automation/AutomationLane.h"

namespace harmonia::ui::automation
{
    /** The lower panel for automation (F8): which parameter of the current track follows a line
        (volume, pan, a send, an effect's or the instrument's knob) and that line, under the
        arrangement and lined up with its bars. Each track remembers the parameter shown.

        The master, the return tracks and the groups aren't rows of the arrangement: their lines
        are chosen here, with the button that names whose lines are shown. The panel stays on
        them until another track is clicked in the arrangement. */
    class AutomationPanel final : public juce::Component,
                                  private arrangement::ArrangementModel::Listener,
                                  private juce::ValueTree::Listener,
                                  private juce::AsyncUpdater
    {
    public:
        AutomationPanel (arrangement::ArrangementModel&, const arrangement::ArrangementView&, audio::Transport&);
        ~AutomationPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** The current track, its effects or the arrangement's scroll and zoom changed. */
        void refresh();

        /** Whose lines are shown: the master's, a return track's or a group's (see the class
            comment), or the arrangement's current track's again. */
        void showMaster();
        void showBus (te::AudioTrack& returnOrGroup);
        void followCurrentTrack();
        bool showsMaster() const noexcept                           { return master; }

        /** The track whose lines are shown; nullptr for the master, or when there is no track. */
        te::AudioTrack* getShownTrack() const noexcept              { return track.get(); }
        juce::Button& getTargetButton() noexcept                    { return targetButton; }

        /** Shows (and remembers for what is shown) this parameter's line. */
        void setParameter (te::AutomatableParameter*);
        te::AutomatableParameter* getParameter() const noexcept     { return lane.getParameter(); }

        /** Removes the shown line (one undo step). */
        void clearLine();

        /** The keys of the selected points, when the lane was the last thing clicked: Del,
            Ctrl+C, Ctrl+X, Ctrl+V (see getPasteBeat), Ctrl+A (all points), Esc (none). */
        bool keyPressed (const juce::KeyPress&) override;

        /** Where Ctrl+V puts copied points: the playhead, on the arrangement's grid. */
        double getPasteBeat() const;

        AutomationLane& getLane() noexcept                          { return lane; }
        juce::Button& getParameterButton() noexcept                 { return parameterButton; }

        /** Sizes of the left column and the strip on the right, so the lane lines up with the
            arrangement's clips above (the same as its track headers and scrollbar). */
        void setMargins (int left, int right);

    private:
        arrangement::ArrangementModel& model;
        const arrangement::ArrangementView& view;
        audio::Transport& transport;
        te::Edit& edit;

        AutomationLane lane { model, view };
        juce::Label infoLabel;
        juce::TextButton targetButton, parameterButton, clearButton { "Clear" };
        int leftMargin = 200, rightMargin = 10;

        juce::ReferenceCountedObjectPtr<te::AudioTrack> track;      // whose lines are shown; none for the master
        juce::ReferenceCountedObjectPtr<te::AudioTrack> bus;        // the return track or group chosen here
        bool master = false;
        te::EditItemID lastCurrentTrack;                            // the arrangement's, when last looked at
        std::map<te::EditItemID, te::AutomatableParameter::Ptr> chosen;     // the master's under no ID

        juce::VBlankAttachment vblank { this, [this] { updatePlayhead(); } };

        juce::Array<te::AutomatableParameter*> getParameters() const;
        juce::String getTargetName() const;
        juce::Colour getTargetColour() const;
        void showTargetMenu();
        void showParameterMenu();
        void updateControls();
        void updatePlayhead();

        void arrangementChanged() override;
        void handleAsyncUpdate() override;
        void treeChanged (const juce::ValueTree&);
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

        JUCE_DECLARE_NON_COPYABLE (AutomationPanel)
    };
}
