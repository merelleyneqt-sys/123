#pragma once
#include "PluginProcessor.h"
#include <array>
#include <vector>

//==============================================================================
namespace Ritual
{
    // Violet-black palette (names are historical)
    inline const juce::Colour ink   { 0xff060410 };   // near-black base
    inline const juce::Colour blood { 0xff3a1a78 };   // deep violet
    inline const juce::Colour ember { 0xffa56bff };   // bright violet glow
    inline const juce::Colour gold  { 0xff9a8bcf };   // lilac ornaments / lines
    inline const juce::Colour bone  { 0xffeee8ff };   // pale lavender text
    inline const juce::Colour ash   { 0xff6a6188 };   // muted violet-grey
    inline const juce::Colour dim   { 0xffb4abd2 };   // captions

    // Cinzel (embedded) for all UI text; falls back to the system serif if the font file was not bundled
    juce::Font font (float height);
    // The "ONYX" wordmark keeps the original system serif
    juce::Font titleFont (float height);

    // Text with letter-spacing, centred in `area`.
    void drawTracked (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                      const juce::Font&, float tracking, juce::Colour);
}

//==============================================================================
class RitualLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RitualLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;     // value readouts: text only, no frame
    juce::Font getLabelFont (juce::Label&) override { return Ritual::font (13.0f); }

private:
    juce::Path texture;     // unit-radius gothic ornament (lancet arches + quatrefoil), built once
};

//==============================================================================
class SaturatorEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit SaturatorEditor (SaturatorProcessor&);
    ~SaturatorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;     // shattering glass lives on top of the knobs
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct KnobUI
    {
        juce::Slider slider;
        juce::String title;
        juce::String caption;                  // short "what it does" line under the knob
        juce::Rectangle<int> titleRect;
        juce::Rectangle<int> captionRect;
        std::unique_ptr<SliderAtt> att;
    };

    struct Spark { float x, y, vy, phase, size, bright; };

    struct Crack { std::vector<juce::Point<float>> pts; };          // offsets from the eclipse centre
    struct Shard
    {
        juce::Point<float> pos, vel;
        std::array<juce::Point<float>, 3> tri;
        float rot = 0.0f, vrot = 0.0f, life = 0.0f;
    };

    void setupKnob (KnobUI&, const juce::String& paramId, const juce::String& title,
                    const juce::String& caption, bool big);
    void renderBackdrop();
    void buildCracks();
    void spawnShards();
    void timerCallback() override;

    SaturatorProcessor& proc;
    RitualLookAndFeel laf;

    KnobUI drive, input, thump, tone, lowCut, highCut, output, mix, collapse, fracture, aftershock;
    std::array<KnobUI*, 11> knobs { &drive, &input, &thump, &tone, &lowCut, &highCut, &output,
                                    &mix, &collapse, &fracture, &aftershock };

    juce::ToggleButton ruinBtn;
    std::unique_ptr<ButtonAtt> ruinAtt;

    juce::Image backdrop;
    std::array<Spark, 28> sparks;
    float phase = 0.0f;
    float level = 0.0f;                 // smoothed signal level driving the living eclipse

    // shattering glass (Drive at maximum)
    std::vector<Crack> cracks;
    std::array<Shard, 36> shards {};
    float crackProgress = 0.0f;         // 0 = intact, 1 = fully cracked
    float flash = 0.0f;
    bool  atMax = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SaturatorEditor)
};
