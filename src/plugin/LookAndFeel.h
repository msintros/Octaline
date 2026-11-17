#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace octaline
{

namespace colours
{
inline const juce::Colour background { 0xff15171c };
inline const juce::Colour panel      { 0xff1e2128 };
inline const juce::Colour panelEdge  { 0xff2c3039 };
inline const juce::Colour text       { 0xffe6e8ec };
inline const juce::Colour textDim    { 0xff8b919c };
inline const juce::Colour track      { 0xff353a45 };

// One accent per band. Grouped bands take the colour of their group's head,
// so a group reads as one block of colour.
inline juce::Colour band (int index0)
{
    static const juce::Colour c[] = {
        juce::Colour (0xff4fc3c9), juce::Colour (0xff7aa7ff), juce::Colour (0xffb58cff), juce::Colour (0xffff8cc6),
        juce::Colour (0xffff9a6b), juce::Colour (0xffffd166), juce::Colour (0xff9be08b), juce::Colour (0xff5fd6a4),
    };
    return c[juce::jlimit (0, 7, index0)];
}
} // namespace colours

class OctalineLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OctalineLookAndFeel()
    {
        using namespace juce;
        setColour (ResizableWindow::backgroundColourId, colours::background);
        setColour (Label::textColourId, colours::text);
        setColour (Slider::textBoxTextColourId, colours::text);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour (Slider::textBoxHighlightColourId, colours::band (0).withAlpha (0.4f));
        setColour (Slider::rotarySliderOutlineColourId, colours::track);
        setColour (Slider::rotarySliderFillColourId, colours::band (0));
        setColour (ComboBox::backgroundColourId, colours::background);
        setColour (ComboBox::outlineColourId, colours::panelEdge);
        setColour (ComboBox::textColourId, colours::text);
        setColour (ComboBox::arrowColourId, colours::textDim);
        setColour (PopupMenu::backgroundColourId, colours::panel);
        setColour (PopupMenu::textColourId, colours::text);
        setColour (PopupMenu::highlightedBackgroundColourId, colours::band (0).withAlpha (0.35f));
        setColour (PopupMenu::highlightedTextColourId, colours::text);
        setColour (TextButton::buttonColourId, colours::panelEdge);
        setColour (TextButton::buttonOnColourId, colours::band (0));
        setColour (TextButton::textColourOffId, colours::text);
        setColour (TextButton::textColourOnId, colours::background);
        setColour (TooltipWindow::backgroundColourId, colours::panel);
        setColour (TooltipWindow::textColourId, colours::text);
        setColour (TooltipWindow::outlineColourId, colours::panelEdge);
        setColour (AlertWindow::backgroundColourId, colours::panel);
        setColour (AlertWindow::textColourId, colours::text);
        setColour (AlertWindow::outlineColourId, colours::panelEdge);
        setColour (TextEditor::backgroundColourId, colours::background);
        setColour (TextEditor::textColourId, colours::text);
        setColour (TextEditor::outlineColourId, colours::panelEdge);
        setColour (CaretComponent::caretColourId, colours::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        using namespace juce;
        const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float lineW = jmax (2.5f, radius * 0.16f);
        const float arcR = radius - lineW * 0.5f;
        const float alpha = s.isEnabled() ? 1.0f : 0.3f;

        Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (s.findColour (Slider::rotarySliderOutlineColourId).withMultipliedAlpha (alpha));
        g.strokePath (track, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

        // Bipolar controls (pan) fill from the centre.
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        const float to = startAngle + pos * (endAngle - startAngle);
        if (std::abs (to - from) > 0.001f)
        {
            Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, jmin (from, to), jmax (from, to), true);
            g.setColour (s.findColour (Slider::rotarySliderFillColourId).withMultipliedAlpha (alpha));
            g.strokePath (value, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));
        }

        const float knobR = arcR - lineW * 1.2f;
        g.setColour (colours::panelEdge.brighter (0.1f).withMultipliedAlpha (alpha));
        g.fillEllipse (Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre));
        Path pointer;
        pointer.addRoundedRectangle (-1.25f, -knobR, 2.5f, knobR * 0.55f, 1.0f);
        pointer.applyTransform (AffineTransform::rotation (to).translated (centre));
        g.setColour (colours::text.withMultipliedAlpha (alpha));
        g.fillPath (pointer);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (12.0f); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return juce::FontOptions (juce::jmin (12.5f, (float) h * 0.6f)); }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (juce::FontOptions (11.5f));
        return l;
    }
};

} // namespace octaline
