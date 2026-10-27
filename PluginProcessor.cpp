#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Curvas de clipping. x ya viene normalizado por el threshold (1.0 = nivel de clip).
    inline float softClip (float x) noexcept { return std::tanh (x); }

    inline float softClipPro (float x) noexcept
    {
        const float a = std::abs (x);
        if (a >= 1.5f)
            return std::copysign (1.0f, x);
        return x - (4.0f / 27.0f) * x * x * x; // llega a 1.0 con pendiente 0 en x = 1.5
    }

    inline float hardClip (float x) noexcept { return juce::jlimit (-1.0f, 1.0f, x); }

    inline float ratio21 (float x) noexcept
    {
        const float a = std::abs (x);
        return a <= 1.0f ? x : std::copysign (1.0f + (a - 1.0f) * 0.5f, x);
    }

    template <typename Shaper>
    void shapeBlock (juce::dsp::AudioBlock<float>& block, float threshold, Shaper shaper) noexcept
    {
        const float invT = 1.0f / threshold;
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* d = block.getChannelPointer (ch);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
                d[i] = threshold * shaper (d[i] * invT);
        }
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout DezuClipAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::inGain, 1 }, "Input Gain",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::clip, 1 }, "Clip Level",
        NormalisableRange<float> (-24.0f, 0.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::outGain, 1 }, "Output Gain",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::ceiling, 1 }, "Ceiling",
        NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -0.1f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::mode, 1 }, "Mode",
        StringArray { "Soft Clip", "Soft Clip Pro", "Hard Clip", "Ratio 2:1" }, 1));

    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::os, 1 }, "Oversampling",
        StringArray { "Off", "2x", "4x", "8x", "16x", "32x" }, 2));

    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { ids::phase, 1 }, "Filter Phase",
        StringArray { "Minimum", "Linear" }, 0));

    return { p.begin(), p.end() };
}

DezuClipAudioProcessor::DezuClipAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    inGainParam  = apvts.getRawParameterValue (ids::inGain);
    clipParam    = apvts.getRawParameterValue (ids::clip);
    outGainParam = apvts.getRawParameterValue (ids::outGain);
    ceilingParam = apvts.getRawParameterValue (ids::ceiling);
    modeParam    = apvts.getRawParameterValue (ids::mode);
    osParam      = apvts.getRawParameterValue (ids::os);
    phaseParam   = apvts.getRawParameterValue (ids::phase);
}

bool DezuClipAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DezuClipAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    using Type = juce::dsp::Oversampling<float>::FilterType;
    const Type types[2] = { Type::filterHalfBandPolyphaseIIR, Type::filterHalfBandFIREquiripple };

    for (int phase = 0; phase < 2; ++phase)
    {
        for (int stage = 0; stage < maxOsStages; ++stage)
        {
            auto os = std::make_unique<juce::dsp::Oversampling<float>> (
                2, (size_t) (stage + 1), types[phase], true, true);
            os->initProcessing ((size_t) samplesPerBlock);
            oversamplers[(size_t) phase][(size_t) stage] = std::move (os);
        }
    }

    preCopy.setSize (1, samplesPerBlock, false, true, false);

    lastInGain  = juce::Decibels::decibelsToGain (inGainParam->load());
    lastOutGain = juce::Decibels::decibelsToGain (outGainParam->load());
    lastKey = -1;
}

void DezuClipAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    if (numSamples == 0 || numCh < 2)
        return;

    const float inGain   = juce::Decibels::decibelsToGain (inGainParam->load());
    const float outGain  = juce::Decibels::decibelsToGain (outGainParam->load());
    const float clipT    = juce::Decibels::decibelsToGain (clipParam->load());
    const float ceilingG = juce::Decibels::decibelsToGain (ceilingParam->load());
    const int mode       = (int) modeParam->load();
    const int osIdx      = (int) osParam->load();   // 0 = off, 1..5 = 2x..32x
    const int phaseIdx   = (int) phaseParam->load();

    const float holdCoeff = std::exp (-(float) numSamples / (0.3f * (float) currentSampleRate));

    // Medidor de entrada (antes de ganancia)
    {
        const float peak = juce::jmax (buffer.getMagnitude (0, numSamples),
                                       inPeak.load() * holdCoeff);
        inPeak.store (peak);
    }

    // Ganancia de entrada con rampa
    buffer.applyGainRamp (0, numSamples, lastInGain, inGain);
    lastInGain = inGain;

    // Copia de la señal pre-clip para el osciloscopio
    if (preCopy.getNumSamples() < numSamples)
        preCopy.setSize (1, numSamples, false, false, true);
    preCopy.copyFrom (0, 0, buffer, 0, 0, numSamples);

    // Cambio de oversampling / fase: reiniciar filtros y avisar latencia
    const int key = osIdx * 2 + phaseIdx;
    if (key != lastKey)
    {
        lastKey = key;
        if (osIdx > 0)
        {
            auto& os = *oversamplers[(size_t) phaseIdx][(size_t) (osIdx - 1)];
            os.reset();
            setLatencySamples (juce::roundToInt (os.getLatencyInSamples()));
        }
        else
        {
            setLatencySamples (0);
        }
    }

    auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, 2);

    auto runShaper = [&] (juce::dsp::AudioBlock<float>& b)
    {
        switch (mode)
        {
            case 0:  shapeBlock (b, clipT, softClip);    break;
            case 1:  shapeBlock (b, clipT, softClipPro); break;
            case 2:  shapeBlock (b, clipT, hardClip);    break;
            default: shapeBlock (b, clipT, ratio21);     break;
        }
    };

    if (osIdx == 0)
    {
        runShaper (block);
    }
    else
    {
        auto& os = *oversamplers[(size_t) phaseIdx][(size_t) (osIdx - 1)];
        auto upBlock = os.processSamplesUp (block);
        runShaper (upBlock);
        os.processSamplesDown (block);
    }

    // Ganancia de salida con rampa
    buffer.applyGainRamp (0, numSamples, lastOutGain, outGain);
    lastOutGain = outGain;

    // Ceiling (después del sobremuestreo)
    float peak = 0.0f;
    double sumSq = 0.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            d[i] = juce::jlimit (-ceilingG, ceilingG, d[i]);
            peak = juce::jmax (peak, std::abs (d[i]));
            sumSq += (double) d[i] * d[i];
        }
    }

    outPeak.store (juce::jmax (peak, outPeak.load() * holdCoeff));
    outRms.store ((float) std::sqrt (sumSq / (2.0 * numSamples)));

    // Osciloscopio (decimado: una muestra cada 16)
    const float* pre  = preCopy.getReadPointer (0);
    const float* post = buffer.getReadPointer (0);
    int pos = scopeWritePos.load (std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i)
    {
        if (std::abs (pre[i])  >= std::abs (windowPre))  windowPre  = pre[i];
        if (std::abs (post[i]) >= std::abs (windowPost)) windowPost = post[i];

        if (++scopeCounter >= 16)
        {
            scopePre[(size_t) pos].store (windowPre,  std::memory_order_relaxed);
            scopePost[(size_t) pos].store (windowPost, std::memory_order_relaxed);
            pos = (pos + 1) % scopeSize;
            scopeCounter = 0;
            windowPre = windowPost = 0.0f;
        }
    }
    scopeWritePos.store (pos, std::memory_order_release);
}

juce::AudioProcessorEditor* DezuClipAudioProcessor::createEditor()
{
    return new DezuClipAudioProcessorEditor (*this);
}

void DezuClipAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DezuClipAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DezuClipAudioProcessor();
}
