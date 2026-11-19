#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>

namespace
{
    const char* const kParamIds[4]  = { "octave", "pulse", "glide", "mix" };
    const char* const kNames[4]     = { "OCTAVA", "SIERRA<>PULSO", "GLIDE", "DRY/WET" };
    const double      kDefaults[4]  = { 0.0, 0.25, 20.0, 1.0 };

    const juce::Colour kLabelColour (0xffc8c8c8);
    const juce::Colour kValueColour (0xffffd25a);
}

//==============================================================================
// Solo dibuja el indicador: rectangulo blanco que gira (mismo tamano que el de la imagen)
void PedalLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto c = bounds.getCentre();
    const float s = bounds.getWidth() / (2.0f * pedal::knobHit);
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    juce::Path bar, tip;
    bar.addRectangle (-5.5f * s, -55.0f * s, 11.0f * s, 40.0f * s);
    tip.addRectangle (-5.5f * s, -18.0f * s, 11.0f * s,  3.0f * s);

    const auto tf = juce::AffineTransform::rotation (angle).translated (c.x, c.y);

    g.setColour (juce::Colours::black.withAlpha (0.35f));                  // sombra suave
    g.fillPath (bar, tf.translated (1.5f * s, 2.0f * s));
    g.setColour (juce::Colours::white);
    g.fillPath (bar, tf);
    g.setColour (juce::Colour (0xffc9c9c9));
    g.fillPath (tip, tf);
}

//==============================================================================
FunkVoxEditor::FunkVoxEditor (FunkVoxProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    background = juce::ImageCache::getFromMemory (BinaryData::pedal_bg_png, BinaryData::pedal_bg_pngSize);

    for (size_t i = 0; i < 4; ++i)
    {
        auto& s = sliders[i];
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,      // -135 grados
                               juce::MathConstants<float>::pi * 2.75f,      // +135 grados
                               true);
        s.setMouseDragSensitivity (180);
        s.setLookAndFeel (&lnf);
        s.onValueChange = [this] { repaintLabels(); };
        s.addMouseListener (this, false);
        addAndMakeVisible (s);

        attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, kParamIds[i], s);
        s.setDoubleClickReturnValue (true, kDefaults[i]);                   // doble clic = valor por defecto
    }

    setResizable (true, true);
    setResizeLimits (228, 433, 728, 1382);
    getConstrainer()->setFixedAspectRatio ((double) pedal::refW / (double) pedal::refH);
    setSize (364, 691);
}

FunkVoxEditor::~FunkVoxEditor()
{
    for (auto& s : sliders) s.setLookAndFeel (nullptr);
}

juce::Rectangle<int> FunkVoxEditor::labelBounds (int k) const
{
    const float s = (float) getWidth() / pedal::refW;
    return juce::Rectangle<float> (pedal::knobX[k] - 88.0f, pedal::labelY - 18.0f, 176.0f, 36.0f).transformedBy (juce::AffineTransform::scale (s)).toNearestInt();
}

void FunkVoxEditor::repaintLabels()
{
    const float s = (float) getWidth() / pedal::refW;
    repaint (0, juce::roundToInt ((pedal::labelY - 24.0f) * s), getWidth(), juce::roundToInt (48.0f * s));
}

juce::String FunkVoxEditor::readout (int k) const
{
    const double v = sliders[(size_t) k].getValue();
    switch (k)
    {
        case 0:  { const int o = (int) std::lround (v); return juce::String (o > 0 ? "+" : "") + juce::String (o) + " OCT"; }
        case 1:  return juce::String (juce::roundToInt (v * 100.0)) + "% PULSO";
        case 2:  return juce::String (juce::roundToInt (v)) + " ms";
        default: return juce::String (juce::roundToInt (v * 100.0)) + "% WET";
    }
}

void FunkVoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (background, getLocalBounds().toFloat());

    const float s = (float) getWidth() / pedal::refW;
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 25.0f * s, juce::Font::bold)));

    for (int k = 0; k < 4; ++k)
    {
        const bool showValue = sliders[(size_t) k].isMouseOverOrDragging();
        g.setColour (showValue ? kValueColour : kLabelColour);
        g.drawFittedText (showValue ? readout (k) : juce::String (kNames[k]),
                          labelBounds (k), juce::Justification::centred, 1, 0.6f);
    }
}

void FunkVoxEditor::resized()
{
    const float s = (float) getWidth() / pedal::refW;
    for (int k = 0; k < 4; ++k)
    {
        const auto r = juce::Rectangle<float> (pedal::knobX[k] - pedal::knobHit, pedal::knobY - pedal::knobHit,
                                               2.0f * pedal::knobHit, 2.0f * pedal::knobHit)
                           .transformedBy (juce::AffineTransform::scale (s));
        sliders[(size_t) k].setBounds (r.toNearestInt());
    }
}
