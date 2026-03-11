#include "PluginEditor.h"
#include <BinaryData.h>

KickCraftEditor::KickCraftEditor (KickCraftProcessor& p)
    : AudioProcessorEditor (p), processor (p), webView (*this)
{
    setSize (1060, 660);
    setResizable (true, true);
    setResizeLimits (800, 500, 1400, 900);
    addAndMakeVisible (webView);

    juce::String html (BinaryData::ui_html, BinaryData::ui_htmlSize);

    juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("kickcraft_ui.html");
    tmp.replaceWithText (html);
    webView.goToURL ("file://" + tmp.getFullPathName());

    static const char* ids[] = {"sub","trans","punch","body","click","air","tight","sat","clip","mix","out",nullptr};
    for (int i=0; ids[i]; ++i) processor.apvts.addParameterListener(ids[i], this);

    paramsNeedSync = false;
    startTimer (800);
}

KickCraftEditor::~KickCraftEditor()
{
    stopTimer();
    static const char* ids[] = {"sub","trans","punch","body","click","air","tight","sat","clip","mix","out",nullptr};
    for (int i=0; ids[i]; ++i) processor.apvts.removeParameterListener(ids[i], this);
}

void KickCraftEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

void KickCraftEditor::timerCallback()
{
    if (! firstCallDone)
    {
        firstCallDone = true;
        if (auto* peer = getPeer())
            peer->setFullScreen (false);
        syncAllParamsToUI();
        webView.evaluateJavascript ("if(typeof resizeDisplays==='function'){resizeDisplays();buildKnobs();}");
        startTimerHz (30);
        return;
    }

    if (paramsNeedSync.exchange (false)) syncAllParamsToUI();

    // Send one restore chunk per tick — safe size for WKWebView evaluateJavascript
    if (kickRestoreIdx >= 0 && kickRestoreIdx < kickRestoreChunks.size())
    {
        int n     = kickRestoreIdx;
        int total = kickRestoreChunks.size();
        juce::String js =
            "if(window.__kickcraft__&&window.__kickcraft__._receiveRestoreChunk)"
            "window.__kickcraft__._receiveRestoreChunk("
            + juce::String(n) + "," + juce::String(total) + ",'"
            + kickRestoreChunks[n] + "');";
        webView.evaluateJavascript (js);
        kickRestoreIdx++;
        if (kickRestoreIdx >= total)
        {
            kickRestoreIdx  = -1;
            kickRestoreDone = true;
            kickRestoreChunks.clear();
        }
    }

    // Export error alert
    if (processor.exportFailed.exchange (false))
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon,
            "KickCraft",
            "Export failed — check disk space and write permissions.",
            "OK");
    }

    // All WAV export chunks received — decode and write to disk
    if (allChunksReady.exchange (false))
    {
        juce::MemoryOutputStream decoded;
        if (! juce::Base64::convertFromBase64 (decoded, wavB64Accumulator))
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon, "KickCraft",
                "Export failed — base64 decode error.", "OK");
            wavB64Accumulator = juce::String();
            return;
        }
        juce::MemoryBlock wavData (decoded.getData(), decoded.getDataSize());
        wavB64Accumulator = juce::String();

        fileChooser = std::make_unique<juce::FileChooser> (
            "Save Kick as WAV",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                .getChildFile ("KickCraft.wav"),
            "*.wav");

        juce::Component::SafePointer<KickCraftEditor> safeThis (this);
        fileChooser->launchAsync (
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis, wavData](const juce::FileChooser& fc)
            {
                if (safeThis == nullptr) return;
                auto result = fc.getResult();
                if (result != juce::File{})
                    if (! result.replaceWithData (wavData.getData(), wavData.getSize()))
                        juce::AlertWindow::showMessageBoxAsync (
                            juce::AlertWindow::WarningIcon, "KickCraft",
                            "Export failed — could not write file.", "OK");
            });
    }

    // All save-kick chunks received — store in processor for next editor open
    if (saveKickReady.exchange (false))
    {
        processor.savedKickB64    = saveKickB64Accumulator;
        saveKickB64Accumulator    = juce::String();
    }

    // Old-style export fallback
    if (processor.exportRequested.exchange (false))
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Save Kick as WAV",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                .getChildFile ("KickCraft.wav"),
            "*.wav");

        juce::Component::SafePointer<KickCraftEditor> safeThis (this);
        fileChooser->launchAsync (
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis](const juce::FileChooser& fc)
            {
                if (safeThis == nullptr) return;
                auto result = fc.getResult();
                if (result != juce::File{})
                    safeThis->processor.renderToWav (result);
            });
    }
}

