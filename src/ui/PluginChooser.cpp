#include "ui/PluginChooser.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace catalog = audio::plugin_catalog;

    PluginChooser::PluginChooser (core::Session& s, Kind k, std::function<void (const Entry&)> chosen)
        : session (s), kind (k), onChosen (std::move (chosen))
    {
        search.setTextToShowWhenEmpty (kind == Kind::instrument ? "Search instruments: a name or a maker" : "Search effects: a name or a maker",
                                       theme::colour::textSecondary);
        search.setFont (juce::FontOptions (theme::size::fontSize));
        search.setColour (juce::TextEditor::backgroundColourId, theme::colour::background);
        search.setColour (juce::TextEditor::textColourId, theme::colour::textPrimary);
        search.setColour (juce::TextEditor::outlineColourId, theme::colour::border);
        search.setColour (juce::TextEditor::focusedOutlineColourId, theme::colour::accent);
        search.setSelectAllWhenFocused (true);
        search.addListener (this);
        search.addKeyListener (this);       // Up / Down go through the list while typing
        addAndMakeVisible (search);

        list.setModel (this);
        list.setRowHeight (rowHeight);
        list.setColour (juce::ListBox::backgroundColourId, theme::colour::surface);
        list.setMouseMoveSelectsRows (true);
        list.setWantsKeyboardFocus (false);
        addAndMakeVisible (list);

        hint.setFont (juce::FontOptions (theme::size::smallFontSize));
        hint.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        hint.setText (juce::String::fromUTF8 ("Enter: choose   \xe2\x98\x85: favourite   Esc: close"), juce::dontSendNotification);
        addAndMakeVisible (hint);

        emptyLabel.setFont (juce::FontOptions (theme::size::smallFontSize));
        emptyLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        emptyLabel.setJustificationType (juce::Justification::centred);
        emptyLabel.setInterceptsMouseClicks (false, false);
        addChildComponent (emptyLabel);

        refresh();
        setSize (380, 440);
    }

    PluginChooser::~PluginChooser()
    {
        search.removeKeyListener (this);
        search.removeListener (this);
        list.setModel (nullptr);
    }

    void PluginChooser::show (core::Session& session, Kind kind, juce::Component& target, std::function<void (const Entry&)> onChosen)
    {
        auto chooser = std::make_unique<PluginChooser> (session, kind, std::move (onChosen));
        juce::CallOutBox::launchAsynchronously (std::move (chooser), target.getScreenBounds(), nullptr);
    }

    void PluginChooser::setSearch (const juce::String& text)
    {
        search.setText (text, juce::dontSendNotification);
        refresh();
    }

    void PluginChooser::refresh()
    {
        rows = catalog::list (session, kind, search.getText());
        list.updateContent();
        list.repaint();
        selectFirstEntry();
        emptyLabel.setText (getEmptyText(), juce::dontSendNotification);
        emptyLabel.setVisible (rows.empty());
    }

    void PluginChooser::selectFirstEntry()
    {
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            if (! rows[(size_t) i].isHeading())
            {
                list.selectRow (i);
                return;
            }
        }

        list.deselectAllRows();
    }

    void PluginChooser::moveSelection (int delta)
    {
        const auto step = delta < 0 ? -1 : 1;

        for (auto row = getSelected() + step; juce::isPositiveAndBelow (row, (int) rows.size()); row += step)
        {
            if (! rows[(size_t) row].isHeading())
            {
                list.selectRow (row);
                return;
            }
        }
    }

    void PluginChooser::choose (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].isHeading())
            return;

        // The call-out, and this with it, may go while the choice is carried out.
        const auto entry = rows[(size_t) row].entry;
        const auto chosen = onChosen;
        juce::Component::SafePointer<PluginChooser> safe (this);

        if (chosen != nullptr)
            chosen (entry);

        if (safe != nullptr)
            safe->close();
    }

    void PluginChooser::toggleFavourite (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].isHeading())
            return;

        const auto entry = rows[(size_t) row].entry;
        catalog::setFavourite (session, entry, ! rows[(size_t) row].favourite);

        // The list is put together again (the entry moves under Favourites): stay on the entry.
        rows = catalog::list (session, kind, search.getText());
        list.updateContent();
        list.repaint();

        for (int i = 0; i < (int) rows.size(); ++i)
            if (! rows[(size_t) i].isHeading() && rows[(size_t) i].entry == entry)
                list.selectRow (i);
    }

    juce::String PluginChooser::getEmptyText() const
    {
        if (! rows.empty())
            return {};

        return "Nothing is called \"" + search.getText().trim() + "\".\nPlugins of your own: File > Settings > Plugins scans for them.";
    }

    void PluginChooser::close()
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    //==============================================================================
    void PluginChooser::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);
    }

    void PluginChooser::resized()
    {
        auto area = getLocalBounds().reduced (8);
        search.setBounds (area.removeFromTop (28));
        area.removeFromTop (6);
        hint.setBounds (area.removeFromBottom (18));
        area.removeFromBottom (4);
        list.setBounds (area);
        emptyLabel.setBounds (area.reduced (12).removeFromTop (60));
    }

    void PluginChooser::visibilityChanged()
    {
        if (isShowing())
            search.grabKeyboardFocus();
    }

    void PluginChooser::parentHierarchyChanged()
    {
        if (isShowing())
            search.grabKeyboardFocus();
    }

    void PluginChooser::paintListBoxItem (int index, juce::Graphics& g, int width, int height, bool selected)
    {
        if (! juce::isPositiveAndBelow (index, (int) rows.size()))
            return;

        const auto& row = rows[(size_t) index];

        if (row.isHeading())
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (juce::FontOptions (theme::size::smallFontSize, juce::Font::bold));
            g.drawText (row.heading, 8, 0, width - 16, height, juce::Justification::bottomLeft, true);
            return;
        }

        if (selected)
        {
            g.setColour (theme::colour::accent.withAlpha (0.3f));
            g.fillRect (0, 0, width, height);
        }

        g.setFont (juce::FontOptions (theme::size::fontSize));
        g.setColour (row.favourite ? theme::colour::accent : theme::colour::textSecondary.withAlpha (0.6f));
        g.drawText (juce::String::fromUTF8 (row.favourite ? "\xe2\x98\x85" : "\xe2\x98\x86"), 0, 0, starWidth, height, juce::Justification::centred, false);

        const auto maker = row.entry.isBuiltIn() ? juce::String ("built-in") : row.entry.maker;
        const auto makerWidth = std::min (width / 2, juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (theme::size::smallFontSize), maker) + 12);

        g.setColour (theme::colour::textPrimary);
        g.drawText (row.entry.name, starWidth, 0, width - starWidth - makerWidth - 4, height, juce::Justification::centredLeft, true);
        g.setColour (theme::colour::textSecondary);
        g.setFont (juce::FontOptions (theme::size::smallFontSize));
        g.drawText (maker, width - makerWidth - 6, 0, makerWidth, height, juce::Justification::centredRight, true);
    }

    void PluginChooser::listBoxItemClicked (int row, const juce::MouseEvent& e)
    {
        if (e.x < starWidth)
            toggleFavourite (row);
        else
            choose (row);
    }

    juce::String PluginChooser::getTooltipForRow (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].isHeading())
            return {};

        return rows[(size_t) row].entry.getLabel() + ": click to choose; the star keeps it on top as a favourite";
    }

    //==============================================================================
    void PluginChooser::textEditorTextChanged (juce::TextEditor&)
    {
        refresh();
    }

    void PluginChooser::textEditorReturnKeyPressed (juce::TextEditor&)
    {
        choose (getSelected());
    }

    void PluginChooser::textEditorEscapeKeyPressed (juce::TextEditor&)
    {
        close();
    }

    bool PluginChooser::keyPressed (const juce::KeyPress& key, juce::Component*)
    {
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            moveSelection (key == juce::KeyPress::upKey ? -1 : 1);
            return true;
        }

        return false;
    }
}
