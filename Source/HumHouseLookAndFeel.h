#pragma once

#include <JuceHeader.h>

namespace humtune
{

// HumHouse purple / dark theme inspired by Slate Digital MetaTune.
// All colours, gradients, and corner radii live here so the editor
// only references semantic names.
struct Palette
{
    // Backgrounds
    static inline const juce::Colour bgDark        { 0xff1a1a24 };  // near-black
    static inline const juce::Colour bgPanel        { 0xff222233 };
    static inline const juce::Colour bgSection      { 0xff2a2a3e };

    // Purple accents
    static inline const juce::Colour purplePrimary   { 0xff8855cc };
    static inline const juce::Colour purpleBright    { 0xffaa77ee };
    static inline const juce::Colour purpleGlow      { 0x608855cc };
    static inline const juce::Colour purpleDim       { 0xff5533aa };

    // Piano keys
    static inline const juce::Colour keyWhite        { 0xff6644aa };
    static inline const juce::Colour keyBlack        { 0xff2a2244 };
    static inline const juce::Colour keyActive       { 0xffcc88ff };
    static inline const juce::Colour keyDisabled     { 0xff333344 };

    // Text
    static inline const juce::Colour textBright      { 0xfff0e8ff };
    static inline const juce::Colour textDim         { 0xff9988bb };
    static inline const juce::Colour textValue       { 0xffddccff };

    // Meters / heatmap
    static inline const juce::Colour meterGreen      { 0xff44cc88 };
    static inline const juce::Colour meterYellow     { 0xffcccc44 };
    static inline const juce::Colour meterRed        { 0xffcc4444 };
    static inline const juce::Colour heatCold        { 0xff3344aa };
    static inline const juce::Colour heatWarm        { 0xffcc44cc };

    // Knob
    static inline const juce::Colour knobTrack       { 0xff333355 };
    static inline const juce::Colour knobFill        { 0xffaa77ee };
    static inline const juce::Colour knobThumb       { 0xfff0e8ff };
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
        setColour(juce::ToggleButton::tickColourId,          Palette::purplePrimary);
        setColour(juce::TextButton::buttonColourId,          Palette::bgSection);
        setColour(juce::TextButton::textColourOnId,          Palette::textBright);
        setColour(juce::TextButton::textColourOffId,         Palette::textDim);
        setColour(juce::ComboBox::backgroundColourId,        Palette::bgSection);
        setColour(juce::ComboBox::textColourId,              Palette::textBright);
        setColour(juce::ComboBox::outlineColourId,           Palette::purpleDim);
        setColour(juce::PopupMenu::backgroundColourId,       Palette::bgPanel);
        setColour(juce::PopupMenu::textColourId,             Palette::textBright);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Palette::purpleDim);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                              static_cast<float>(width), static_cast<float>(height));
        auto radius  = std::min(bounds.getWidth(), bounds.getHeight()) * 0.4f;
        auto centreX = bounds.getCentreX();
        auto centreY = bounds.getCentreY();
        auto arcRadius = radius - 4.0f;

        float lineWidth = 3.5f;

        // Track arc (background)
        juce::Path track;
        track.addCentredArc(centreX, centreY, arcRadius, arcRadius,
                            0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(Palette::knobTrack);
        g.strokePath(track, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // Value arc
        float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        juce::Path value;
        value.addCentredArc(centreX, centreY, arcRadius, arcRadius,
                            0.0f, rotaryStartAngle, angle, true);

        juce::ColourGradient grad(Palette::purpleDim, centreX, centreY + arcRadius,
                                   Palette::purpleBright, centreX, centreY - arcRadius, false);
        g.setGradientFill(grad);
        g.strokePath(value, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // Thumb dot
        float thumbAngle = angle;
        float thumbX = centreX + arcRadius * std::cos(thumbAngle - juce::MathConstants<float>::halfPi);
        float thumbY = centreY + arcRadius * std::sin(thumbAngle - juce::MathConstants<float>::halfPi);
        g.setColour(Palette::knobThumb);
        g.fillEllipse(thumbX - 4.0f, thumbY - 4.0f, 8.0f, 8.0f);

        // Center circle (dark)
        g.setColour(Palette::bgDark);
        g.fillEllipse(centreX - radius * 0.55f, centreY - radius * 0.55f,
                       radius * 1.1f, radius * 1.1f);

        // Value text inside knob
        g.setColour(Palette::textValue);
        g.setFont(juce::Font(juce::FontOptions(radius * 0.45f)));

        juce::String text;
        if (slider.getTextValueSuffix().isNotEmpty())
            text = slider.getTextFromValue(slider.getValue());
        else
            text = juce::String(slider.getValue(), 1);

        g.drawText(text,
                   static_cast<int>(centreX - radius * 0.6f),
                   static_cast<int>(centreY - radius * 0.3f),
                   static_cast<int>(radius * 1.2f),
                   static_cast<int>(radius * 0.6f),
                   juce::Justification::centred, false);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);

        // Background pill
        auto bgColour = button.getToggleState() ? Palette::purplePrimary : Palette::bgSection;
        if (shouldDrawButtonAsHighlighted)
            bgColour = bgColour.brighter(0.1f);

        g.setColour(bgColour);
        g.fillRoundedRectangle(bounds, 4.0f);

        // Border
        g.setColour(Palette::purpleDim);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

        // Text
        g.setColour(button.getToggleState() ? Palette::textBright : Palette::textDim);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(button.getButtonText(), bounds, juce::Justification::centred, false);

        juce::ignoreUnused(shouldDrawButtonAsDown);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        auto baseColour = button.getToggleState() ? Palette::purplePrimary : Palette::bgSection;
        if (shouldDrawButtonAsDown)
            baseColour = baseColour.darker(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.1f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(Palette::purpleDim);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }
};

} // namespace humtune
