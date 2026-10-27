#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/ChordTemplateBank.h"
#include <cmath>

namespace reharm
{

TriLayerReharmAudioProcessor::TriLayerReharmAudioProcessor()
    // メインのステレオオーディオ入出力（後段への完全パススルーおよびAuditionミックス用）と
    // サイドチェイン入力（Audio Groove抽出用）を備えるバス構成
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), true)),
      // Phase 12: UndoManager を APVTS の第2引数へバインド
      apvts_(*this, &undoManager_, "TriLayerReharmParameters", createParameterLayout())
{
    modeParamPtr_       = apvts_.getRawParameterValue(ParamIDs::operatingMode);
    grooveSyncParamPtr_ = apvts_.getRawParameterValue(ParamIDs::enableGrooveSync);
    auditionParamPtr_   = apvts_.getRawParameterValue(ParamIDs::enableAudition);
    lengthParamPtr_     = apvts_.getRawParameterValue(ParamIDs::progressionLength);

    // Phase 12: 軽量ポリフォニック Audition シンセサイザーの初期化（12ボイス）
    engine::setupSimpleAuditionSynth(auditionSynth_, 12);

    const auto initialSeq = engine::ChordTemplateBank::createPresetSequence(0, 0, false, 960.0);
    reharmEngine_.setSourceMidiSequence(initialSeq, 960.0);
    reharmEngine_.updateRulesFromPresets(0, 0, 0);
    reharmEngine_.triggerOfflineReharmonize(true);
}

juce::AudioProcessorValueTreeState::ParameterLayout TriLayerReharmAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // 動作モード切り替えパラメーター（デフォルト: 0 = Bypass (Thru)）
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::operatingMode, 1 },
        "Operating Mode",
        juce::StringArray { "Bypass (Thru)", "Capture MIDI", "Playback Reharm" },
        0));

    // Phase 12: Layer 1: Genre (8ジャンルへ拡張)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::genre, 1 },
        "Genre",
        juce::StringArray {
            "Jazz",
            "Neo-Soul",
            "City Pop",
            "Cinematic",
            "Lo-Fi",
            "Modern R&B",
            "Gospel",
            "8-Bit (Chiptune)"
        },
        0));

    // Phase 12: ジャンル内コード進行プリセット選択 (0〜3、Undo/Redo対象)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::progressionPreset, 1 },
        "Progression Preset",
        juce::StringArray { "Preset 1", "Preset 2", "Preset 3", "Preset 4" },
        0));

    // Phase 14: プリセットコード進行の長さ選択 ("4 Bars", "8 Bars")
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::progressionLength, 1 },
        "Progression Length",
        juce::StringArray { "4 Bars", "8 Bars" },
        0));

    // Layer 2: Artist
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::artist, 1 },
        "Artist",
        juce::StringArray { "Artist A", "Artist B", "Artist C", "Artist D" },
        0));

    // Layer 3: Environment
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::environment, 1 },
        "Environment",
        juce::StringArray { "Rainy", "VHS", "Deep Space" },
        0));

    // Phase 8: Mutation Amount (0.0% = Basic, 50.0% = Advanced, 100.0% = Chaos)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParamIDs::mutationAmount, 1 },
        "Mutation Amount",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Phase 9: Audio Groove Sync (Sidechain Transient Quantize Toggle)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::enableGrooveSync, 1 },
        "Audio Groove Sync",
        false));

    // Phase 12: Audition (Preview) 内部シンセ再生トグル
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::enableAudition, 1 },
        "Audition (Preview)",
        false));

    return { params.begin(), params.end() };
}

const juce::String TriLayerReharmAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool TriLayerReharmAudioProcessor::acceptsMidi() const
{
    return true;
}

bool TriLayerReharmAudioProcessor::producesMidi() const
{
    return true;
}

bool TriLayerReharmAudioProcessor::isMidiEffect() const
{
    return true;
}

double TriLayerReharmAudioProcessor::getTailLengthSeconds() const
{
    return 0.25;
}

