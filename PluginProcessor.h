#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

namespace ids
{
    inline constexpr const char* inGain  = "inGain";
    inline constexpr const char* clip    = "clip";
    inline constexpr const char* outGain = "outGain";
    inline constexpr const char* ceiling = "ceiling";
    inline constexpr const char* mode    = "mode";
    inline constexpr const char* os      = "os";
    inline constexpr const char* phase   = "phase";
}

class DezuClipAudioProcessor : public juce::AudioProcessor
{
public:
    DezuClipAudioProcessor();
    ~DezuClipAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "dezuClip"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Medidores (valores lineales), leídos por el editor.
    std::atomic<float> inPeak  { 0.0f };
    std::atomic<float> outPeak { 0.0f };
    std::atomic<float> outRms  { 0.0f };

    // Osciloscopio: señal antes del clip (tras input gain) y señal final.
    static constexpr int scopeSize = 512;
    std::array<std::atomic<float>, scopeSize> scopePre {};
    std::array<std::atomic<float>, scopeSize> scopePost {};
    std::atomic<int> scopeWritePos { 0 };

private:
    static constexpr int maxOsStages = 5; // 2x ... 32x

    std::array<std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, maxOsStages>, 2> oversamplers;

    std::atomic<float>* inGainParam  = nullptr;
    std::atomic<float>* clipParam    = nullptr;
    std::atomic<float>* outGainParam = nullptr;
    std::atomic<float>* ceilingParam = nullptr;
    std::atomic<float>* modeParam    = nullptr;
    std::atomic<float>* osParam      = nullptr;
    std::atomic<float>* phaseParam   = nullptr;

    double currentSampleRate = 44100.0;
    float lastInGain  = 1.0f;
    float lastOutGain = 1.0f;
    int lastKey = -1;

    juce::AudioBuffer<float> preCopy;
    int scopeCounter = 0;
    float windowPre = 0.0f, windowPost = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DezuClipAudioProcessor)
};
