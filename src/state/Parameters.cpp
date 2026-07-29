#include "state/Parameters.h"

namespace nammodeler
{
namespace pid
{
juce::String slotParam(int slotIndex, const char* baseId)
{
    return juce::String(slotIndex == 0 ? "a_" : "b_") + baseId;
}
} // namespace pid

namespace
{
constexpr int kVersionHint = 1;

juce::ParameterID makeId(const juce::String& id)
{
    return juce::ParameterID{id, kVersionHint};
}

// Symmetric skew so 0 dB lands at slider centre for asymmetric-looking ranges.
juce::NormalisableRange<float> dbRange(float lo, float hi)
{
    juce::NormalisableRange<float> range{lo, hi, 0.01f};
    range.setSkewForCentre(0.0f);
    return range;
}

std::unique_ptr<juce::AudioParameterFloat> gainParam(const juce::String& id, const juce::String& name, float lo,
                                                     float hi)
{
    return std::make_unique<juce::AudioParameterFloat>(
        makeId(id), name, dbRange(lo, hi), 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB").withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1) + " dB"; }));
}

// Amp-style 0-10 knob, 5 = flat.
std::unique_ptr<juce::AudioParameterFloat> toneParam(const juce::String& id, const juce::String& name)
{
    return std::make_unique<juce::AudioParameterFloat>(
        makeId(id), name, juce::NormalisableRange<float>{0.0f, 10.0f, 0.01f}, 5.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 2); }));
}

void addSlotParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
{
    const juce::String slotName = slotIndex == 0 ? "Amp A" : "Amp B";
    const auto id = [slotIndex](const char* base) { return pid::slotParam(slotIndex, base); };
    const auto name = [&slotName](const juce::String& label) { return slotName + " " + label; };

    auto group = std::make_unique<juce::AudioProcessorParameterGroup>(
        slotIndex == 0 ? "slotA" : "slotB", slotName, "|");

    group->addChild(std::make_unique<juce::AudioParameterBool>(makeId(id(pid::enabled)), name("On"), true));
    group->addChild(gainParam(id(pid::inTrim), name("Input Trim"), -20.0f, 20.0f));
    group->addChild(gainParam(id(pid::outTrim), name("Output Trim"), -40.0f, 40.0f));

    group->addChild(std::make_unique<juce::AudioParameterFloat>(
        makeId(id(pid::pan)), name("Pan"), juce::NormalisableRange<float>{-1.0f, 1.0f, 0.001f}, 0.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction([](float v, int) {
            if (std::abs(v) < 0.005f)
                return juce::String("C");
            const int amount = juce::roundToInt(std::abs(v) * 100.0f);
            return (v < 0.0f ? juce::String("L") : juce::String("R")) + juce::String(amount);
        })));

    group->addChild(std::make_unique<juce::AudioParameterBool>(makeId(id(pid::phase)), name("Phase Invert"), false));
    group->addChild(std::make_unique<juce::AudioParameterBool>(makeId(id(pid::solo)), name("Solo"), false));
    group->addChild(std::make_unique<juce::AudioParameterBool>(makeId(id(pid::mute)), name("Mute"), false));

    group->addChild(std::make_unique<juce::AudioParameterBool>(makeId(id(pid::eqOn)), name("EQ"), true));
    group->addChild(toneParam(id(pid::bass), name("Bass")));
    group->addChild(toneParam(id(pid::mid), name("Mid")));
    group->addChild(toneParam(id(pid::treble), name("Treble")));

    // The NAM-exposed parameters: everything a .nam model actually surfaces.
    group->addChild(std::make_unique<juce::AudioParameterChoice>(
        makeId(id(pid::outMode)), name("Output Mode"), juce::StringArray{"Raw", "Normalized", "Calibrated"},
        static_cast<int>(OutputMode::normalized)));

    group->addChild(std::make_unique<juce::AudioParameterBool>(makeId(id(pid::calIn)), name("Calibrate Input"), false));

    group->addChild(std::make_unique<juce::AudioParameterFloat>(
        makeId(id(pid::calDbu)), name("Interface Level"), juce::NormalisableRange<float>{-60.0f, 60.0f, 0.1f}, 12.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dBu").withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1) + " dBu"; })));

    group->addChild(std::make_unique<juce::AudioParameterFloat>(
        makeId(id(pid::slim)), name("Model Size"), juce::NormalisableRange<float>{0.0f, 1.0f, 0.01f}, 1.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(
            [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; })));

    layout.add(std::move(group));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    addSlotParameters(layout, 0);
    addSlotParameters(layout, 1);

    auto global = std::make_unique<juce::AudioProcessorParameterGroup>("global", "Global", "|");

    global->addChild(std::make_unique<juce::AudioParameterChoice>(
        makeId(pid::inputMode), "Input Mode", juce::StringArray{"Mono", "Stereo"},
        static_cast<int>(InputMode::mono)));

    global->addChild(gainParam(pid::masterOut, "Master Output", -40.0f, 40.0f));
    global->addChild(std::make_unique<juce::AudioParameterBool>(makeId(pid::monoSum), "Mono Sum", false));
    global->addChild(std::make_unique<juce::AudioParameterBool>(makeId(pid::gateOn), "Gate", false));

    global->addChild(std::make_unique<juce::AudioParameterFloat>(
        makeId(pid::gateThresh), "Gate Threshold", juce::NormalisableRange<float>{-100.0f, 0.0f, 0.1f}, -80.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB").withStringFromValueFunction([](float v, int) {
            return v <= -99.95f ? juce::String("Off") : juce::String(v, 1) + " dB";
        })));

    layout.add(std::move(global));

    return layout;
}

} // namespace nammodeler
