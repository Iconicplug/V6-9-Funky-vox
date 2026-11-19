#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

//==============================================================================
// Geometria del pedal (en pixeles de la imagen original 910 x 1728).
// Las perillas ya estan dibujadas en la imagen: aqui solo se pinta el indicador
// blanco que gira, asi el diseno queda identico al original.
namespace pedal
{
    constexpr float refW = 910.0f, refH = 1728.0f;
    constexpr float knobX[4] = { 186.0f, 365.0f, 547.0f, 727.0f };
    constexpr float knobY    = 320.0f;     // centro vertical de las perillas
    constexpr float knobHit  = 66.0f;      // radio del area de arrastre
    constexpr float labelY   = 478.0f;     // texto bajo las perillas
}

class PedalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;
};

class FunkVoxEditor : public juce::AudioProcessorEditor
{
public:
    explicit FunkVoxEditor (FunkVoxProcessor&);
    ~FunkVoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override { repaintLabels(); }
    void mouseExit  (const juce::MouseEvent&) override { repaintLabels(); }

private:
    juce::String readout (int knob) const;
    juce::Rectangle<int> labelBounds (int knob) const;
    void repaintLabels();

    FunkVoxProcessor& proc;
    PedalLookAndFeel lnf;
    juce::Image background;

    std::array<juce::Slider, 4> sliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 4> attachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FunkVoxEditor)
};
