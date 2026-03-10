#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class KickCraftEditor : public juce::AudioProcessorEditor,
                        public juce::Timer,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit KickCraftEditor (KickCraftProcessor&);
    ~KickCraftEditor() override;
    void resized() override;
    void timerCallback() override;

private:
    void parameterChanged (const juce::String& paramID, float newValue) override;
    void syncAllParamsToUI();

    KickCraftProcessor& processor;

    struct KickWebView : public juce::WebBrowserComponent
    {
        KickWebView (KickCraftEditor& o) : owner(o) {}
        bool pageAboutToLoad (const juce::String& url) override;
        KickCraftEditor& owner;
    };

    KickWebView webView;
    std::atomic<bool> paramsNeedSync { true };
    bool firstCallDone { false };
    std::unique_ptr<juce::FileChooser> fileChooser;

    // WAV export — chunk assembly (message thread only)
    juce::String      wavB64Accumulator;
    int               chunksTotal    { 0 };
    int               chunksReceived { 0 };
    std::atomic<bool> allChunksReady { false };

    // Kick save — chunk assembly for restore after minimize (message thread only)
    juce::String      saveKickB64Accumulator;
    int               saveKickChunksTotal    { 0 };
    int               saveKickChunksReceived { 0 };
    std::atomic<bool> saveKickReady          { false };

    // Kick restore — chunked C++→JS injection (message thread only)
    juce::StringArray kickRestoreChunks;
    int               kickRestoreIdx  { -1 };    // -1 = idle
    bool              kickRestoreDone { false };  // true after first restore completes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickCraftEditor)
};
