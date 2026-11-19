#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

// Solo 4 parametros: los mismos ids de siempre, asi los proyectos guardados siguen funcionando.
APVTS::ParameterLayout FunkVoxProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { "octave", 1 }, "Octava", -2, 2, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "pulse", 1 },  "Sierra <> Pulso", 0.0f, 1.0f, 0.25f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 },  "Glide (ms)",
                 NormalisableRange<float> (0.0f, 400.0f, 1.0f, 0.5f), 20.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 },    "Dry/Wet", 0.0f, 1.0f, 1.0f));

    return { p.begin(), p.end() };
}

FunkVoxProcessor::FunkVoxProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool FunkVoxProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in  = l.getMainInputChannelSet();
    const auto out = l.getMainOutputChannelSet();
    if (in != out) return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;

    const auto sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void FunkVoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    const auto n = (size_t) juce::jmax (samplesPerBlock, 512);
    inMono.assign (n, 0.0f);
    scMono.assign (n, 0.0f);
    outMono.assign (n, 0.0f);
    setLatencySamples (0);          // sin latencia: sirve para grabar y para cantar en vivo
}

void FunkVoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    if ((int) inMono.size() < n)
    {
        inMono.resize ((size_t) n);
        scMono.resize ((size_t) n);
        outMono.resize ((size_t) n);
    }

    // ---- voz (bus principal) -> mono
    auto mainBuf = getBusBuffer (buffer, true, 0);
    const int numCh = mainBuf.getNumChannels();
    if (numCh == 0) return;

    std::fill (inMono.begin(), inMono.begin() + n, 0.0f);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* src = mainBuf.getReadPointer (ch);
        for (int i = 0; i < n; ++i) inMono[(size_t) i] += src[i];
    }
    if (numCh > 1)
        for (int i = 0; i < n; ++i) inMono[(size_t) i] /= (float) numCh;

    // ---- sidechain (sintetizador de otra pista) -> mono
    bool hasSC = false;
    if (getBusCount (true) > 1)
        if (auto* scBus = getBus (true, 1))
            if (scBus->isEnabled())
            {
                auto sb = getBusBuffer (buffer, true, 1);
                if (sb.getNumChannels() > 0)
                {
                    std::fill (scMono.begin(), scMono.begin() + n, 0.0f);
                    for (int ch = 0; ch < sb.getNumChannels(); ++ch)
                    {
                        const float* src = sb.getReadPointer (ch);
                        for (int i = 0; i < n; ++i) scMono[(size_t) i] += src[i];
                    }
                    const float k = 1.0f / (float) sb.getNumChannels();
                    for (int i = 0; i < n; ++i) scMono[(size_t) i] *= k;
                    hasSC = true;
                }
            }

    // ---- parametros
    engine.setParams ((int) std::lround (apvts.getRawParameterValue ("octave")->load()),
                      apvts.getRawParameterValue ("pulse")->load(),
                      apvts.getRawParameterValue ("glide")->load(),
                      apvts.getRawParameterValue ("mix")->load());

    // ---- audio, partido en los eventos MIDI para precision de muestra
    const float* scPtr = hasSC ? scMono.data() : nullptr;
    int pos = 0;

    for (const auto meta : midi)
    {
        const int t = juce::jlimit (pos, n, meta.samplePosition);
        if (t > pos)
        {
            engine.process (inMono.data() + pos, scPtr ? scPtr + pos : nullptr, outMono.data() + pos, t - pos);
            pos = t;
        }

        const auto m = meta.getMessage();
        if      (m.isNoteOn())                          engine.noteOn  (m.getNoteNumber());
        else if (m.isNoteOff())                         engine.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
    }

    if (pos < n)
        engine.process (inMono.data() + pos, scPtr ? scPtr + pos : nullptr, outMono.data() + pos, n - pos);

    // ---- salida a todos los canales
    for (int ch = 0; ch < numCh; ++ch)
        std::copy (outMono.begin(), outMono.begin() + n, mainBuf.getWritePointer (ch));

    midi.clear();
}

juce::AudioProcessorEditor* FunkVoxProcessor::createEditor()
{
    return new FunkVoxEditor (*this);
}

void FunkVoxProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void FunkVoxProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FunkVoxProcessor(); }