void KickCraftEditor::parameterChanged (const juce::String&, float)
{
    paramsNeedSync = true;
}

void KickCraftEditor::syncAllParamsToUI()
{
    static const char* ids[] = {"sub","trans","punch","body","click","air","tight","sat","clip","mix","out",nullptr};
    for (int i=0; ids[i]; ++i)
    {
        float v = processor.apvts.getRawParameterValue(ids[i])->load();
        juce::String js = "if(window.__kickcraft__)window.__kickcraft__.receiveParam('"
                        + juce::String(ids[i]) + "'," + juce::String(v, 6) + ");";
        webView.evaluateJavascript (js);
    }

    // Restore previously loaded kick — chunked to stay under WKWebView JS size limit.
    // Guard: only start once per editor open, never re-trigger on knob changes.
    if (processor.savedKickB64.isNotEmpty() && !kickRestoreDone && kickRestoreIdx < 0)
    {
        kickRestoreChunks.clear();
        const int CHUNK = 4000;
        const juce::String& b64 = processor.savedKickB64;
        for (int pos = 0; pos < b64.length(); pos += CHUNK)
            kickRestoreChunks.add (b64.substring (pos, pos + CHUNK));
        kickRestoreIdx = 0;   // timerCallback sends one chunk per tick
    }
}

// ─── WebView URL interception ─────────────────────────────────────────────────
bool KickCraftEditor::KickWebView::pageAboutToLoad (const juce::String& url)
{
    if (url.startsWith ("juce://chunk"))
    {
        juce::String query = url.fromFirstOccurrenceOf ("?", false, false);
        juce::StringArray pairs; pairs.addTokens (query, "&", "");
        int n = 0, total = 0;
        juce::String data;
        for (auto& pair : pairs) {
            if (pair.startsWith ("n="))     n     = pair.fromFirstOccurrenceOf ("=", false, false).getIntValue();
            if (pair.startsWith ("total=")) total = pair.fromFirstOccurrenceOf ("=", false, false).getIntValue();
            if (pair.startsWith ("data="))  data  = juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false));
        }
        if (n == 0) {
            owner.wavB64Accumulator = juce::String();
            owner.chunksTotal       = total;
            owner.chunksReceived    = 0;
        }
        owner.wavB64Accumulator += data;
        owner.chunksReceived++;
        if (owner.chunksReceived == owner.chunksTotal && owner.chunksTotal > 0)
            owner.allChunksReady = true;
        return false;
    }

    // Save kick chunks — store in processor for restore after minimize/reopen
    if (url.startsWith ("juce://savekick"))
    {
        juce::String query = url.fromFirstOccurrenceOf ("?", false, false);
        juce::StringArray pairs; pairs.addTokens (query, "&", "");
        int n = 0, total = 0;
        juce::String data;
        for (auto& pair : pairs) {
            if (pair.startsWith ("n="))     n     = pair.fromFirstOccurrenceOf ("=", false, false).getIntValue();
            if (pair.startsWith ("total=")) total = pair.fromFirstOccurrenceOf ("=", false, false).getIntValue();
            if (pair.startsWith ("data="))  data  = juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false));
        }
        if (n == 0) {
            owner.saveKickB64Accumulator = juce::String();
            owner.saveKickChunksTotal    = total;
            owner.saveKickChunksReceived = 0;
        }
        owner.saveKickB64Accumulator += data;
        owner.saveKickChunksReceived++;
        if (owner.saveKickChunksReceived == owner.saveKickChunksTotal && owner.saveKickChunksTotal > 0)
            owner.saveKickReady = true;
        return false;
    }

    if (url.startsWith ("juce://export"))
    {
        owner.processor.exportRequested = true;
        return false;
    }

    if (url.startsWith ("juce://param"))
    {
        juce::String query = url.fromFirstOccurrenceOf ("?", false, false);
        juce::StringArray pairs; pairs.addTokens (query, "&", "");
        juce::String paramId; float value = 0.f;
        for (auto& pair : pairs) {
            if (pair.startsWith ("id="))    paramId = pair.fromFirstOccurrenceOf ("=", false, false);
            if (pair.startsWith ("value=")) value   = pair.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        }
        if (paramId.isNotEmpty())
            if (auto* param = owner.processor.apvts.getParameter (paramId))
                param->setValueNotifyingHost (param->getNormalisableRange().convertTo0to1 (value));
        return false;
    }

    return url.startsWith ("file://") || url.startsWith ("data:") || url.startsWith ("blob:");
}
