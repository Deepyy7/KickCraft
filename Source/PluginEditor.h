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
        KickWebView (KickCraftEditor& o)
            : juce::WebBrowserComponent (
                juce::WebBrowserComponent::Options{}
                    .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                    .withWinWebView2Options (
                        juce::WebBrowserComponent::Options::WinWebView2{}
                            .withUserDataFolder (juce::File::getSpecialLocation (
                                juce::File::SpecialLocationType::userApplicationDataDirectory)
                                .getChildFile ("KickCraft")))
                    .withKeepPageLoadedWhenBrowserIsHidden()
            .withNativeIntegrationEnabled()
            .withEventListener (juce::Identifier ("sendParam"),
                [p = &o](const juce::var& data) {
                    auto id  = data["id"].toString();
                    auto val = static_cast<float> (static_cast<double> (data["value"]));
                    juce::MessageManager::callAsync ([p, id, val] {
                        if (auto* param = p->processor.apvts.getParameter (id))
                            param->setValueNotifyingHost (
                                param->getNormalisableRange().convertTo0to1 (val));
                    });
                })
            .withEventListener (juce::Identifier ("exportkick"),
                [p = &o](const juce::var&) {
                    p->processor.exportRequested = true;
                })
            .withEventListener (juce::Identifier ("savekick"),
                [p = &o](const juce::var& data) {
                    auto b64 = data["b64"].toString();
                    juce::MessageManager::callAsync ([p, b64] {
                        p->processor.savedKickB64 = b64;
                    });
                })),
              owner (o) {}

        bool pageAboutToLoad (const juce::String& url) override;
        KickCraftEditor& owner;
    };

    KickWebView webView;
    std::atomic<bool> paramsNeedSync { true };
    bool firstCallDone { false };
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String      wavB64Accumulator;
    int               chunksTotal    { 0 };
    int               chunksReceived { 0 };
    std::atomic<bool> allChunksReady { false };

    juce::String      saveKickB64Accumulator;
    int               saveKickChunksTotal    { 0 };
    int               saveKickChunksReceived { 0 };
    std::atomic<bool> saveKickReady          { false };

    juce::StringArray kickRestoreChunks;
    int               kickRestoreIdx  { -1 };
    bool              kickRestoreDone { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickCraftEditor)
};
