#pragma once

#include "core/Project.h"

namespace harmonia::app
{
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (core::Session&, core::Project&);

        void closeButtonPressed() override;

        /** Shows a problem in the status bar. */
        void showWarning (const juce::String&);

    private:
        JUCE_DECLARE_NON_COPYABLE (MainWindow)
    };
}
