#include "PluginEditor.h"

// Assets are embedded by CMake (juce_add_binary_data) when the files exist next to CMakeLists.txt
#ifndef ONYX_HAS_BG
 #define ONYX_HAS_BG 0
#endif
#ifndef ONYX_HAS_CINZEL
 #define ONYX_HAS_CINZEL 0
#endif
#if ONYX_HAS_BG || ONYX_HAS_CINZEL
 #include "BinaryData.h"
#endif

using namespace juce;

namespace
{
    constexpr int   kW  = 800;
    constexpr int   kH  = 660;
    constexpr float kCX = 400.0f;   // centre of the eclipse
    constexpr float kCY = 235.0f;

    Image backgroundImage()
    {
#if ONYX_HAS_BG
        static Image img = ImageCache::getFromMemory (BinaryData::background_jpg,
                                                      BinaryData::background_jpgSize);
        return img;
#else
        return {};
#endif
    }

    // Minimalist gothic ornament at unit radius: a ring of lancet (pointed) arches and a quatrefoil
    Path makeGothicTexture()
    {
        Path p;
        const int N = 12;
        const float r0 = 0.60f, r1 = 0.93f, hw = 0.15f;
        for (int k = 0; k < N; ++k)
        {
            const float a = (float) k / (float) N * MathConstants<float>::twoPi;
            const Point<float> u (std::sin (a), -std::cos (a)), t (std::cos (a), std::sin (a));
            const Point<float> baseL = u * r0 - t * hw, baseR = u * r0 + t * hw, apex = u * r1;
            const Point<float> ctrlL = baseL + u * ((r1 - r0) * 0.8f), ctrlR = baseR + u * ((r1 - r0) * 0.8f);
            p.startNewSubPath (baseL);
            p.quadraticTo (ctrlL, apex);
            p.quadraticTo (ctrlR, baseR);
        }
        for (int k = 0; k < 4; ++k)                       // quatrefoil
        {
            const float a = (float) k * MathConstants<float>::halfPi;
            const Point<float> c (std::sin (a) * 0.17f, -std::cos (a) * 0.17f);
            p.addEllipse (c.x - 0.11f, c.y - 0.11f, 0.22f, 0.22f);
        }
        p.addEllipse (-0.04f, -0.04f, 0.08f, 0.08f);
        return p;
    }
}

//==============================================================================
// Helpers
//==============================================================================
namespace Ritual
{
    Font font (float height)
    {
#if ONYX_HAS_CINZEL
        static const Typeface::Ptr tf = Typeface::createSystemTypefaceFor (BinaryData::Cinzel_ttf,
                                                                           (size_t) BinaryData::Cinzel_ttfSize);
        if (tf != nullptr)
            return Font (FontOptions (tf).withHeight (height));
#endif
        return Font (FontOptions (Font::getDefaultSerifFontName(), height, Font::plain));
    }

    Font titleFont (float height)
    {
        return Font (FontOptions (Font::getDefaultSerifFontName(), height, Font::plain));
    }

    void drawTracked (Graphics& g, const String& text, Rectangle<float> area,
                      const Font& fnt, float tracking, Colour col)
    {
        if (text.isEmpty())
            return;

        std::vector<float> widths;
        float total = 0.0f;

        for (auto ch : text)
        {
            float w = fnt.getHeight() * 0.3f;
            if (ch != ' ')
            {
                GlyphArrangement ga;
                ga.addLineOfText (fnt, String::charToString (ch), 0.0f, 0.0f);
                w = ga.getBoundingBox (0, -1, true).getWidth();
            }
            widths.push_back (w);
            total += w + tracking;
        }
        total -= tracking;

        float x = area.getCentreX() - total * 0.5f;
        g.setColour (col);
        g.setFont (fnt);

        size_t i = 0;
        for (auto ch : text)
        {
            g.drawText (String::charToString (ch),
                        Rectangle<float> (x, area.getY(), widths[i] + 2.0f, area.getHeight()),
                        Justification::centredLeft, false);
            x += widths[i++] + tracking;
        }
    }
}

