#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "AbyssEngine.h"

class SaturatorProcessor : public juce::AudioProcessor
{
public:
    SaturatorProcessor();
    ~SaturatorProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Onyx"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // How hard the signal hits the engine (post-drive peak, with release).
    // Read by the editor to drive the "living eclipse".
    std::atomic<float> driveLevel { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParamValue (const char* id, float plainValue);

    // Stereo biquad on plain buffers (double precision, no allocation)
    struct StereoBiquad
    {
        abyss::Biquad ch[2];
        void setCoeffs (const abyss::Biquad& p) noexcept { ch[0].copyCoeffs (p); ch[1].copyCoeffs (p); }
        void reset() noexcept { ch[0].reset(); ch[1].reset(); }
        void process (juce::AudioBuffer<float>& b) noexcept
        {
            const int n = b.getNumSamples();
            for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
            {
                auto* d = b.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                    d[i] = (float) ch[c].process ((double) d[i]);
            }
        }
    };

    StereoBiquad thumpShelf, toneLow, toneHigh, lowCut, highCut, dcBlock;
    juce::dsp::Gain<float> inputGain, inGain, outGain;
    juce::dsp::Oversampling<float> oversampling { 2, 2,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true };
    juce::dsp::DryWetMixer<float> mixer;
    abyss::Engine engine;

    // cached raw parameter pointers (no lookups in the audio callback)
    std::atomic<float> *pInput = nullptr, *pDrive = nullptr, *pRuin = nullptr, *pTone = nullptr,
                       *pThump = nullptr, *pLowCut = nullptr, *pHighCut = nullptr, *pMix = nullptr,
                       *pOutput = nullptr, *pCollapse = nullptr, *pFracture = nullptr, *pAftershock = nullptr;

    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SaturatorProcessor)
};
