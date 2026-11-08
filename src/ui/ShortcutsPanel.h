#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace harmonia::ui
{
    /** Every keyboard shortcut and mouse trick, in one list over the main window (F1), with a
        search box. Esc, F1, the close button or a click outside the list hides it. */
    class ShortcutsPanel final : public juce::Component
    {
    public:
        struct Row
        {
            juce::String keys, what;
        };

        struct Section
        {
            juce::String title;
            std::vector<Row> rows;
        };

        /** The list shown (what docs/user tells too). */
        static const std::vector<Section>& getSections();

        ShortcutsPanel();
        ~ShortcutsPanel() override;

        /** Shows only the rows whose keys or words contain this text (any case). */
        void setFilter (const juce::String&);
        juce::String getFilter() const                  { return search.getText(); }

        /** How many rows show with the filter. */
        int getNumShownRows() const;

        /** Asks the owner to hide the panel. */
        std::function<void()> onClose;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void visibilityChanged() override;

    private:
        class List;

        juce::Label title;
        juce::TextEditor search;
        juce::TextButton closeButton { "Close" };
        juce::Viewport viewport;
        std::unique_ptr<List> list;

        juce::Rectangle<int> getPanelArea() const;
    };
}