//==============================================================================
// Look and feel
//==============================================================================
RitualLookAndFeel::RitualLookAndFeel()
    : texture (makeGothicTexture())
{
    setColour (Slider::textBoxTextColourId,       Ritual::bone.withAlpha (0.92f));
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxOutlineColourId,    Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId,  Ritual::blood);
    setColour (Label::outlineColourId,            Colours::transparentBlack);
    setColour (TextEditor::backgroundColourId,    Colours::transparentBlack);
    setColour (TextEditor::outlineColourId,       Colours::transparentBlack);
    setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
    setColour (TextEditor::textColourId,          Ritual::bone);
    setColour (TextEditor::highlightColourId,     Ritual::blood);
    setColour (CaretComponent::caretColourId,     Ritual::ember);
}

// Value readouts: only the text, no outline and no background
void RitualLookAndFeel::drawLabel (Graphics& g, Label& label)
{
    if (label.isBeingEdited())
        return;

    g.setColour (label.findColour (Label::textColourId));
    g.setFont (getLabelFont (label));
    g.drawText (label.getText(), label.getLocalBounds(), label.getJustificationType(), false);
}

void RitualLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                          float a0, float a1, Slider& s)
{
    using namespace Ritual;

    const bool big = (bool) s.getProperties().getWithDefault ("big", false);
    const float lvl = static_cast<float> ((double) s.getProperties().getWithDefault ("level", 0.0));
    auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto  c = b.getCentre();
    const float ang = a0 + pos * (a1 - a0);

    auto pt = [&] (float rad, float a) { return Point<float> (c.x + rad * std::sin (a),
                                                              c.y - rad * std::cos (a)); };

    // ---- eclipse corona (Drive knob only): value sets the base heat, the live level makes it flare
    if (big)
    {
        const float heat = jlimit (0.0f, 1.0f, 0.30f * pos + 0.85f * lvl);

        ColourGradient cg (Colours::transparentBlack, c, Colours::transparentBlack, pt (r, 0.0f), true);
        cg.addColour (0.46, ember.withAlpha (0.0f));
        cg.addColour (0.55, ember.withAlpha (0.12f + 0.75f * heat));
        cg.addColour (0.66, blood.withAlpha (0.10f + 0.35f * heat));
        cg.addColour (0.92, blood.withAlpha (0.0f));
        g.setGradientFill (cg);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }

    // ---- outer ring + ticks
    g.setColour (gold.withAlpha (0.45f));
    g.drawEllipse (c.x - r * 0.98f, c.y - r * 0.98f, r * 1.96f, r * 1.96f, 1.0f);

    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (float) i / 10.0f * (a1 - a0);
        const bool major = (i == 0 || i == 5 || i == 10);
        g.setColour (gold.withAlpha (major ? 0.85f : 0.45f));
        g.drawLine (Line<float> (pt (r * (major ? 0.86f : 0.89f), a), pt (r * 0.94f, a)), major ? 1.4f : 1.0f);
    }

    // ---- track + glowing value arc
    const float arcR = r * 0.76f;
    Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, a1, true);
    g.setColour (ash.withAlpha (0.5f));
    g.strokePath (track, PathStrokeType (1.6f));

    if (pos > 0.001f)
    {
        Path v;
        v.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, ang, true);
        for (int i = 3; i > 0; --i)
        {
            g.setColour (ember.withAlpha (0.07f + 0.05f * pos));
            g.strokePath (v, PathStrokeType (2.0f + (float) i * 3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        g.setColour (Colour (0xffb98cff));
        g.strokePath (v, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));

        g.setColour (bone);
        const auto tip = pt (arcR, ang);
        g.fillEllipse (tip.x - 2.5f, tip.y - 2.5f, 5.0f, 5.0f);
    }

    // ---- body (obsidian disc)
    const float bodyR = r * 0.55f;
    ColourGradient body (Colour (0xff1a1230), c.translated (-bodyR * 0.35f, -bodyR * 0.45f),
                         Colour (0xff030208), c.translated (bodyR * 0.9f, bodyR * 0.6f), true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    g.setColour (gold.withAlpha (0.6f));
    g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.2f);

    // ---- gothic texture: lancet arches + quatrefoil, turns with the knob
    {
        Path tex (texture);
        tex.applyTransform (AffineTransform::scale (bodyR).rotated (ang).translated (c.x, c.y));
        g.setColour (gold.withAlpha (0.42f));
        g.strokePath (tex, PathStrokeType (0.9f));
        g.setColour (gold.withAlpha (0.16f));
        g.drawEllipse (c.x - bodyR * 0.58f, c.y - bodyR * 0.58f, bodyR * 1.16f, bodyR * 1.16f, 0.7f);
    }

    // ---- pointer
    g.setColour (bone);
    g.drawLine (Line<float> (pt (bodyR * 0.30f, ang), pt (bodyR * 0.92f, ang)), 2.2f);
}

