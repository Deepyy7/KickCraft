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

    // JS bridge injection
    juce::String bridge = R"BRIDGE(
<script>
(function() {
  if (!window.__kickcraft__) window.__kickcraft__ = {};

  // iframe nav — fallback for file:// context where __JUCE__ is not available
  function _juceNav(url) {
    var f = document.getElementById('__juce_bridge__');
    if (!f) { f = document.createElement('iframe'); f.id='__juce_bridge__'; f.style.display='none'; document.body.appendChild(f); }
    f.src = url;
  }
  window.__kickcraft__._nav = _juceNav;

  function _emit(name, data) {
    try { window.__JUCE__.backend.emitEvent(name, data); return; } catch(e) {}
    // fallback: URL nav
    var q = Object.keys(data).map(function(k){ return encodeURIComponent(k)+'='+encodeURIComponent(String(data[k])); }).join('&');
    _juceNav('juce://' + name + (q ? '?' + q : ''));
  }

  window.__kickcraft__.sendParam = function(id, value) {
    _emit('sendParam', {id: id, value: value});
  };

  window.__kickcraft__.receiveParam = function(id, value) {
    if (typeof kv === 'undefined') return;
    kv[id] = value;
    var k = KNOBS.find(function(kk){return kk.id===id;});
    if (k && kCvs[id]) {
      renderKnob(kCvs[id], k.sz, value, k);
      applyDSP(); drawWaveform();
      var tv = document.getElementById('tipval-' + id);
      if (tv) tv.textContent = id==='out'?(value>=0?'+':'')+parseFloat(value).toFixed(1)+' dB':Math.round(value*100)+'%';
    }
  };

  window.__kickcraft__.exportKick = function() {
    _emit('exportkick', {});
  };

  // Restore a previously loaded kick from base64 (called after minimize/reopen)
  window.__kickcraft__.restoreKickFromB64 = function(b64) {
    if (!b64) return;
    initAudio();
    if (actx.state === 'suspended') actx.resume();
    try {
      var bin = atob(b64), len = bin.length;
      var ab = new ArrayBuffer(len), v = new Uint8Array(ab);
      for (var i = 0; i < len; i++) v[i] = bin.charCodeAt(i);
      actx.decodeAudioData(ab, function(buf) {
        audioBuf = buf; extractWave(buf); drawWaveform();
      });
    } catch(e) {}
  };

  // Receive restore chunks from C++ — chunked to stay under WKWebView JS size limit
  window.__kickcraft__._receiveRestoreChunk = function(n, total, data) {
    if (n === 0) window.__kickcraft__._restoreBuf = '';
    window.__kickcraft__._restoreBuf += data;
    if (n === total - 1) {
      window.__kickcraft__.restoreKickFromB64(window.__kickcraft__._restoreBuf);
      window.__kickcraft__._restoreBuf = '';
    }
  };

  window.__kickcraft__._sendKickB64 = function(b64) {
    try { window.__JUCE__.backend.emitEvent('savekick', {b64: b64}); return; } catch(e) {}
    // fallback: chunked nav
    var CHUNK = 8000, total = Math.ceil(b64.length / CHUNK);
    function send(n) {
      if (n >= total) return;
      _juceNav('juce://savekick?n=' + n + '&total=' + total + '&data=' + encodeURIComponent(b64.slice(n*CHUNK,(n+1)*CHUNK)));
      setTimeout(function(){ send(n+1); }, 30);
    }
    send(0);
  };

})();
</script>
)BRIDGE";

    html = html.replace ("</body>", bridge + "\n</body>");

    // Patch knob mouseup — send param to C++
    html = html.replace (
        "window.removeEventListener('mouseup',   onUp);\n          pushUndo();",
        "window.removeEventListener('mouseup',   onUp);\n          pushUndo();\n          if(window.__kickcraft__)window.__kickcraft__.sendParam(k_.id,kv[k_.id]);"
    );
    // Patch double-click reset
    html = html.replace (
        "pushUndo(); kv[k_.id] = k_.def; renderKnob(cvs_, sz_, k_.def, k_); applyDSP(); drawWaveform();",
        "pushUndo(); kv[k_.id]=k_.def; renderKnob(cvs_,sz_,k_.def,k_); applyDSP(); drawWaveform(); if(window.__kickcraft__)window.__kickcraft__.sendParam(k_.id,k_.def);"
    );
    // Patch preset load
    html = html.replace (
        "var id; for (id in p.v) { kv[id] = p.v[id]; if (window.__kickcraft__) window.__kickcraft__.sendParam(id, p.v[id]); }",
        "var id; for(id in p.v){kv[id]=p.v[id]; if(window.__kickcraft__)window.__kickcraft__.sendParam(id,p.v[id]);}"
    );
    // Patch loadFileObj — after decoding audio, also send base64 to C++ for restore
    html = html.replace (
        "  reader.readAsArrayBuffer(file);\n}",
        "  reader.readAsArrayBuffer(file);\n"
        "  if (window.__kickcraft__ && window.__kickcraft__._sendKickB64) {\n"
        "    var r2 = new FileReader();\n"
        "    r2.onload = function(e2) {\n"
        "      var b64 = e2.target.result.split(',')[1];\n"
        "      window.__kickcraft__._sendKickB64(b64);\n"
        "    };\n"
        "    r2.readAsDataURL(file);\n"
        "  }\n"
        "}"
    );

    pendingHtml = html;
    webView.goToURL ("https://kickcraft.localhost/");

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
