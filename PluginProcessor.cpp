#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SaturatorProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Input (dB)",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive",
        NormalisableRange<float> (0.0f, 10.0f, 0.01f), 3.0f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "punish", 1 }, "Ruin", false));  // id kept for old projects
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tone", 1 }, "Tone",
        NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "thump", 1 }, "Thump",
        NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "lowcut", 1 }, "Low Cut",
        NormalisableRange<float> (20.0f, 1000.0f, 0.0f, 0.3f), 20.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "highcut", 1 }, "High Cut",
        NormalisableRange<float> (1000.0f, 20000.0f, 0.0f, 0.5f), 20000.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output (dB)",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));

    // ABYSS engine (0..100 %)
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "collapse", 1 }, "Collapse",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "fracture", 1 }, "Fracture",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "aftershock", 1 }, "Aftershock",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 10.0f));

    return { p.begin(), p.end() };
}

SaturatorProcessor::SaturatorProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pInput      = apvts.getRawParameterValue ("input");
    pDrive      = apvts.getRawParameterValue ("drive");
    pRuin       = apvts.getRawParameterValue ("punish");
    pTone       = apvts.getRawParameterValue ("tone");
    pThump      = apvts.getRawParameterValue ("thump");
    pLowCut     = apvts.getRawParameterValue ("lowcut");
    pHighCut    = apvts.getRawParameterValue ("highcut");
    pMix        = apvts.getRawParameterValue ("mix");
    pOutput     = apvts.getRawParameterValue ("output");
    pCollapse   = apvts.getRawParameterValue ("collapse");
    pFracture   = apvts.getRawParameterValue ("fracture");
    pAftershock = apvts.getRawParameterValue ("aftershock");
}

bool SaturatorProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

void SaturatorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };

    for (auto* f : { &thumpShelf, &toneLow, &toneHigh, &lowCut, &highCut, &dcBlock })
        f->reset();

    {
        abyss::Biquad dc;
        dc.setHighpass (sampleRate, 12.0);
        dcBlock.setCoeffs (dc);
    }

    inputGain.prepare (spec); inputGain.setRampDurationSeconds (0.02);
    inGain.prepare (spec);    inGain.setRampDurationSeconds (0.02);
    outGain.prepare (spec);   outGain.setRampDurationSeconds (0.02);

    oversampling.initProcessing ((size_t) samplesPerBlock);
    oversampling.reset();

    // the ABYSS engine runs inside the oversampled block, so it needs the oversampled rate
    engine.prepare (sampleRate * (double) oversampling.getOversamplingFactor());
    engine.setParams ((double) pCollapse->load() * 0.01, (double) pFracture->load() * 0.01,
                      (double) pAftershock->load() * 0.01);
    engine.reset();

    mixer.prepare (spec);
    mixer.setMixingRule (juce::dsp::DryWetMixingRule::balanced);
    const auto latency = oversampling.getLatencyInSamples();
    mixer.setWetLatency (latency);
    setLatencySamples ((int) std::round (latency));    // the engine itself adds no latency

    driveLevel.store (0.0f);
}

void SaturatorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 2)
        return;

    const float inDb   = pInput->load();
    const float drive  = pDrive->load();
    const bool  ruin   = pRuin->load() > 0.5f;
    const float tone   = pTone->load();
    const float thump  = pThump->load();
    const float lc     = pLowCut->load();
    const float hc     = juce::jmin (pHighCut->load(), (float) (sr * 0.45));
    const float mix    = pMix->load() * 0.01f;
    const float outDb  = pOutput->load();

    // ---- filter coefficients: pure arithmetic, no allocation
    {
        abyss::Biquad p;
        p.setLowShelf (sr, 90.0, 0.7, (double) juce::Decibels::decibelsToGain (thump * 1.2f));
        thumpShelf.setCoeffs (p);

        const float tiltDb = tone * 6.0f;
        p.setLowShelf  (sr, 650.0, 0.5, (double) juce::Decibels::decibelsToGain (-tiltDb));
        toneLow.setCoeffs (p);
        p.setHighShelf (sr, 650.0, 0.5, (double) juce::Decibels::decibelsToGain ( tiltDb));
        toneHigh.setCoeffs (p);

        p.setHighpass (sr, (double) lc);  lowCut.setCoeffs (p);
        p.setLowpass  (sr, (double) hc);  highCut.setCoeffs (p);
    }

    inputGain.setGainDecibels (inDb);
    inGain.setGainDecibels  (drive * 2.5f + (ruin ? 12.0f : 0.0f));
    // rough auto-compensation so loudness doesn't explode with Drive
    outGain.setGainDecibels (outDb - drive * 1.2f - (ruin ? 6.0f : 0.0f));
    mixer.setWetMixProportion (mix);

    engine.setParams ((double) pCollapse->load() * 0.01,
                      (double) pFracture->load() * 0.01,
                      (double) pAftershock->load() * 0.01);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);

    mixer.pushDrySamples (block);

    inputGain.process (ctx);
    thumpShelf.process (buffer);
    inGain.process (ctx);

    // level meter for the GUI: how hard the engine is being hit
    {
        float pk = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            pk = juce::jmax (pk, buffer.getMagnitude (ch, 0, n));
        const float rel = std::exp (-(float) n / (float) (sr * 0.18));
        driveLevel.store (juce::jmax (pk, driveLevel.load() * rel));
    }

    // ---- ABYSS at 4x oversampling (COLLAPSE / FRACTURE / AFTERSHOCK)
    auto up = oversampling.processSamplesUp (block);
    engine.process (up.getChannelPointer (0), up.getChannelPointer (1), (int) up.getNumSamples());
    oversampling.processSamplesDown (block);

    dcBlock.process (buffer);
    toneLow.process (buffer);
    toneHigh.process (buffer);
    lowCut.process (buffer);
    highCut.process (buffer);
    outGain.process (ctx);

    mixer.mixWetSamples (block);
}

juce::AudioProcessorEditor* SaturatorProcessor::createEditor()
{
    return new SaturatorEditor (*this);
}

//==============================================================================
void SaturatorProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SaturatorProcessor::setParamValue (const char* id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

void SaturatorProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // Look for a state saved by an older build (Veil / Halo / Abyss modes + Aura / Haunt knobs)
    bool hasNew = false;
    float oldHaunt = -1.0f, oldAura = -1.0f;
    for (auto* e : xml->getChildIterator())
    {
        const auto id = e->getStringAttribute ("id");
        if (id == "collapse" || id == "fracture" || id == "aftershock") hasNew = true;
        else if (id == "haunt") oldHaunt = (float) e->getDoubleAttribute ("value");
        else if (id == "aura")  oldAura  = (float) e->getDoubleAttribute ("value");
    }

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // Migration: the ghost-echo amount becomes AFTERSHOCK and the style "Aura" becomes FRACTURE
    // (both were 0..10, now 0..100 %). Drive / Ruin / Tone / filters / Mix / Output keep their IDs.
    if (! hasNew)
    {
        if (oldHaunt >= 0.0f) setParamValue ("aftershock", juce::jlimit (0.0f, 100.0f, oldHaunt * 10.0f));
        if (oldAura  >= 0.0f) setParamValue ("fracture",   juce::jlimit (0.0f, 100.0f, oldAura  * 10.0f));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SaturatorProcessor();
}
