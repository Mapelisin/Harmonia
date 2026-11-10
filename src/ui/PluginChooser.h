#pragma once

#include "audio/PluginCatalog.h"

namespace harmonia::ui
{
    /** The plugin chooser: a search box and, under it, the instruments or the effects to choose
        from: the favourites, the ones used lately, then all of them (Harmonia's own first).

        Typing narrows the list to the names and makers that have the letters (the best match on
        top, already selected); Enter or a click chooses; Up / Down move; the star makes a
        favourite; Escape closes. Opens focused on the search box, so a plugin is two letters and
        Enter away. */
    class PluginChooser final : public juce::Component,
                                private juce::ListBoxModel,
                                private juce::TextEditor::Listener,
                                private juce::KeyListener
    {
    public:
        using Kind = audio::plugin_catalog::Kind;
        using Entry = audio::plugin_catalog::Entry;
        using Row = audio::plugin_catalog::Row;

        PluginChooser (core::Session&, Kind, std::function<void (const Entry&)> onChosen);
        ~PluginChooser() override;

        /** Opens the chooser in a call-out next to `target`; `onChosen` gets what was chosen
            (the call-out closes itself). */
        static void show (core::Session&, Kind, juce::Component& target, std::function<void (const Entry&)> onChosen);

        void setSearch (const juce::String&);
        juce::String getSearch() const                          { return search.getText(); }

        const std::vector<Row>& getRows() const noexcept        { return rows; }

        /** The row Enter would choose (-1: none). */
        int getSelected() const                                 { return list.getSelectedRow(); }

        /** Up / Down: to the next entry, over the headings. */
        void moveSelection (int delta);

        /** What a click on a row or Enter does: chooses its entry (a heading: nothing). */
        void choose (int row);

        /** What a click on a row's star does. */
        void toggleFavourite (int row);

        /** What is said instead of a list when there is nothing to show. */
        juce::String getEmptyText() const;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;
        void parentHierarchyChanged() override;

        static constexpr int rowHeight = 24, starWidth = 26;

    private:
        core::Session& session;
        const Kind kind;
        std::function<void (const Entry&)> onChosen;

        juce::TextEditor search;
        juce::ListBox list;
        juce::Label hint, emptyLabel;
        std::vector<Row> rows;

        void refresh();
        void selectFirstEntry();
        void close();

        int getNumRows() override                               { return (int) rows.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        juce::String getTooltipForRow (int row) override;

        void textEditorTextChanged (juce::TextEditor&) override;
        void textEditorReturnKeyPressed (juce::TextEditor&) override;
        void textEditorEscapeKeyPressed (juce::TextEditor&) override;

        using juce::Component::keyPressed;
        bool keyPressed (const juce::KeyPress&, juce::Component*) override;

        JUCE_DECLARE_NON_COPYABLE (PluginChooser)
    };
}