// Only the RUIN pill uses a toggle button
void RitualLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& bt, bool hl, bool /*down*/)
{
    using namespace Ritual;

    const bool on = bt.getToggleState();
    auto pill = bt.getLocalBounds().toFloat().reduced (2.0f);
    const float rad = pill.getHeight() * 0.5f;

    if (on)
    {
        for (int i = 3; i > 0; --i)
        {
            g.setColour (ember.withAlpha (0.10f));
            g.drawRoundedRectangle (pill.expanded ((float) i * 2.0f), rad + (float) i * 2.0f, 2.0f);
        }
        g.setGradientFill (ColourGradient (Colour (0xff7a35d9), pill.getX(), pill.getY(),
                                           blood.darker (0.4f), pill.getX(), pill.getBottom(), false));
        g.fillRoundedRectangle (pill, rad);
    }
    else
    {
        g.setColour (ink.withAlpha (0.75f));
        g.fillRoundedRectangle (pill, rad);
    }

    g.setColour (on ? ember : gold.withAlpha (hl ? 0.9f : 0.6f));
    g.drawRoundedRectangle (pill, rad, 1.2f);
    drawTracked (g, bt.getButtonText(), pill, font (12.0f), 4.0f, on ? bone : dim);
}

//==============================================================================
// Editor
//==============================================================================
SaturatorEditor::SaturatorEditor (SaturatorProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    setSize (kW, kH);
    setResizable (false, false);

    //                                               title         what it does
    setupKnob (drive,      "drive",      "DRIVE",      "Saturation amount",  true);
    setupKnob (input,      "input",      "INPUT",      "Level into plugin",  false);
    setupKnob (thump,      "thump",      "THUMP",      "Low-end weight",     false);
    setupKnob (tone,       "tone",       "TONE",       "Dark to bright",     false);
    setupKnob (lowCut,     "lowcut",     "LOW CUT",    "Removes the bass",   false);
    setupKnob (highCut,    "highcut",    "HIGH CUT",   "Removes the treble", false);
    setupKnob (output,     "output",     "OUTPUT",     "Output volume",      false);
    setupKnob (mix,        "mix",        "MIX",        "Dry / wet blend",    false);
    setupKnob (collapse,   "collapse",   "COLLAPSE",   "Crush and fold",     false);
    setupKnob (fracture,   "fracture",   "FRACTURE",   "Harmonic grit",      false);
    setupKnob (aftershock, "aftershock", "AFTERSHOCK", "Dark echoing tails", false);

    // Display formatting (set after attachments, which install their own text functions)
    auto num = [] (const String& t) { return t.retainCharacters ("0123456789.-").getDoubleValue(); };
    for (auto* k : knobs)
        k->slider.valueFromTextFunction = [num] (const String& t)
            { return t.containsIgnoreCase ("k") ? num (t) * 1000.0 : num (t); };

    auto pct = [] (double v) { return String (roundToInt (v)) + " %"; };
    auto hz  = [] (double v) { return v >= 1000.0 ? String (v / 1000.0, 1) + " kHz" : String (roundToInt (v)) + " Hz"; };
    auto db1 = [] (double v) { return String (v, 1) + " dB"; };

    drive.slider.textFromValueFunction      = [] (double v) { return String (v, 1); };
    input.slider.textFromValueFunction      = db1;
    thump.slider.textFromValueFunction      = [] (double v) { return String (v, 1); };
    tone.slider.textFromValueFunction       = [] (double v) { return String (v > 0.005 ? "+" : "") + String (v, 2); };
    lowCut.slider.textFromValueFunction     = hz;
    highCut.slider.textFromValueFunction    = hz;
    output.slider.textFromValueFunction     = db1;
    mix.slider.textFromValueFunction        = pct;
    collapse.slider.textFromValueFunction   = pct;
    fracture.slider.textFromValueFunction   = pct;
    aftershock.slider.textFromValueFunction = pct;
    for (auto* k : knobs) k->slider.updateText();

    // ---- RUIN (was PUNISH; parameter id unchanged so old projects keep working)
    ruinBtn.setButtonText ("RUIN");
    ruinBtn.setMouseCursor (MouseCursor::PointingHandCursor);
    addAndMakeVisible (ruinBtn);
    ruinAtt = std::make_unique<ButtonAtt> (proc.apvts, "punish", ruinBtn);

    // ---- sparks
    Random rng (42);
    for (auto& s : sparks)
        s = { rng.nextFloat() * (float) kW, rng.nextFloat() * (float) kH, 0.25f + rng.nextFloat() * 0.6f,
              rng.nextFloat() * 6.28f, 1.2f + rng.nextFloat() * 2.2f, 0.4f + rng.nextFloat() * 0.6f };

    buildCracks();
    startTimerHz (30);
}

