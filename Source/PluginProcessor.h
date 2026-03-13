#pragma once
#include <JuceHeader.h>

class KickCraftProcessor : public juce::AudioProcessor
{
public:
    KickCraftProcessor();
    ~KickCraftProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "KickCraft"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<bool> exportRequested { false };
    std::atomic<bool> exportFailed    { false };
    void renderToWav (const juce::File& outFile);

    juce::String savedKickB64;   // persists across editor instances — restored on reopen

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    using IIR    = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    IIR subL, subR;
    IIR punchL, punchR;
    IIR bodyL, bodyR;
    IIR clickBpL, clickBpR;
    IIR airL, airR;

    juce::dsp::Compressor<float> compressor;

    std::atomic<double> sampleRate { 44100.0 };

    juce::SmoothedValue<float> subSmooth, transSmooth, punchSmooth, bodySmooth;
    juce::SmoothedValue<float> clickSmooth, airSmooth, tightSmooth;
    juce::SmoothedValue<float> satSmooth, clipSmooth, outSmooth;

    float transEnv    { 1.0f };
    float transDecay  { 0.0f };
    float prevTransKv { -1.0f };

    juce::AudioBuffer<float> dryBuf;

    static constexpr int kCaptureSecs = 3;
    juce::AudioBuffer<float> captureBuffer;
    std::atomic<int>         captureWritePos { 0 };
    std::atomic<int>         captureFilled   { 0 };
    std::atomic<int>         captureBufLen   { 0 };

    void updateFilters();
    void updateCompressor();

    static float softClip (float x) noexcept
    {
        const float a = x * x;
        return x * (27.0f + a) / (27.0f + 9.0f * a);
    }
    static float softClipHard (float x) noexcept
    {
        return juce::jlimit (-1.0f, 1.0f, x - (x * x * x) / 3.0f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickCraftProcessor)
};
