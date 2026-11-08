#pragma once

#include "audio/Transport.h"
#include "ui/automation/AutomationLane.h"

namespace harmonia::ui::automation
{
    /** The lower panel for automation (F8): which parameter of the current track follows a line
        (volume, pan, a send, an effect's or the instrument's knob) and that line, under the
        arrangement and lined up with its bars. Each track remembers the parameter shown. */
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

        /** Shows (and remembers for the current track) this parameter's line. */
        void setParameter (te::AutomatableParameter*);
        te::AutomatableParameter* getParameter() const noexcept     { return lane.getParameter(); }

        /** Removes the shown line (one undo step). */
        void clearLine();

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
        juce::Label trackLabel, infoLabel;
        juce::TextButton parameterButton, clearButton { "Clear" };
        int leftMargin = 200, rightMargin = 10;

        juce::ReferenceCountedObjectPtr<te::AudioTrack> track;
        std::map<te::EditItemID, te::AutomatableParameter::Ptr> chosen;

        juce::VBlankAttachment vblank { this, [this] { updatePlayhead(); } };

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