SaturatorEditor::~SaturatorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SaturatorEditor::setupKnob (KnobUI& k, const String& id, const String& title,
                                 const String& caption, bool big)
{
    k.title = title;
    k.caption = caption;
    k.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, big ? 110 : 90, big ? 24 : 18);
    k.slider.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    k.slider.getProperties().set ("big", big);
    addAndMakeVisible (k.slider);
    k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.slider);
}

void SaturatorEditor::resized()
{
    auto place = [] (KnobUI& k, int centreX, int top)
    {
        k.slider.setBounds (centreX - 50, top, 100, 120);
        k.titleRect   = { centreX - 65, top - 20, 130, 16 };
        k.captionRect = { centreX - 65, top + 121, 130, 13 };
    };

    // mirrored left / right: Low Cut <-> High Cut, Thump <-> Tone, Input <-> Output
    place (lowCut,  95, 130);   place (highCut, 705, 130);
    place (thump,  225, 210);   place (tone,    575, 210);
    place (input,   95, 290);   place (output,  705, 290);

    drive.slider.setBounds (270, 115, 260, 264);
    drive.titleRect   = { 330, 92, 140, 20 };
    drive.captionRect = { 330, 375, 140, 13 };

    ruinBtn.setBounds (340, 392, 120, 26);

    // lower row: the ABYSS engine + Mix
    place (collapse,   175, 500);
    place (fracture,   325, 500);
    place (aftershock, 475, 500);
    place (mix,        625, 500);

    renderBackdrop();
}