int TriLayerReharmAudioProcessor::getNumPrograms()
{
    return 1;
}

int TriLayerReharmAudioProcessor::getCurrentProgram()
{
    return 0;
}

void TriLayerReharmAudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String TriLayerReharmAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void TriLayerReharmAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void TriLayerReharmAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    fastEnvelope_ = 0.0f;
    slowEnvelope_ = 0.0f;
    cooldownSamplesRemaining_ = 0;
    debugSampleCounter_ = 0;
    auditionPlayheadPpq_ = 0.0;
    wasAuditionActive_ = false;

    reharmEngine_.prepare(sampleRate_, samplesPerBlock);
    auditionSynth_.setCurrentPlaybackSampleRate(sampleRate_);
}

void TriLayerReharmAudioProcessor::releaseResources()
{
    reharmEngine_.reset();
    auditionSynth_.allNotesOff(0, false);
}

bool TriLayerReharmAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto inSet  = layouts.getMainInputChannelSet();
    const auto outSet = layouts.getMainOutputChannelSet();

    const bool validIn = (inSet.isDisabled()
                          || inSet == juce::AudioChannelSet::mono()
                          || inSet == juce::AudioChannelSet::stereo());
    const bool validOut = (outSet.isDisabled()
                           || outSet == juce::AudioChannelSet::mono()
                           || outSet == juce::AudioChannelSet::stereo());

    return validIn && validOut;
}

void TriLayerReharmAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    // 1秒に1回程度の頻度で生存確認ログ（Audio Samples数とMIDI Events数）を出力
    debugSampleCounter_ += numSamples;
    if (debugSampleCounter_ >= static_cast<int>(sampleRate_))
    {
        debugSampleCounter_ = 0;
        DBG("processBlock called: Audio Samples=" << numSamples
            << ", MIDI Events=" << midiMessages.getNumEvents());
    }

    if (modeParamPtr_ != nullptr)
    {
        const auto modeIndex = static_cast<int>(modeParamPtr_->load(std::memory_order_relaxed));
        reharmEngine_.setOperatingMode(static_cast<engine::OperatingMode>(modeIndex));
    }

    const bool grooveSyncEnabled = (grooveSyncParamPtr_ != nullptr)
                                   && (grooveSyncParamPtr_->load(std::memory_order_relaxed) >= 0.5f);
    reharmEngine_.setGrooveSyncEnabled(grooveSyncEnabled);

    const bool auditionEnabled = (auditionParamPtr_ != nullptr)
                                 && (auditionParamPtr_->load(std::memory_order_relaxed) >= 0.5f);

    juce::Optional<juce::AudioPlayHead::PositionInfo> posInfo;
    if (auto* currentPlayHead = getPlayHead())
    {
        posInfo = currentPlayHead->getPosition();
    }

    // 1. サイドチェイン入力（有効かつ実バッファにチャンネルが存在する場合）またはメイン入力からトランジェントを検出し GrooveMap を更新
    const int totalBufferChannels = buffer.getNumChannels();
    bool sidechainProcessed = false;

    if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled())
    {
        const int scOffset   = getChannelIndexInProcessBlockBuffer(true, 1, 0);
        const int scChannels = getBus(true, 1)->getNumberOfChannels();
        if (scChannels > 0 && scOffset >= 0 && (scOffset + scChannels) <= totalBufferChannels && numSamples > 0)
        {
            auto sidechainBuffer = getBusBuffer(buffer, true, 1);
            if (sidechainBuffer.getNumChannels() > 0)
            {
                detectTransientsAndRecordGroove(sidechainBuffer, posInfo);
                sidechainProcessed = true;
            }
        }
    }

    if (!sidechainProcessed && totalBufferChannels > 0 && numSamples > 0)
    {
        juce::AudioBuffer<float> mainView(
            buffer.getArrayOfWritePointers(),
             std::min(totalBufferChannels, 2),
            numSamples);
        detectTransientsAndRecordGroove(mainView, posInfo);
    }

    // 2. オーディオ信号の完全パススルー処理（絶対に buffer.clear() で全体を消去しない）
    const int mainInBusChannels  = (getBusCount(true) > 0 && getBus(true, 0)->isEnabled())
                                       ? std::min(getBus(true, 0)->getNumberOfChannels(), totalBufferChannels)
                                       : 0;
    const int mainOutBusChannels = (getBusCount(false) > 0 && getBus(false, 0)->isEnabled())
                                       ? std::min(getBus(false, 0)->getNumberOfChannels(), totalBufferChannels)
                                       : 0;

    juce::AudioBuffer<float> mainInput(
        buffer.getArrayOfWritePointers(),
        mainInBusChannels,
        numSamples);
    juce::AudioBuffer<float> mainOutput(
        buffer.getArrayOfWritePointers(),
        mainOutBusChannels,
        numSamples);
    const int numInChannels  = mainInput.getNumChannels();
    const int numOutChannels = mainOutput.getNumChannels();

    for (int ch = 0; ch < numOutChannels; ++ch)
    {
        if (ch < numInChannels)
        {
            if (mainOutput.getWritePointer(ch) != mainInput.getReadPointer(ch))
            {
                mainOutput.copyFrom(ch, 0, mainInput, ch, 0, numSamples);
            }
        }
        else if (numInChannels == 1)
        {
            mainOutput.copyFrom(ch, 0, mainInput, 0, 0, numSamples);
        }
        else
        {
            mainOutput.clear(ch, 0, numSamples);
        }
    }

    // 3. MIDIのスルー＆ブロック処理（midiMessages 内のリアルタイムイベントを .clear() せずそのまま後段へ維持）
    reharmEngine_.processMidiBlock(midiMessages, posInfo, numSamples);

    // 4. Phase 12 & 14: Audition (Preview) 内部シンセ再生ルーティング（4小節=16.0 PPQ / 8小節=32.0 PPQ 対応）
    if (auditionEnabled && numOutChannels > 0 && numSamples > 0)
    {
        const bool isEightBars = (lengthParamPtr_ != nullptr)
                                 && (lengthParamPtr_->load(std::memory_order_relaxed) >= 0.5f);
        const double kLoopLengthPpq = isEightBars ? 32.0 : 16.0;
        const bool hostPlaying = posInfo.hasValue() && posInfo->getIsPlaying();
        const double rawBpm    = posInfo.hasValue() ? posInfo->getBpm().orFallback(120.0) : 120.0;
        const double safeBpm   = rawBpm > 10.0 ? rawBpm : 120.0;
        const double ppqPerSample = (safeBpm / 60.0) / sampleRate_;
        const double blockDurationPpq = static_cast<double>(numSamples) * ppqPerSample;

        double currentStartPpq = 0.0;
        if (hostPlaying)
        {
            currentStartPpq = std::fmod(std::max(0.0, posInfo->getPpqPosition().orFallback(0.0)), kLoopLengthPpq);
            auditionPlayheadPpq_ = std::fmod(currentStartPpq + blockDurationPpq, kLoopLengthPpq);
        }
        else
        {
            if (!wasAuditionActive_)
            {
                auditionPlayheadPpq_ = 0.0;
            }
            currentStartPpq = auditionPlayheadPpq_;
            auditionPlayheadPpq_ = std::fmod(auditionPlayheadPpq_ + blockDurationPpq, kLoopLengthPpq);
        }

        // ONに切り替わった直後の先頭ノート取りこぼし防止
        if (!wasAuditionActive_ && currentStartPpq < 0.05)
        {
            currentStartPpq = 0.0;
        }
        wasAuditionActive_ = true;

        juce::MidiBuffer auditionMidi;
        reharmEngine_.extractAuditionMidiForBlock(
            auditionMidi,
            currentStartPpq,
            blockDurationPpq,
            ppqPerSample,
            numSamples
        );

        // パススルー済みの mainOutput に対して内部シンセの音声を加算ミックス
        auditionSynth_.renderNextBlock(mainOutput, auditionMidi, 0, numSamples);
    }
    else if (wasAuditionActive_)
    {
        wasAuditionActive_ = false;
        auditionSynth_.allNotesOff(0, true);
    }
}

