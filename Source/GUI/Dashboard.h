// NX Player — MIT License
// Settings › Audio & Système: the hardware dashboard (Audirvana style) — COMPUTER card (OS, CPU, RAM, Memory
// Play, sleep prevention) linked to the AUDIO DEVICE card (drawing, supported rates) by the connection
// (built-in / network / USB, exclusive access). And the NX Remote panel (QR code).
#pragma once

#include "Widgets.h"
#include "../Platform/SystemMonitor.h"

namespace nx
{

constexpr int HardwareDashboardHeight = 330;
constexpr int RemotePanelHeight = 270;

class HardwareDashboard : public juce::Component, private juce::Timer
{
public:
    explicit HardwareDashboard (AppContext& c);
    void paint (juce::Graphics&) override;
    void refresh();
    /** Rates shown in the matrix and how the device takes them (tests). */
    enum class RateSupport { None, Pcm, DoP, Native };
    static RateSupport supportFor (const DeviceCapabilities& caps, double rate, bool dsd);
    SystemSnapshot getSnapshot() const { return snapshot; }
private:
    void timerCallback() override;
    AppContext& ctx;
    SystemMonitor monitor;
    SystemSnapshot snapshot;
    DeviceCapabilities caps;
    OutputStatus status;
    double phase = 0.0;
    int ticks = 0;
};

/** QR code of a text (URL), black on white with a quiet zone. */
class QrCodeView : public juce::Component
{
public:
    void setText (const juce::String& t);
    void paint (juce::Graphics&) override;
    int getModuleCount() const noexcept { return size; }
    bool getModule (int x, int y) const noexcept { return x >= 0 && y >= 0 && x < size && y < size && modules[(size_t) (y * size + x)]; }
private:
    juce::String text;
    int size = 0;
    std::vector<bool> modules;
};

class RemotePanel : public juce::Component, private juce::Timer
{
public:
    explicit RemotePanel (AppContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    QrCodeView qr;
private:
    void timerCallback() override { repaint(); }
    AppContext& ctx;
    juce::TextButton openButton, copyButton;
};

std::unique_ptr<juce::Component> makeHardwareDashboard (AppContext& ctx);
std::unique_ptr<juce::Component> makeRemotePanel (AppContext& ctx);

} // namespace nx