//------------------------------------------------------------------------------
// Glass cracks: jagged radial fractures with branches plus broken concentric rings.
// Points are offsets from the eclipse centre; they appear as the crack front travels outwards.
//------------------------------------------------------------------------------
void SaturatorEditor::buildCracks()
{
    Random rng (1337);
    cracks.clear();
    const float twoPi = MathConstants<float>::twoPi;

    const int N = 17;
    for (int i = 0; i < N; ++i)
    {
        float ang = (float) i / (float) N * twoPi + (rng.nextFloat() - 0.5f) * 0.25f;
        const float reach = 120.0f + rng.nextFloat() * 95.0f;
        const int steps = 9 + rng.nextInt (4);

        Crack main;
        main.pts.push_back ({ 0.0f, 0.0f });
        float r = 0.0f;
        for (int s = 1; s <= steps; ++s)
        {
            r += reach / (float) steps * (0.7f + rng.nextFloat() * 0.6f);
            ang += (rng.nextFloat() - 0.5f) * 0.16f;
            main.pts.push_back ({ r * std::sin (ang), -r * std::cos (ang) });
        }

        for (int b = 0; b < 2; ++b)          // short side branches
        {
            const int at = 2 + rng.nextInt ((int) main.pts.size() - 4);
            const auto origin = main.pts[(size_t) at];
            float bang = std::atan2 (origin.x, -origin.y) + (rng.nextBool() ? 1.0f : -1.0f) * (0.5f + rng.nextFloat() * 0.45f);
            const float blen = 22.0f + rng.nextFloat() * 36.0f;

            Crack br;
            br.pts.push_back (origin);
            Point<float> cur = origin;
            for (int s = 0; s < 3; ++s)
            {
                bang += (rng.nextFloat() - 0.5f) * 0.3f;
                cur += Point<float> (std::sin (bang), -std::cos (bang)) * (blen / 3.0f);
                br.pts.push_back (cur);
            }
            cracks.push_back (std::move (br));
        }
        cracks.push_back (std::move (main));
    }

    for (float rad : { 34.0f, 58.0f, 86.0f, 118.0f })       // broken rings
    {
        float a = rng.nextFloat() * twoPi;
        const float end = a + twoPi;
        while (a < end)
        {
            const float len = 0.5f + rng.nextFloat() * 0.9f;
            Crack arc;
            for (float t = 0.0f; t <= len; t += 0.12f)
            {
                const float rr = rad + (rng.nextFloat() - 0.5f) * 3.0f;
                arc.pts.push_back ({ rr * std::sin (a + t), -rr * std::cos (a + t) });
            }
            if (arc.pts.size() > 1)
                cracks.push_back (std::move (arc));
            a += len + 0.15f + rng.nextFloat() * 0.45f;
        }
    }
}

void SaturatorEditor::spawnShards()
{
    Random rng;
    const float twoPi = MathConstants<float>::twoPi;
    for (auto& s : shards)
    {
        const float a = rng.nextFloat() * twoPi, d = 10.0f + rng.nextFloat() * 90.0f;
        const Point<float> dir (std::sin (a), -std::cos (a));
        const float size = 5.0f + rng.nextFloat() * 10.0f;

        s.pos = Point<float> (kCX, kCY) + dir * d;
        s.vel = dir * (80.0f + rng.nextFloat() * 280.0f) + Point<float> (0.0f, -40.0f);
        for (auto& p : s.tri)
        {
            const float pa = rng.nextFloat() * twoPi, pr = size * (0.5f + rng.nextFloat() * 0.6f);
            p = { pr * std::sin (pa), -pr * std::cos (pa) };
        }
        s.rot  = rng.nextFloat() * twoPi;
        s.vrot = (rng.nextFloat() - 0.5f) * 10.0f;
        s.life = 0.6f + rng.nextFloat() * 0.4f;
    }
}

