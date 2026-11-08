#pragma once

#include "core/Project.h"

namespace harmonia::app
{
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (core::Session&, core::Project&);

        void closeButtonPressed() override;

    private:
        JUCE_DECLARE_NON_COPYABLE (MainWindow)
    };
}
