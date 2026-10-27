#include "PluginEditor.h"

namespace
{
    const juce::Colour bgTop    { 0xff16181d };
    const juce::Colour bgBottom { 0xff0d0e11 };
    const juce::Colour accent   { 0xff3ddc97 };
    const juce::Colour muted    { 0xff8a90a0 };

    float dbToNorm (float gain)
    {
        const float db = juce::Decibels::gainToDecibels (gain, -60.0f);
        return juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, 6.0f, 0.0f, 1.0f));
    }
}

DezuClipAudioProcessorEditor::DezuClipAudioProcessorEditor (DezuClipAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setupKnob (inKnob,      "INPUT",   ids::inGain);
    setupKnob (clipKnob,    "CLIP",    ids::clip);
    setupKnob (outKnob,     "OUTPUT",  ids::outGain);
    setupKnob (ceilingKnob, "CEILING", ids::ceiling);

    setupChoice (modeBox,  "MODE",         ids::mode);
    setupChoice (osBox,    "OVERSAMPLING", ids::os);
    setupChoice (phaseBox, "FILTER PHASE", ids::phase);

    setSize (580, 420);
    startTimerHz (30);
}

DezuClipAudioProcessorEditor::~DezuClipAudioProcessorEditor()
{
    stopTimer();
}

void DezuClipAudioProcessorEditor::setupKnob (Knob& k, const juce::String& text, const char* paramId)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    k.slider.setTextValueSuffix (" dB");
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff2a2e38));
    addAndMakeVisible (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, muted);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<SliderAttachment> (processor.apvts, paramId, k.slider);
}

void DezuClipAudioProcessorEditor::setupChoice (Choice& c, const juce::String& text, const char* paramId)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramId)))
        c.box.addItemList (param->choices, 1);

    addAndMakeVisible (c.box);

    c.label.setText (text, juce::dontSendNotification);
    c.label.setJustificationType (juce::Justification::centred);
    c.label.setColour (juce::Label::textColourId, muted);
    c.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (c.label);

    c.attachment = std::make_unique<ComboBoxAttachment> (processor.apvts, paramId, c.box);
}

void DezuClipAudioProcessorEditor::timerCallback()
{
    inPeakDisp  = juce::jmax (processor.inPeak.load(),  inPeakDisp  * 0.85f);
    outPeakDisp = juce::jmax (processor.outPeak.load(), outPeakDisp * 0.85f);
    outRmsDisp  = juce::jmax (processor.outRms.load(),  outRmsDisp  * 0.85f);
    repaint (scopeArea.getUnion (meterArea));
}

void DezuClipAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("dezuClip", 16, 8, 200, 30, juce::Justification::centredLeft);

    // --- Osciloscopio ---
    auto sa = scopeArea.toFloat();
    g.setColour (juce::Colour (0xff0a0b0e));
    g.fillRoundedRectangle (sa, 6.0f);
    g.setColour (juce::Colour (0xff2a2e38));
    g.drawRoundedRectangle (sa, 6.0f, 1.0f);

    const float range = 1.5f; // +-1.5 de escala completa
    const float midY = sa.getCentreY();
    auto toY = [&] (float v)
    {
        return midY - juce::jlimit (-range, range, v) / range * (sa.getHeight() * 0.5f - 4.0f);
    };

    g.setColour (juce::Colour (0xff2a2e38));
    g.drawHorizontalLine ((int) midY, sa.getX(), sa.getRight());
    g.setColour (juce::Colour (0xff4a3a3a));
    const float y1 = toY (1.0f), y2 = toY (-1.0f);
    g.drawHorizontalLine ((int) y1, sa.getX(), sa.getRight());
    g.drawHorizontalLine ((int) y2, sa.getX(), sa.getRight());

    const int n = DezuClipAudioProcessor::scopeSize;
    const int start = processor.scopeWritePos.load (std::memory_order_acquire);

    juce::Path prePath, postPath;
    for (int i = 0; i < n; ++i)
    {
        const int idx = (start + i) % n;
        const float x = sa.getX() + sa.getWidth() * (float) i / (float) (n - 1);
        const float yPre  = toY (processor.scopePre[(size_t) idx].load (std::memory_order_relaxed));
        const float yPost = toY (processor.scopePost[(size_t) idx].load (std::memory_order_relaxed));

        if (i == 0) { prePath.startNewSubPath (x, yPre); postPath.startNewSubPath (x, yPost); }
        else        { prePath.lineTo (x, yPre);          postPath.lineTo (x, yPost); }
    }

    g.saveState();
    g.reduceClipRegion (scopeArea);
    g.setColour (juce::Colour (0x66ff6b6b));
    g.strokePath (prePath, juce::PathStrokeType (1.0f));
    g.setColour (accent);
    g.strokePath (postPath, juce::PathStrokeType (1.5f));
    g.restoreState();

    g.setColour (muted);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("pre-clip", scopeArea.reduced (8, 4), juce::Justification::topLeft);
    g.setColour (accent);
    g.drawText ("output", scopeArea.reduced (8, 4).translated (60, 0), juce::Justification::topLeft);

    // --- Medidores (IN / OUT peak / RMS) ---
    auto drawMeter = [&] (juce::Rectangle<float> r, float value, juce::Colour c, const juce::String& name)
    {
        g.setColour (juce::Colour (0xff0a0b0e));
        g.fillRoundedRectangle (r, 3.0f);
        const float h = r.getHeight() * dbToNorm (value);
        g.setColour (c);
        g.fillRoundedRectangle (r.withTop (r.getBottom() - h), 3.0f);
        g.setColour (muted);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (name, r.withY (r.getBottom() + 2.0f).withHeight (12.0f).expanded (6.0f, 0.0f),
                    juce::Justification::centred);
    };

    auto mr = meterArea.toFloat().withTrimmedBottom (14.0f);
    const float w = mr.getWidth() / 3.0f;
    drawMeter (mr.removeFromLeft (w).reduced (3.0f, 0.0f), inPeakDisp,  juce::Colour (0xffff6b6b), "IN");
    drawMeter (mr.removeFromLeft (w).reduced (3.0f, 0.0f), outPeakDisp, accent,                    "PK");
    drawMeter (mr.reduced (3.0f, 0.0f),                    outRmsDisp,  juce::Colour (0xff5ab0ff), "RMS");
}

void DezuClipAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (32); // título

    auto top = area.removeFromTop (150);
    meterArea = top.removeFromRight (90);
    top.removeFromRight (10);
    scopeArea = top;

    area.removeFromTop (14);

    auto knobRow = area.removeFromTop (150);
    const int kw = knobRow.getWidth() / 4;
    auto place = [&] (Knob& k)
    {
        auto cell = knobRow.removeFromLeft (kw);
        k.label.setBounds (cell.removeFromTop (18));
        k.slider.setBounds (cell);
    };
    place (inKnob);
    place (clipKnob);
    place (outKnob);
    place (ceilingKnob);

    area.removeFromTop (6);

    auto choiceRow = area.removeFromTop (50);
    const int cw = choiceRow.getWidth() / 3;
    auto placeChoice = [&] (Choice& c)
    {
        auto cell = choiceRow.removeFromLeft (cw).reduced (8, 0);
        c.label.setBounds (cell.removeFromTop (16));
        c.box.setBounds (cell.removeFromTop (26));
    };
    placeChoice (modeBox);
    placeChoice (osBox);
    placeChoice (phaseBox);
}