//------------------------------------------------------------------------------
// Static layer, rendered once at 2x: background image, ritual circles, plates, frame, text
//------------------------------------------------------------------------------
void SaturatorEditor::renderBackdrop()
{
    using namespace Ritual;

    const float S = 2.0f;
    const int W = getWidth(), H = getHeight();
    if (W <= 0 || H <= 0) return;

    backdrop = Image (Image::ARGB, (int) ((float) W * S), (int) ((float) H * S), true);
    Graphics g (backdrop);
    g.addTransform (AffineTransform::scale (S));

    const Point<float> c (kCX, kCY);

    // ---- background: the supplied image ("cover" crop), then darkened and tinted violet for legibility
    g.setGradientFill (ColourGradient (Colour (0xff0d0818), 0.0f, 0.0f, Colour (0xff030208), 0.0f, (float) H, false));
    g.fillAll();

    const Image bg = backgroundImage();
    if (bg.isValid())
    {
        const float sc = jmax ((float) W / (float) bg.getWidth(), (float) H / (float) bg.getHeight());
        const int sw = (int) ((float) W / sc), sh = (int) ((float) H / sc);
        const int sx = (bg.getWidth() - sw) / 2, sy = (bg.getHeight() - sh) / 2;
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (bg, 0, 0, W, H, sx, sy, sw, sh);

        g.setColour (Colour (0xff060410).withAlpha (0.52f));     // darken (raise/lower to taste)
        g.fillAll();
        g.setColour (Colour (0xff3a1a78).withAlpha (0.26f));      // violet tint
        g.fillAll();
    }

    // faint violet haze behind the circle
    {
        ColourGradient hz (blood.withAlpha (0.30f), c, blood.withAlpha (0.0f), c.translated (330.0f, 0.0f), true);
        g.setGradientFill (hz);
        g.fillAll();
    }

    // ritual circles
    const float radii[] = { 150.0f, 170.0f, 192.0f };
    const float alphas[] = { 0.16f, 0.26f, 0.16f };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (gold.withAlpha (alphas[i]));
        g.drawEllipse (c.x - radii[i], c.y - radii[i], radii[i] * 2.0f, radii[i] * 2.0f, 1.0f);
    }
    for (int i = 0; i < 96; ++i)
    {
        const float a = (float) i / 96.0f * MathConstants<float>::twoPi;
        const float len = (i % 8 == 0) ? 9.0f : (i % 4 == 0 ? 6.0f : 3.5f);
        g.setColour (gold.withAlpha (i % 8 == 0 ? 0.40f : 0.22f));
        g.drawLine (c.x + 192.0f * std::sin (a), c.y - 192.0f * std::cos (a),
                    c.x + (192.0f + len) * std::sin (a), c.y - (192.0f + len) * std::cos (a), 1.0f);
    }

    auto diamond = [&] (Point<float> p, float s, Colour fill, Colour line)
    {
        Path d;
        d.addQuadrilateral (p.x, p.y - s, p.x + s, p.y, p.x, p.y + s, p.x - s, p.y);
        g.setColour (fill);  g.fillPath (d);
        g.setColour (line);  g.strokePath (d, PathStrokeType (1.0f));
    };
    for (int i = 0; i < 4; ++i)
    {
        const float a = (float) i * MathConstants<float>::halfPi;
        diamond ({ c.x + 170.0f * std::sin (a), c.y - 170.0f * std::cos (a) }, 4.0f, ink, gold.withAlpha (0.6f));
    }

    // soft vignette
    {
        ColourGradient vg (Colours::transparentBlack, c, Colours::black.withAlpha (0.70f),
                           c.translated (620.0f, 0.0f), true);
        vg.addColour (0.5, Colours::transparentBlack);
        g.setGradientFill (vg);
        g.fillAll();
    }

    // dark plates behind every knob so the labels stay readable on the image
    for (auto* k : knobs)
    {
        const bool big = (k == &drive);
        const Point<float> kc ((float) k->slider.getBounds().getCentreX(), (float) k->slider.getY() + (big ? 120.0f : 60.0f));
        const float rad = big ? 175.0f : 92.0f;
        ColourGradient plate (Colour (0xff04020a).withAlpha (big ? 0.40f : 0.66f), kc,
                              Colour (0xff04020a).withAlpha (0.0f), kc.translated (rad, 0.0f), true);
        plate.addColour (0.55, Colour (0xff04020a).withAlpha (big ? 0.34f : 0.52f));
        g.setGradientFill (plate);
        g.fillEllipse (kc.x - rad, kc.y - rad, rad * 2.0f, rad * 2.0f);
    }

    // divider around the RUIN pill
    g.setColour (gold.withAlpha (0.40f));
    g.drawLine (165.0f, 405.0f, 328.0f, 405.0f, 1.0f);
    g.drawLine (472.0f, 405.0f, 635.0f, 405.0f, 1.0f);
    diamond ({ 165.0f, 405.0f }, 3.0f, ink, gold.withAlpha (0.8f));
    diamond ({ 635.0f, 405.0f }, 3.0f, ink, gold.withAlpha (0.8f));

    // ABYSS header over the engine knobs
    {
        const float hx = 325.0f;
        drawTracked (g, "ABYSS", { hx - 90.0f, 441.0f, 180.0f, 20.0f }, font (15.0f), 10.0f, bone);
        drawTracked (g, "NONLINEAR HARMONIC COLLAPSE", { hx - 150.0f, 461.0f, 300.0f, 14.0f }, font (8.5f), 2.5f, dim);
        g.setColour (gold.withAlpha (0.45f));
        g.drawLine (105.0f, 451.0f, hx - 58.0f, 451.0f, 1.0f);
        g.drawLine (hx + 58.0f, 451.0f, 545.0f, 451.0f, 1.0f);
        diamond ({ 105.0f, 451.0f }, 3.0f, ink, gold.withAlpha (0.8f));
        diamond ({ 545.0f, 451.0f }, 3.0f, ink, gold.withAlpha (0.8f));
    }

    // frame
    g.setColour (gold.withAlpha (0.65f));
    g.drawRect (Rectangle<float> (8.0f, 8.0f, (float) W - 16.0f, (float) H - 16.0f), 1.0f);
    g.setColour (gold.withAlpha (0.28f));
    g.drawRect (Rectangle<float> (14.0f, 14.0f, (float) W - 28.0f, (float) H - 28.0f), 1.0f);
    diamond ({ (float) W * 0.5f, 8.0f },             5.0f, ink, gold.withAlpha (0.9f));
    diamond ({ (float) W * 0.5f, (float) H - 8.0f }, 5.0f, ink, gold.withAlpha (0.9f));
    for (auto p : { Point<float> (8, 8), Point<float> ((float) W - 8, 8),
                    Point<float> (8, (float) H - 8), Point<float> ((float) W - 8, (float) H - 8) })
        diamond (p, 4.0f, ink, gold.withAlpha (0.9f));

    // title: the only text that is not Cinzel
    drawTracked (g, "ONYX", { 0.0f, 28.0f, (float) W, 44.0f }, titleFont (36.0f), 20.0f, bone);
    g.setColour (gold.withAlpha (0.55f));
    g.drawLine (350.0f, 80.0f, 388.0f, 80.0f, 1.0f);
    g.drawLine (412.0f, 80.0f, 450.0f, 80.0f, 1.0f);
    diamond ({ 400.0f, 80.0f }, 3.5f, ink, gold.withAlpha (0.85f));

    // knob titles and "what it does" captions
    for (auto* k : knobs)
    {
        const bool big = (k == &drive);
        drawTracked (g, k->title, k->titleRect.toFloat(), font (big ? 14.0f : 11.5f), big ? 4.0f : 2.0f, bone);
        drawTracked (g, k->caption, k->captionRect.toFloat(), font (big ? 10.5f : 10.0f), 0.8f, dim);
    }

    // grain
    Random rng (7);
    for (int i = 0; i < 3000; ++i)
    {
        g.setColour (Colours::white.withAlpha (rng.nextFloat() * 0.03f));
        g.fillRect (rng.nextFloat() * (float) W, rng.nextFloat() * (float) H, 1.0f, 1.0f);
    }
}

