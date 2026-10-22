// NX Player — MIT License
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Tests/TestFramework.h"
#include <iostream>

namespace nx
{

class AppController;

class NXPlayerApplication final : public juce::JUCEApplication
{
public:
    NXPlayerApplication();
    ~NXPlayerApplication() override;
    const juce::String getApplicationName() override    { return "NX Player"; }
    const juce::String getApplicationVersion() override { return NX_VERSION; }
    bool moreThanOneInstanceAllowed() override          { return getCommandLineParameters().contains ("--selftest"); }

    void initialise (const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String& commandLine) override;

    static AppController* getController();

private:
    std::unique_ptr<AppController> controller;
};

} // namespace nx
