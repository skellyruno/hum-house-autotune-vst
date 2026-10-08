#pragma once

#include <JuceHeader.h>

namespace humtune
{

// HumHouse neon-green / dark "skeleton tech" theme.
// All colours live here, so the editor only uses semantic names.
// To recolour the whole plugin, change the accent* lines below.
struct Palette
{
    // Backgrounds
    static inline const juce::Colour bgDark        { 0xff050a07 };
    static inline const juce::Colour bgDeep        { 0xff020504 };
    static inline const juce::Colour bgPanel       { 0xff0a1610 };
    static inline const juce::Colour bgSection     { 0xff10201a };

    // Green accents
    static inline const juce::Colour accent        { 0xff22dd66 };
    static inline const juce::Colour accentBright  { 0xff66ff99 };
    static inline const juce::Colour accentGlow    { 0x6066ff99 };
    static inline const juce::Colour accentDim     { 0xff117a38 };
    static inline const juce::Colour accentDeep    { 0xff0d3a1e };

    // Piano keys
    static inline const juce::Colour keyWhiteTop     { 0xff17301f };
    static inline const juce::Colour keyWhiteBottom  { 0xff6fe39a };
    static inline const juce::Colour keyBlackTop     { 0xff050a07 };
    static inline const juce::Colour keyBlackBottom  { 0xff14502e };
    static inline const juce::Colour keyOffTop       { 0xff1a211c };
    static inline const juce::Colour keyOffBottom    { 0xff2c352f };

    // Text
    static inline const juce::Colour textBright    { 0xffe8fff0 };
    static inline const juce::Colour textDim       { 0xff7fae92 };
    static inline const juce::Colour textValue     { 0xffc8ffdc };

    // Meters / heatmap
    static inline const juce::Colour meterGreen    { 0xff44cc88 };
    static inline const juce::Colour meterYellow   { 0xffcccc44 };
    static inline const juce::Colour meterRed      { 0xffcc4444 };
    static inline const juce::Colour heatCold      { 0xff33dddd };
    static inline const juce::Colour heatWarm      { 0xff66ff66 };