//------------------------------------------------------------------------------
// Dynamic layer: glow tied to Drive and live level, rising sparks
//------------------------------------------------------------------------------
void SaturatorEditor::paint (Graphics& g)
{
    using namespace Ritual;

    if (backdrop.isValid())
    {
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (backdrop, getLocalBounds().toFloat());
    }

    const float d = (float) (drive.slider.getValue() / 10.0);
    const float pulse = 0.5f + 0.5f * std::sin (phase);
    const bool ruin = ruinBtn.getToggleState();
    const Point<float> c (kCX, kCY);

    const float a = 0.03f + 0.18f * d * (0.85f + 0.15f * pulse) + 0.18f * level
                  + (ruin ? 0.07f * pulse : 0.0f);
    ColourGradient glow (ember.withAlpha (jmin (a, 0.6f)), c, blood.withAlpha (0.0f), c.translated (290.0f, 0.0f), true);
    g.setGradientFill (glow);
    g.fillRect (getLocalBounds());

    const float intensity = 0.15f + 0.45f * d + 0.35f * level + (ruin ? 0.2f : 0.0f);
    for (auto& s : sparks)
    {
        const float fade = jlimit (0.0f, 1.0f, (s.y - 40.0f) / 220.0f);
        g.setColour (ember.withAlpha (jlimit (0.0f, 1.0f, fade * intensity * s.bright)));
        g.fillEllipse (s.x, s.y, s.size, s.size);
    }
}

