#pragma once

#include "PluginProcessor.h"

class DezuClipAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit DezuClipAudioProcessorEditor (DezuClipAudioProcessor&);
    ~DezuClipAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    void timerCallback() override;
    void setupKnob (Knob&, const juce::String& text, const char* paramId);
    void setupChoice (Choice&, const juce::String& text, const char* paramId);

    DezuClipAudioProcessor& processor;

    Knob inKnob, clipKnob, outKnob, ceilingKnob;
    Choice modeBox, osBox, phaseBox;

    juce::Rectangle<int> scopeArea, meterArea;
    float inPeakDisp = 0.0f, outPeakDisp = 0.0f, outRmsDisp = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DezuClipAudioProcessorEditor)
};
