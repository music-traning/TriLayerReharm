#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace reharm
{

TriLayerReharmAudioProcessor::TriLayerReharmAudioProcessor()
    // Phase 9: オーディオ出力は持たず、オーディオ入力（サイドチェイン/Groove抽出用）のみを受け付けるバス構成
    : AudioProcessor(BusesProperties()
                         .withInput("Audio Groove Input", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "TriLayerReharmParameters", createParameterLayout())
{
    modeParamPtr_       = apvts_.getRawParameterValue(ParamIDs::operatingMode);
    grooveSyncParamPtr_ = apvts_.getRawParameterValue(ParamIDs::enableGrooveSync);

    reharmEngine_.updateRulesFromPresets(0, 0, 0);
    reharmEngine_.triggerOfflineReharmonize(true);
}

juce::AudioProcessorValueTreeState::ParameterLayout TriLayerReharmAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // 動作モード切り替えパラメーター
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::operatingMode, 1 },
        "Operating Mode",
        juce::StringArray { "Bypass (Thru)", "Capture MIDI", "Playback Reharm" },
        0));

    // Layer 1: Genre
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::genre, 1 },
        "Genre",
        juce::StringArray { "Jazz", "Neo-Soul", "City Pop", "Cinematic" },
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
    return 0.0;
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

    reharmEngine_.prepare(sampleRate_, samplesPerBlock);
}

void TriLayerReharmAudioProcessor::releaseResources()
{
    reharmEngine_.reset();
}

bool TriLayerReharmAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto inSet  = layouts.getMainInputChannelSet();
    const auto outSet = layouts.getMainOutputChannelSet();

    // 出力バスが Disabled で、入力（サイドチェイン）が Disabled / Mono / Stereo の構成を優先許可
    const bool validInput = (inSet.isDisabled()
                             || inSet == juce::AudioChannelSet::mono()
                             || inSet == juce::AudioChannelSet::stereo());

    if (outSet.isDisabled() && validInput)
        return true;

    // ホスト側がダミーの出力バスを要求する場合のフォールバック互換性
    if (inSet == outSet && validInput)
        return true;

    return false;
}

void TriLayerReharmAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    if (modeParamPtr_ != nullptr)
    {
        const auto modeIndex = static_cast<int>(modeParamPtr_->load(std::memory_order_relaxed));
        reharmEngine_.setOperatingMode(static_cast<engine::OperatingMode>(modeIndex));
    }

    const bool grooveSyncEnabled = (grooveSyncParamPtr_ != nullptr)
                                   && (grooveSyncParamPtr_->load(std::memory_order_relaxed) >= 0.5f);
    reharmEngine_.setGrooveSyncEnabled(grooveSyncEnabled);

    juce::Optional<juce::AudioPlayHead::PositionInfo> posInfo;
    if (auto* currentPlayHead = getPlayHead())
    {
        posInfo = currentPlayHead->getPosition();
    }

    // 1. 入力オーディオ（サイドチェイン）からトランジェントを検出し GrooveMap を更新
    if (buffer.getNumChannels() > 0 && buffer.getNumSamples() > 0)
    {
        detectTransientsAndRecordGroove(buffer, posInfo);
    }

    // 2. オーディオ出力は行わないためバッファをクリア
    buffer.clear();

    // 3. MIDIブロック処理
    reharmEngine_.processMidiBlock(midiMessages, posInfo, buffer.getNumSamples());
}

void TriLayerReharmAudioProcessor::detectTransientsAndRecordGroove(
    const juce::AudioBuffer<float>& buffer,
    const juce::Optional<juce::AudioPlayHead::PositionInfo>& posInfo)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // 高速エンベロープ（アタック約1.5ms / リリース約25ms）と低速エンベロープ（アタック約25ms / リリース約150ms）
    const float fastAttackCoeff  = std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.0015));
    const float fastReleaseCoeff = std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.0250));
    const float slowAttackCoeff  = std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.0250));
    const float slowReleaseCoeff = std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.1500));

    // チャタリング（1つの打音で複数回反応すること）を防ぐホールドオフ時間（約75ms）
    const int minHoldoffSamples = static_cast<int>(sampleRate_ * 0.075);

    const bool isPlaying = posInfo.hasValue() && posInfo->getIsPlaying();
    const double blockStartPpq = posInfo.hasValue() ? posInfo->getPpqPosition().orFallback(0.0) : 0.0;
    const double bpm           = posInfo.hasValue() ? posInfo->getBpm().orFallback(120.0) : 120.0;
    const double ppqPerSample  = (bpm / 60.0) / sampleRate_;

    constexpr float kNoiseFloor      = 0.025f; // -32dB 相当の最小ノイズゲート
    constexpr float kTransientRatio  = 2.35f;  // 急激な立ち上がり判定比率 (fast / slow)

    for (int s = 0; s < numSamples; ++s)
    {
        // 全入力チャンネルの絶対値ピークを取得
        float absSample = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            absSample = std::max(absSample, std::abs(buffer.getSample(ch, s)));
        }

        // Fast Envelope 更新
        if (absSample > fastEnvelope_)
            fastEnvelope_ = fastAttackCoeff * fastEnvelope_ + (1.0f - fastAttackCoeff) * absSample;
        else
            fastEnvelope_ = fastReleaseCoeff * fastEnvelope_ + (1.0f - fastReleaseCoeff) * absSample;

        // Slow Envelope 更新
        if (absSample > slowEnvelope_)
            slowEnvelope_ = slowAttackCoeff * slowEnvelope_ + (1.0f - slowAttackCoeff) * absSample;
        else
            slowEnvelope_ = slowReleaseCoeff * slowEnvelope_ + (1.0f - slowReleaseCoeff) * absSample;

        if (cooldownSamplesRemaining_ > 0)
        {
            --cooldownSamplesRemaining_;
            continue;
        }

        // トランジェント発火判定
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
        // 緩やかに減衰させる
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