// Glass shattering over the main knob. Only draws while something is happening.
void SaturatorEditor::paintOverChildren (Graphics& g)
{
    using namespace Ritual;
    const Point<float> c (kCX, kCY);

    if (flash > 0.01f)                                      // impact flash
    {
        ColourGradient fg (Colour (0xfff2eaff).withAlpha (flash * 0.65f), c,
                           Colour (0xffa56bff).withAlpha (0.0f), c.translated (210.0f, 0.0f), true);
        g.setGradientFill (fg);
        g.fillEllipse (c.x - 210.0f, c.y - 210.0f, 420.0f, 420.0f);
    }

    if (crackProgress > 0.01f)                              // cracks grow outwards from the centre
    {
        const float front = crackProgress * 235.0f;
        const float alpha = jlimit (0.0f, 1.0f, crackProgress * 1.6f) * (0.85f + 0.15f * std::sin (phase * 3.0f));

        auto strokeAll = [&] (float width, Colour col)
        {
            g.setColour (col);
            for (auto& cr : cracks)
                for (size_t j = 1; j < cr.pts.size(); ++j)
                {
                    const auto p0 = cr.pts[j - 1];
                    auto p1 = cr.pts[j];
                    const float r0 = p0.getDistanceFromOrigin(), r1 = p1.getDistanceFromOrigin();
                    if (r0 >= front) continue;
                    if (r1 > front && r1 > r0)
                        p1 = p0 + (p1 - p0) * ((front - r0) / (r1 - r0));
                    g.drawLine (c.x + p0.x, c.y + p0.y, c.x + p1.x, c.y + p1.y, width);
                }
        };
        strokeAll (3.4f, ember.withAlpha (0.22f * alpha));      // glow
        strokeAll (1.1f, bone.withAlpha (0.92f * alpha));       // sharp edge
    }

    for (auto& s : shards)                                  // flying glass fragments
    {
        if (s.life <= 0.0f) continue;
        Path p;
        p.startNewSubPath (s.tri[0]);
        p.lineTo (s.tri[1]);
        p.lineTo (s.tri[2]);
        p.closeSubPath();
        p.applyTransform (AffineTransform::rotation (s.rot).translated (s.pos.x, s.pos.y));
        g.setColour (Colour (0xffe8dcff).withAlpha (jlimit (0.0f, 1.0f, s.life) * 0.55f));
        g.fillPath (p);
        g.setColour (ember.withAlpha (jlimit (0.0f, 1.0f, s.life) * 0.9f));
        g.strokePath (p, PathStrokeType (0.8f));
    }
}

void SaturatorEditor::timerCallback()
{
    phase += 0.07f;
    const float dt = 1.0f / 30.0f;

    // living eclipse: follow the real signal level (fast attack, slow release)
    const float raw = proc.driveLevel.load();
    const float target = 1.0f - std::exp (-raw * 0.8f);
    level += (target - level) * (target > level ? 0.6f : 0.12f);
    drive.slider.getProperties().set ("level", level);

    // glass breaks when Drive reaches its maximum, heals slowly when it is turned back
    const bool nowMax = drive.slider.getValue() >= drive.slider.getMaximum() - 0.02;
    if (nowMax && ! atMax)
    {
        spawnShards();
        flash = 1.0f;
    }
    atMax = nowMax;
    crackProgress = jlimit (0.0f, 1.0f, crackProgress + (nowMax ? 0.12f : -0.035f));
    flash *= 0.86f;

    for (auto& s : shards)
        if (s.life > 0.0f)
        {
            s.pos += s.vel * dt;
            s.vel.y += 520.0f * dt;
            s.vel *= 0.985f;
            s.rot += s.vrot * dt;
            s.life -= dt / 1.3f;
        }

    static Random rng;
    const float speed = 1.0f + level + (ruinBtn.getToggleState() ? 0.8f : 0.0f);
    for (auto& s : sparks)
    {
        s.y -= s.vy * speed;
        s.phase += 0.03f;
        s.x += std::sin (s.phase) * 0.25f;
        if (s.y < 30.0f)
        {
            s.y = (float) getHeight() + rng.nextFloat() * 40.0f;
            s.x = 20.0f + rng.nextFloat() * ((float) getWidth() - 40.0f);
        }
    }
    repaint();
}
