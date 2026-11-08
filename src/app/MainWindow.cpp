#include "app/MainWindow.h"
#include "ui/MainComponent.h"
#include "ui/RenderingPolicy.h"
#include "ui/Theme.h"

namespace harmonia::app
{
    MainWindow::MainWindow (core::Session& session, core::Project& project)
        : juce::DocumentWindow ("Harmonia", ui::theme::colour::background, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new ui::MainComponent (session, project), true);
        setResizable (true, false);
        setResizeLimits (800, 500, 16384, 16384);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        ui::applyPreferredRenderer (*this);
    }

    void MainWindow::closeButtonPressed()
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
}
