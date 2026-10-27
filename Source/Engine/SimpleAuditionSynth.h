#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <algorithm>

namespace reharm::engine
{

/**
 * @brief Phase 12: 内部プレビュー再生（Audition）用の軽量 SynthesiserSound
 */
class SimpleAuditionSound final : public juce::SynthesiserSound
{
public:
    SimpleAuditionSound() = default;
    ~SimpleAuditionSound() override = default;

    bool appliesToNote(int /*midiNoteNumber*/) override { return true; }
    bool appliesToChannel(int /*midiChannel*/) override { return true; }
};

/**
 * @brief Phase 12: 内部プレビュー再生（Audition）用の軽量ポリフォニック SynthesiserVoice
 *
 * アタックが早く（約4ms）、心地よい減衰とリリースを持つサイン波＋倍音エレピ（Rhodes風）音色を生成します。
 */
class SimpleAuditionVoice final : public juce::SynthesiserVoice
{
public:
    SimpleAuditionVoice()
    {
        adsrParams_.attack  = 0.004f;
        adsrParams_.decay   = 1.40f;
        adsrParams_.sustain = 0.25f;
        adsrParams_.release = 0.18f;
        adsr_.setParameters(adsrParams_);
    }

    ~SimpleAuditionVoice() override = default;

    bool canPlaySound(juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SimpleAuditionSound*>(sound) != nullptr;
    }

    void setCurrentPlaybackSampleRate(double newRate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate(newRate);
        if (newRate > 0.0)
        {
            adsr_.setSampleRate(newRate);
            adsr_.setParameters(adsrParams_);
        }
    }

    void startNote(int midiNoteNumber,
                   float velocity,
                   juce::SynthesiserSound* /*sound*/,
                   int /*currentPitchWheelPosition*/) override
    {
        const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
        const double cyclesPerSecond = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
        const double cyclesPerSample = cyclesPerSecond / sr;

        currentAngle_ = 0.0;
        angleDelta_   = cyclesPerSample * juce::MathConstants<double>::twoPi;

        tineAngle_      = 0.0;
        tineAngleDelta_ = cyclesPerSample * 7.0 * juce::MathConstants<double>::twoPi;
        tineEnv_        = 1.0f;
        tineDecayCoeff_ = std::exp(-1.0f / static_cast<float>(sr * 0.14));

        level_ = std::clamp(velocity, 0.05f, 1.0f) * 0.18f;
        pan_   = std::clamp((static_cast<float>(midiNoteNumber) - 60.0f) / 36.0f, -0.6f, 0.6f);

        adsr_.noteOn();
    }

    void stopNote(float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr_.noteOff();
        }
        else
        {
            clearCurrentNote();
            adsr_.reset();
            angleDelta_ = 0.0;
        }
    }

    void pitchWheelMoved(int /*newPitchWheelValue*/) override {}
    void controllerMoved(int /*controllerNumber*/, int /*newControllerValue*/) override {}

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                         int startSample,
                         int numSamples) override
    {
        if (angleDelta_ == 0.0 || !adsr_.isActive())
            return;

        const int numChannels = outputBuffer.getNumChannels();
        if (numChannels <= 0)
            return;

        const float gainL = 0.5f * (1.0f - pan_);
        const float gainR = 0.5f * (1.0f + pan_);

        for (int sampleIdx = startSample; sampleIdx < startSample + numSamples; ++sampleIdx)
        {
            const float envValue = adsr_.getNextSample();
            tineEnv_ *= tineDecayCoeff_;

            // 基本サイン波 + 2倍音 + ベルアタック倍音（軽量エレピ音色）
            const float fundamental = static_cast<float>(std::sin(currentAngle_));
            const float harmonic2   = 0.20f * static_cast<float>(std::sin(currentAngle_ * 2.0));
            const float tineBell    = 0.12f * tineEnv_ * static_cast<float>(std::sin(tineAngle_));

            const float currentSample = (fundamental + harmonic2 + tineBell) * level_ * envValue;

            currentAngle_ += angleDelta_;
            if (currentAngle_ >= juce::MathConstants<double>::twoPi)
                currentAngle_ -= juce::MathConstants<double>::twoPi;

            tineAngle_ += tineAngleDelta_;
            if (tineAngle_ >= juce::MathConstants<double>::twoPi)
                tineAngle_ -= juce::MathConstants<double>::twoPi;

            outputBuffer.addSample(0, sampleIdx, currentSample * (numChannels > 1 ? gainL * 2.0f : 1.0f));
            if (numChannels > 1)
            {
                outputBuffer.addSample(1, sampleIdx, currentSample * (gainR * 2.0f));
            }

            if (!adsr_.isActive())
            {
                clearCurrentNote();
                angleDelta_ = 0.0;
                break;
            }
        }
    }

private:
    juce::ADSR adsr_;
    juce::ADSR::Parameters adsrParams_;

    double currentAngle_ { 0.0 };
    double angleDelta_ { 0.0 };
    double tineAngle_ { 0.0 };
    double tineAngleDelta_ { 0.0 };
    float tineEnv_ { 0.0f };
    float tineDecayCoeff_ { 0.999f };
    float level_ { 0.0f };
    float pan_ { 0.0f };
};

/**
 * @brief `juce::Synthesiser` に軽量エレピボイスとサウンドを登録する初期化ヘルパー
 */
inline void setupSimpleAuditionSynth(juce::Synthesiser& synth, int numVoices = 12)
{
    synth.clearVoices();
    synth.clearSounds();

    for (int i = 0; i < numVoices; ++i)
    {
        synth.addVoice(new SimpleAuditionVoice());
    }
    synth.addSound(new SimpleAuditionSound());
}

} // namespace reharm::engine