void TriLayerReharmAudioProcessor::detectTransientsAndRecordGroove(
    const juce::AudioBuffer<float>& buffer,
    const juce::Optional<juce::AudioPlayHead::PositionInfo>& posInfo)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    const float fastAttackCoeff  = std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.0015));
    const float fastReleaseCoeff = std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.0250));
    const float slowAttackCoeff  = std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.0250));
    const float slowReleaseCoeff = std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.1500));

    const int minHoldoffSamples = static_cast<int>(sampleRate_ * 0.075);

    const bool isPlaying = posInfo.hasValue() && posInfo->getIsPlaying();
    const double blockStartPpq = posInfo.hasValue() ? posInfo->getPpqPosition().orFallback(0.0) : 0.0;
    const double bpm           = posInfo.hasValue() ? posInfo->getBpm().orFallback(120.0) : 120.0;
    const double ppqPerSample  = (bpm / 60.0) / sampleRate_;

    constexpr float kNoiseFloor     = 0.025f;
    constexpr float kTransientRatio = 2.35f;

    for (int s = 0; s < numSamples; ++s)
    {
        float absSample = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            absSample = std::max(absSample, std::abs(buffer.getSample(ch, s)));
        }

        if (absSample > fastEnvelope_)
            fastEnvelope_ = fastAttackCoeff * fastEnvelope_ + (1.0f - fastAttackCoeff) * absSample;
        else
            fastEnvelope_ = fastReleaseCoeff * fastEnvelope_ + (1.0f - fastReleaseCoeff) * absSample;

        if (absSample > slowEnvelope_)
            slowEnvelope_ = slowAttackCoeff * slowEnvelope_ + (1.0f - slowAttackCoeff) * absSample;
        else
            slowEnvelope_ = slowReleaseCoeff * slowEnvelope_ + (1.0f - slowReleaseCoeff) * absSample;

        if (cooldownSamplesRemaining_ > 0)
        {
            --cooldownSamplesRemaining_;
            continue;
        }

        if (fastEnvelope_ > kNoiseFloor && fastEnvelope_ > (slowEnvelope_ * kTransientRatio + 0.015f))
        {
            cooldownSamplesRemaining_ = minHoldoffSamples;

            const float pulseStrength = std::clamp(fastEnvelope_ * 1.8f, 0.25f, 1.0f);
            transientVisualPulse_.store(pulseStrength, std::memory_order_relaxed);

            if (isPlaying)
            {
                const double transientPpq = blockStartPpq + static_cast<double>(s) * ppqPerSample;
                reharmEngine_.recordTransientPpq(transientPpq);
                grooveMapUpdatedFlag_.store(true, std::memory_order_release);
            }
        }
    }
}

float TriLayerReharmAudioProcessor::consumeTransientVisualPulse() noexcept
{
    const float current = transientVisualPulse_.load(std::memory_order_relaxed);
    if (current > 0.001f)
    {
        transientVisualPulse_.store(current * 0.72f, std::memory_order_relaxed);
    }
    return current;
}

bool TriLayerReharmAudioProcessor::consumeGrooveMapUpdatedFlag() noexcept
{
    return grooveMapUpdatedFlag_.exchange(false, std::memory_order_acq_rel);
}

bool TriLayerReharmAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* TriLayerReharmAudioProcessor::createEditor()
{
    return new TriLayerReharmAudioProcessorEditor(*this);
}

void TriLayerReharmAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml != nullptr)
    {
        copyXmlToBinary(*xml, destData);
    }
}

void TriLayerReharmAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts_.state.getType()))
    {
        apvts_.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

} // namespace reharm

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new reharm::TriLayerReharmAudioProcessor();
}
