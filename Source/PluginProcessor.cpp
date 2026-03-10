#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout
KickCraftProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("sub",   "SUB",    0.0f, 1.0f,   0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("trans", "TRANS",  0.0f, 1.0f,   0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("punch", "PUNCH",  0.0f, 1.0f,   0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("body",  "BODY",   0.0f, 1.0f,   0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("click", "CLICK",  0.0f, 1.0f,   0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("air",   "AIR",    0.0f, 1.0f,   0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("tight", "TIGHT",  0.0f, 1.0f,   0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("sat",   "SAT",    0.0f, 1.0f,   0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("clip",  "CLIP",   0.0f, 1.0f,   0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("mix",   "MIX",    0.0f, 1.0f,   1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("out",   "OUTPUT",-12.0f,12.0f,  0.0f));
    return { params.begin(), params.end() };
}

KickCraftProcessor::KickCraftProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KickCraftState", createParameterLayout())
{}

KickCraftProcessor::~KickCraftProcessor() {}

void KickCraftProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32)samplesPerBlock, 2 };
    compressor.prepare (spec);
    compressor.setAttack (10.0f); compressor.setRelease (200.0f);
    compressor.setThreshold (-18.0f); compressor.setRatio (2.0f);

    float r = 0.02f;
    subSmooth.reset(sr,r);   subSmooth.setCurrentAndTargetValue(0.f);
    punchSmooth.reset(sr,r); punchSmooth.setCurrentAndTargetValue(0.f);
    bodySmooth.reset(sr,r);  bodySmooth.setCurrentAndTargetValue(0.f);
    clickSmooth.reset(sr,r); clickSmooth.setCurrentAndTargetValue(0.f);
    airSmooth.reset(sr,r);   airSmooth.setCurrentAndTargetValue(0.f);
    tightSmooth.reset(sr,r); tightSmooth.setCurrentAndTargetValue(0.f);
    satSmooth.reset(sr,r);   satSmooth.setCurrentAndTargetValue(0.f);
    clipSmooth.reset(sr,r);  clipSmooth.setCurrentAndTargetValue(0.f);
    mixSmooth.reset(sr,r);   mixSmooth.setCurrentAndTargetValue(1.f);
    outSmooth.reset(sr,r);   outSmooth.setCurrentAndTargetValue(0.f);
    transSmooth.reset(sr,r); transSmooth.setCurrentAndTargetValue(0.f);

    transEnv=1.f; transDecay=0.f; prevTransKv=-1.f;
    dryBuf.setSize(2, samplesPerBlock);

    // Allocate / reset capture ring buffer
    const int capLen = (int)(sr * kCaptureSecs);
    captureBufLen.store   (capLen, std::memory_order_relaxed);
    captureBuffer.setSize (2, capLen, false, true, false);
    captureWritePos.store (0, std::memory_order_relaxed);
    captureFilled.store   (0, std::memory_order_relaxed);

    updateFilters();
}

void KickCraftProcessor::releaseResources() {}

void KickCraftProcessor::updateFilters()
{
    float sr    = (float)sampleRate.load();
    float sub   = apvts.getRawParameterValue("sub")  ->load();
    float punch = apvts.getRawParameterValue("punch")->load();
    float body  = apvts.getRawParameterValue("body") ->load();
    float air   = apvts.getRawParameterValue("air")  ->load();

    auto sc = Coeffs::makeLowShelf  (sr, 50.f,    0.707f, juce::Decibels::decibelsToGain(sub*12.f));
    auto pc = Coeffs::makePeakFilter(sr, 140.f,   1.5f,   juce::Decibels::decibelsToGain(punch*10.f));
    auto bc = Coeffs::makePeakFilter(sr, 280.f,   1.2f,   juce::Decibels::decibelsToGain(body*8.f));
    auto cc = Coeffs::makeBandPass  (sr, 5500.f,  1.8f);
    auto ac = Coeffs::makeHighShelf (sr, 10000.f, 0.707f, juce::Decibels::decibelsToGain(air*12.f));

    *subL.coefficients=*sc;   *subR.coefficients=*sc;
    *punchL.coefficients=*pc; *punchR.coefficients=*pc;
    *bodyL.coefficients=*bc;  *bodyR.coefficients=*bc;
    *clickBpL.coefficients=*cc; *clickBpR.coefficients=*cc;
    *airL.coefficients=*ac;   *airR.coefficients=*ac;
}

void KickCraftProcessor::updateCompressor()
{
    float tight = apvts.getRawParameterValue("tight")->load();
    compressor.setRatio  (1.f + tight * 7.f);
    compressor.setRelease(400.f - tight * 360.f);
}

void KickCraftProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int N  = buffer.getNumSamples();
    const int CH = juce::jmin(buffer.getNumChannels(), 2);

    subSmooth  .setTargetValue(apvts.getRawParameterValue("sub")  ->load());
    punchSmooth.setTargetValue(apvts.getRawParameterValue("punch")->load());
    bodySmooth .setTargetValue(apvts.getRawParameterValue("body") ->load());
    clickSmooth.setTargetValue(apvts.getRawParameterValue("click")->load());
    airSmooth  .setTargetValue(apvts.getRawParameterValue("air")  ->load());
    tightSmooth.setTargetValue(apvts.getRawParameterValue("tight")->load());
    satSmooth  .setTargetValue(apvts.getRawParameterValue("sat")  ->load());
    clipSmooth .setTargetValue(apvts.getRawParameterValue("clip") ->load());
    mixSmooth  .setTargetValue(apvts.getRawParameterValue("mix")  ->load());
    outSmooth  .setTargetValue(apvts.getRawParameterValue("out")  ->load());

    updateFilters(); updateCompressor();

    float transKv = apvts.getRawParameterValue("trans")->load();
    if (transKv != prevTransKv) {
        if (transKv > 0.005f) {
            float ms = 1.f + (1.f-transKv)*4.f;
            transEnv   = 1.f + transKv*5.f;
            transDecay = std::exp(-1.f/((float)sampleRate.load()*ms*0.001f));
        } else { transEnv=1.f; transDecay=0.f; }
        prevTransKv = transKv;
    }

    dryBuf.setSize(CH, N, false, false, true);
    for (int ch=0; ch<CH; ++ch) dryBuf.copyFrom(ch,0,buffer,ch,0,N);

    for (int s=0; s<N; ++s) {
        if (transDecay>0.f && transEnv>1.001f) transEnv*=transDecay; else transEnv=1.f;

        float click = clickSmooth.getNextValue();
        float sat   = satSmooth.getNextValue();
        float clip  = clipSmooth.getNextValue();
        subSmooth.getNextValue(); punchSmooth.getNextValue(); bodySmooth.getNextValue();
        airSmooth.getNextValue(); tightSmooth.getNextValue();
        mixSmooth.getNextValue(); outSmooth.getNextValue();

        for (int ch=0; ch<CH; ++ch) {
            float x = buffer.getSample(ch,s) * transEnv;
            x = ch==0 ? subL.processSample(x)    : subR.processSample(x);
            x = ch==0 ? punchL.processSample(x)  : punchR.processSample(x);
            x = ch==0 ? bodyL.processSample(x)   : bodyR.processSample(x);
            float cb = ch==0 ? clickBpL.processSample(x) : clickBpR.processSample(x);
            x += cb * click * 6.f;
            x = ch==0 ? airL.processSample(x) : airR.processSample(x);
            if (sat>0.001f) { float d=1.f+sat*3.f; x=softClip(x*d)/softClip(d); }
            if (clip>0.001f) { float t=1.f-clip*0.5f; x=softClipHard(x/t)*t; }
            if (!std::isfinite(x)) x=0.f;
            buffer.setSample(ch,s,x);
        }
    }

    { juce::dsp::AudioBlock<float> blk(buffer); compressor.process(juce::dsp::ProcessContextReplacing<float>(blk)); }

    float mix = mixSmooth.getCurrentValue();
    float og  = juce::Decibels::decibelsToGain(outSmooth.getCurrentValue());
    for (int ch=0; ch<CH; ++ch) {
        auto* w = buffer.getWritePointer(ch);
        auto* d = dryBuf.getReadPointer(ch);
        for (int s=0; s<N; ++s) {
            w[s] = (w[s]*mix + d[s]*(1.f-mix)) * og;
            if (!std::isfinite(w[s])) w[s]=0.f;
        }
    }

    // ── Capture processed output into ring buffer ─────────────────────────────
    // Block-wise copy: at most 2 modulo ops per block instead of N.
    // CH==1 (mono): mirror L→R so the exported WAV always has both channels.
    {
        const int bufLen = captureBufLen.load (std::memory_order_relaxed);
        if (bufLen > 0)
        {
            int pos      = captureWritePos.load (std::memory_order_relaxed);
            int filled   = captureFilled.load   (std::memory_order_relaxed);
            int rem      = N;
            int srcOff   = 0;

            while (rem > 0)
            {
                const int chunk = juce::jmin (rem, bufLen - pos);
                const float* src0 = buffer.getReadPointer (0) + srcOff;
                const float* src1 = buffer.getReadPointer (CH > 1 ? 1 : 0) + srcOff;
                juce::FloatVectorOperations::copy (captureBuffer.getWritePointer (0) + pos, src0, chunk);
                juce::FloatVectorOperations::copy (captureBuffer.getWritePointer (1) + pos, src1, chunk);
                pos    = (pos + chunk) % bufLen;
                srcOff += chunk;
                rem    -= chunk;
            }

            captureWritePos.store (pos,                              std::memory_order_relaxed);
            captureFilled.store   (juce::jmin (filled + N, bufLen), std::memory_order_relaxed);
        }
    }
}

void KickCraftProcessor::getStateInformation(juce::MemoryBlock& d)
{ auto s=apvts.copyState(); std::unique_ptr<juce::XmlElement> x(s.createXml()); copyXmlToBinary(*x,d); }

void KickCraftProcessor::setStateInformation(const void* d, int sz)
{ std::unique_ptr<juce::XmlElement> x(getXmlFromBinary(d,sz)); if(x&&x->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*x)); }

juce::AudioProcessorEditor* KickCraftProcessor::createEditor() { return new KickCraftEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KickCraftProcessor(); }

// ─── Export: write captured audio to WAV ─────────────────────────────────────
void KickCraftProcessor::renderToWav (const juce::File& outFile)
{
    // Snapshot ring buffer state (message thread — audio thread may still write)
    const int bufLen   = captureBufLen.load  (std::memory_order_relaxed);
    const int filled   = captureFilled.load   (std::memory_order_relaxed);
    const int writePos = captureWritePos.load  (std::memory_order_relaxed);
    const int len      = juce::jmin (filled, bufLen);

    if (len == 0) { exportFailed = true; return; }   // nothing recorded yet

    // Copy ring buffer → linear output buffer
    juce::AudioBuffer<float> buf (2, len);
    const int readStart = (writePos - len + bufLen) % bufLen;
    for (int s = 0; s < len; ++s)
    {
        const int rp = (readStart + s) % bufLen;
        buf.setSample (0, s, captureBuffer.getSample (0, rp));
        buf.setSample (1, s, captureBuffer.getSample (1, rp));
    }

    // Write 24-bit stereo WAV
    const double srD = [this]{ double v = sampleRate.load(); return v > 0.0 ? v : 44100.0; }();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> fos (outFile.createOutputStream());
    if (!fos || fos->failedToOpen()) { exportFailed = true; return; }

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (fos.get(), srD, 2, 24, {}, 0));
    if (!writer) { exportFailed = true; return; }

    fos.release();   // writer owns the stream
    if (! writer->writeFromAudioSampleBuffer (buf, 0, len))
        exportFailed = true;
}