    // Knob
    static inline const juce::Colour knobTrack     { 0xff0d3a1e };
    static inline const juce::Colour knobFill      { 0xff66ff99 };
    static inline const juce::Colour knobThumb     { 0xffe8fff0 };
};

class HumHouseLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HumHouseLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, Palette::bgDark);
        setColour(juce::Slider::rotarySliderFillColourId,    Palette::knobFill);
        setColour(juce::Slider::rotarySliderOutlineColourId, Palette::knobTrack);
        setColour(juce::Slider::thumbColourId,               Palette::knobThumb);
        setColour(juce::Label::textColourId,                 Palette::textBright);
        setColour(juce::ToggleButton::textColourId,          Palette::textBright);
        setColour(juce::ToggleButton::tickColourId,          Palette::accent);
        setColour(juce::TextButton::buttonColourId,          Palette::bgSection);
        setColour(juce::TextButton::textColourOnId,          Palette::textBright);
        setColour(juce::TextButton::textColourOffId,         Palette::textDim);
        setColour(juce::ComboBox::backgroundColourId,        Palette::bgSection);
        setColour(juce::ComboBox::textColourId,              Palette::textBright);
        setColour(juce::ComboBox::outlineColourId,           Palette::accentDim);
        setColour(juce::ComboBox::arrowColourId,             Palette::accent);
        setColour(juce::PopupMenu::backgroundColourId,       Palette::bgPanel);
        setColour(juce::PopupMenu::textColourId,             Palette::textBright);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Palette::accentDim);
    }

    // 0 = normal button, 1 = white piano key, 2 = black piano key
    static int pianoKeyType (juce::Component& c)
    {
        return static_cast<int>(c.getProperties().getWithDefault("pianoKey", 0));
    }

    // ------------------------------------------------------------------
    // Knobs: dark housing + ring of glowing segments + value in centre
    // ------------------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                              static_cast<float>(width), static_cast<float>(height));
        const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();

        // Housing
        juce::ColourGradient housing(Palette::bgSection, cx, cy - radius,
                                      Palette::bgDeep,    cx, cy + radius, false);
        g.setGradientFill(housing);
        g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(Palette::accentDim);
        g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.5f);

        // Segmented ring
        const int   numTicks  = 36;
        const float tickOuter = radius - 3.0f;
        const float tickInner = tickOuter - std::max(4.0f, radius * 0.14f);

        for (int i = 0; i <= numTicks; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(numTicks);
            const float a = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
            const float sx = std::sin(a);
            const float sy = -std::cos(a);

            const float x1 = cx + tickInner * sx, y1 = cy + tickInner * sy;
            const float x2 = cx + tickOuter * sx, y2 = cy + tickOuter * sy;

            if (t <= sliderPosProportional + 0.0001f)
            {
                g.setColour(Palette::accent.withAlpha(0.25f));
                g.drawLine(x1, y1, x2, y2, 4.5f);
                g.setColour(Palette::accentDim.interpolatedWith(Palette::accentBright, t));
                g.drawLine(x1, y1, x2, y2, 2.2f);
            }
            else
            {
                g.setColour(Palette::accentDeep);
                g.drawLine(x1, y1, x2, y2, 2.2f);
            }
        }

        // Centre disc
        const float inner = radius * 0.62f;
        juce::ColourGradient disc(Palette::bgPanel, cx, cy - inner,
                                   Palette::bgDeep,  cx, cy + inner, false);
        g.setGradientFill(disc);
        g.fillEllipse(cx - inner, cy - inner, inner * 2.0f, inner * 2.0f);
        g.setColour(Palette::accentDeep);
        g.drawEllipse(cx - inner, cy - inner, inner * 2.0f, inner * 2.0f, 1.0f);

        // Pointer
        const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        g.setColour(Palette::accentBright);
        g.drawLine(cx + inner * 0.80f * std::sin(angle), cy - inner * 0.80f * std::cos(angle),
                   cx + inner * 1.00f * std::sin(angle), cy - inner * 1.00f * std::cos(angle), 2.0f);

        // Value text
        g.setColour(Palette::textValue);
        g.setFont(juce::Font(juce::FontOptions(std::max(9.0f, radius * 0.30f))));

        // A knob can ask to be shown on a different scale, e.g. 0..1 shown as 0..100
        const double displayScale = static_cast<double>(slider.getProperties().getWithDefault("displayScale", 1.0));

        juce::String text;
        if (displayScale != 1.0)
            text = juce::String(juce::roundToInt(slider.getValue() * displayScale));
        else if (slider.getTextValueSuffix().isNotEmpty())
            text = slider.getTextFromValue(slider.getValue());
        else
            text = juce::String(slider.getValue(), 1);

        g.drawText(text,
                   static_cast<int>(cx - inner), static_cast<int>(cy - inner * 0.4f),
                   static_cast<int>(inner * 2.0f), static_cast<int>(inner * 0.8f),
                   juce::Justification::centred, false);
    }

    // ------------------------------------------------------------------
    // Toggle switches: pill with an LED
    // ------------------------------------------------------------------
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
        const bool on = button.getToggleState();
        const float cr = bounds.getHeight() * 0.5f;

        auto fill = on ? Palette::accentDeep : Palette::bgSection;
        if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter(0.15f);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, cr);

        if (on)
        {
            g.setColour(Palette::accent.withAlpha(0.25f));
            g.drawRoundedRectangle(bounds.expanded(1.5f), cr + 1.5f, 3.0f);
        }

        g.setColour(on ? Palette::accent : Palette::accentDim);
        g.drawRoundedRectangle(bounds, cr, 1.2f);

        // LED
        const float ledD = 8.0f;
        auto led = juce::Rectangle<float>(bounds.getX() + 10.0f, bounds.getCentreY() - ledD * 0.5f, ledD, ledD);
        if (on)
        {
            g.setColour(Palette::accent.withAlpha(0.35f));
            g.fillEllipse(led.expanded(3.0f));
        }
        g.setColour(on ? Palette::accentBright : Palette::accentDeep);
        g.fillEllipse(led);

        // Text
        g.setColour(on ? Palette::textBright : Palette::textDim);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText(button.getButtonText(), bounds.withTrimmedLeft(24.0f).toNearestInt(),
                   juce::Justification::centred, false);

        juce::ignoreUnused(shouldDrawButtonAsDown);
    }

    // ------------------------------------------------------------------
    // Text buttons (Major / Minor / Chromatic) and the piano keys
    // ------------------------------------------------------------------
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        const bool on = button.getToggleState();
        const int  pk = pianoKeyType(button);

        if (pk != 0)
        {
            auto b = button.getLocalBounds().toFloat().reduced(0.5f);

            juce::Colour top, bottom;
            if (pk == 1)
            {
                top    = on ? Palette::keyWhiteTop    : Palette::keyOffTop;
                bottom = on ? Palette::keyWhiteBottom : Palette::keyOffBottom;
            }
            else
            {
                top    = on ? Palette::keyBlackTop    : Palette::keyOffTop.darker(0.6f);
                bottom = on ? Palette::keyBlackBottom : Palette::keyOffBottom.darker(0.6f);
            }

            if (shouldDrawButtonAsDown)
            {
                top = top.brighter(0.15f);
                bottom = bottom.brighter(0.15f);
            }
            else if (shouldDrawButtonAsHighlighted)
            {
                bottom = bottom.brighter(0.1f);
            }

            juce::ColourGradient grad(top, 0.0f, b.getY(), bottom, 0.0f, b.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(b, 4.0f);

            g.setColour(on ? Palette::accent.withAlpha(0.8f) : Palette::accentDeep);
            g.drawRoundedRectangle(b, 4.0f, 1.0f);
            return;
        }

        auto b = button.getLocalBounds().toFloat().reduced(1.5f);
        const float cr = b.getHeight() * 0.5f;

        auto fill = on ? Palette::accentDeep : Palette::bgSection;
        if (shouldDrawButtonAsDown)
            fill = fill.darker(0.3f);
        else if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter(0.15f);

        g.setColour(fill);
        g.fillRoundedRectangle(b, cr);

        if (on)
        {
            g.setColour(Palette::accent.withAlpha(0.25f));
            g.drawRoundedRectangle(b.expanded(1.5f), cr + 1.5f, 3.0f);
        }

        g.setColour(on ? Palette::accent : Palette::accentDim);
        g.drawRoundedRectangle(b, cr, 1.2f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override
    {
        const int pk = pianoKeyType(button);

        if (pk != 0)
        {
            auto b = button.getLocalBounds();
            g.setColour(button.getToggleState() ? Palette::textBright : Palette::textDim);
            g.setFont(juce::Font(juce::FontOptions(pk == 1 ? 15.0f : 12.0f)));
            g.drawText(button.getButtonText(), b.removeFromTop(pk == 1 ? 34 : 26),
                       juce::Justification::centred, false);
            return;
        }

        LookAndFeel_V4::drawButtonText(g, button, shouldDrawButtonAsHighlighted,
                                       shouldDrawButtonAsDown);
    }
};

} // namespace humtune
