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
    transposeParamPtr_  = apvts_.getRawParameterValue(ParamIDs::transpose);

    // Phase 12: 軽量ポリフォニック Audition シンセサイザーの初期化（12ボイス）
    engine::setupSimpleAuditionSynth(auditionSynth_, 12);

    // v1.5 機能J: 起動時にユーザープリセット保存フォルダ (<AppData>/TriLayerReharm/Presets) を自動生成
    (void) getUserPresetsDirectory();

    const auto initialSeq = engine::ChordTemplateBank::createPresetSequence(0, 0, false, 960.0);
    reharmEngine_.setSourceMidiSequence(initialSeq, 960.0);
    reharmEngine_.setMutationAmount(0.0f);
    reharmEngine_.setTransposeSemitones(0);
    reharmEngine_.updateRulesFromPresets(0, 3, 0);
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

    // Layer 2: Artist（v1.5 機能D: 達人シミュレーター）
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::artist, 1 },
        "Artist",
        juce::StringArray {
            "Rootless Master (ex. Bill E.)",
            "Quartal Power (ex. McCoy T.)",
            "Melody & Bass (ex. Joe P.)",
            "Standard (Default)"
        },
        3));

    // Layer 3: Environment
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::environment, 1 },
        "Environment",
        juce::StringArray { "Rainy", "VHS", "Deep Space" },
        0));

    // Phase 8 & v1.5: Mutation Amount (0.0% = Basic [初期値], 50.0% = Advanced, 100.0% = Chaos)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParamIDs::mutationAmount, 1 },
        "Mutation Amount",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // v1.5 機能A: Transpose (-12 to +12 半音 / キー選択、デフォルト: index 12 = 0 st)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::transpose, 1 },
        "Transpose",
        juce::StringArray {
            "-12 st (-1 Oct)",
            "-11 st (Db)",
            "-10 st (D)",
            "-9 st (Eb)",
            "-8 st (E)",
            "-7 st (F)",
            "-6 st (Gb)",
            "-5 st (G)",
            "-4 st (Ab)",
            "-3 st (A)",
            "-2 st (Bb)",
            "-1 st (B)",
            "0 st (Original)",
            "+1 st (Db)",
            "+2 st (D)",
            "+3 st (Eb)",
            "+4 st (E)",
            "+5 st (F)",
            "+6 st (Gb)",
            "+7 st (G)",
            "+8 st (Ab)",
            "+9 st (A)",
            "+10 st (Bb)",
            "+11 st (B)",
            "+12 st (+1 Oct)"
        },
        12));

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

    // v1.5 和声の進化1: メロディ絶対維持モード (Melody Lock)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::melodyLock, 1 },
        "Melody Lock",
        false));

    // v1.5 和声の進化2: ダイアトニックスケール拘束 (Key Awareness)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::forceDiatonic, 1 },
        "Force Diatonic",
        false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::songKey, 1 },
        "Song Key",
        juce::StringArray {
            "C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B"
        },
        0));

    // v1.5 機能I: マイナーキー／モード（Dorian, Mixolydian等）のスケール拡張
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::scaleType, 1 },
        "Scale Type",
        juce::StringArray {
            "Major (Ionian)",
            "Natural Minor (Aeolian)",
            "Harmonic Minor",
            "Melodic Minor",
            "Dorian",
            "Mixolydian",
            "Lydian"
        },
        0));

    // v1.5 和声の進化3: ペダルポイント・モード (Pedal Bass)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::pedalBassMode, 1 },
        "Pedal Bass Mode",
        false));

    // v1.5 リズム分割機能: コンピングスタイル (Comping Style)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParamIDs::compingStyle, 1 },
        "Comping Style",
        juce::StringArray {
            juce::String::fromUTF8(reinterpret_cast<const char*>(u8"Sustained(白玉)")),
            "Neo-Soul Pocket",
            "Bossa Bounce"
        },
        0));

    // v1.5 機能F: A/Bコンペア (Original / Reharm 瞬時聴き比べ)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::abCompare, 1 },
        "A/B Compare (Original)",
        false));

    // v1.5 機能H: ベース音(Ch.1)と上声部(Ch.2)のMIDIチャンネル分離出力
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { ParamIDs::splitMidiChannels, 1 },
        "Split Bass/Chord Ch.",
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

    if (transposeParamPtr_ != nullptr)
    {
        const int transposeIdx = static_cast<int>(std::round(transposeParamPtr_->load(std::memory_order_relaxed)));
        reharmEngine_.setTransposeSemitones(std::clamp(transposeIdx - 12, -12, 12));
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
    const bool hostPlaying = posInfo.hasValue() && posInfo->getIsPlaying();
    if (hostPlaying)
    {
        currentPlayheadPpq_.store(std::max(0.0, posInfo->getPpqPosition().orFallback(0.0)), std::memory_order_relaxed);
        isTransportPlaying_.store(true, std::memory_order_relaxed);
    }
    else if (!auditionEnabled)
    {
        if (posInfo.hasValue() && posInfo->getPpqPosition().hasValue())
            currentPlayheadPpq_.store(std::max(0.0, *posInfo->getPpqPosition()), std::memory_order_relaxed);
        isTransportPlaying_.store(false, std::memory_order_relaxed);
    }

    if (auditionEnabled && numOutChannels > 0 && numSamples > 0)
    {
        const bool isEightBars = (lengthParamPtr_ != nullptr)
                                 && (lengthParamPtr_->load(std::memory_order_relaxed) >= 0.5f);
        const double kLoopLengthPpq = isEightBars ? 32.0 : 16.0;
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

        currentPlayheadPpq_.store(currentStartPpq, std::memory_order_relaxed);
        isTransportPlaying_.store(true, std::memory_order_relaxed);

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
    if (auto xml = createFullPresetXml())
    {
        copyXmlToBinary(*xml, destData);
    }
}

void TriLayerReharmAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr)
    {
        (void) restoreFromPresetXml(*xmlState);
    }
}

juce::File TriLayerReharmAudioProcessor::getUserPresetsDirectory()
{
    const auto presetsDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                .getChildFile("TriLayerReharm")
                                .getChildFile("Presets");
    if (!presetsDir.isDirectory())
    {
        (void) presetsDir.createDirectory();
    }
    return presetsDir;
}

std::unique_ptr<juce::XmlElement> TriLayerReharmAudioProcessor::createFullPresetXml()
{
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml != nullptr)
    {
        xml->setAttribute("pluginVersion", "1.5.0");
        xml->setAttribute("hasCustomDroppedMidi", hasCustomDroppedMidi_);
        xml->setAttribute("customSourceLabel", customSourceLabel_);

        if (auto engineStateXml = reharmEngine_.createEngineStateXml())
        {
            xml->addChildElement(engineStateXml.release());
        }
    }
    return xml;
}

bool TriLayerReharmAudioProcessor::restoreFromPresetXml(const juce::XmlElement& presetXml)
{
    if (!presetXml.hasTagName(apvts_.state.getType()))
        return false;

    juce::XmlElement xmlCopy(presetXml);

    hasCustomDroppedMidi_ = xmlCopy.getBoolAttribute("hasCustomDroppedMidi", false);
    customSourceLabel_    = xmlCopy.getStringAttribute("customSourceLabel", {});

    // 1. <EngineState>（capturedSequence_ と Lock状態）を切り出してから APVTS を復元
    std::unique_ptr<juce::XmlElement> engineStateXml;
    if (auto* engineChild = xmlCopy.getChildByName("EngineState"))
    {
        engineStateXml = std::make_unique<juce::XmlElement>(*engineChild);
        xmlCopy.removeChildElement(engineChild, true);
    }

    apvts_.replaceState(juce::ValueTree::fromXml(xmlCopy));

    // 2. エンジン内部の capturedSequence_（インポート済み元MIDI）と Lock（ピン留め）状態を復元
    if (engineStateXml != nullptr)
    {
        reharmEngine_.restoreEngineStateFromXml(*engineStateXml);
    }
    else
    {
        reharmEngine_.clearLockedChordBlocks();
    }

    // 3. 復元された APVTS パラメーターをエンジンへ同期し、ヘッドレス再計算を実行
    int modeIdx      = 0;
    int genreIdx     = 0;
    int presetIdx    = 0;
    int lengthIdx    = 0;
    int artistIdx    = 3;
    int envIdx       = 0;
    int transposeIdx   = 12;
    float mutation     = 0.0f;
    bool grooveSync    = false;
    bool melodyLock    = false;
    bool forceDiatonic = false;
    int songKeyIdx     = 0;
    int scaleTypeIdx   = 0;
    bool pedalBass     = false;
    int compingIdx     = 0;
    bool abCompareOrig = false;
    bool splitMidiCh   = false;

    if (auto* p = apvts_.getRawParameterValue(ParamIDs::operatingMode))
        modeIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::genre))
        genreIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::progressionPreset))
        presetIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::progressionLength))
        lengthIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::artist))
        artistIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::environment))
        envIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::transpose))
        transposeIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::mutationAmount))
        mutation = p->load();
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::enableGrooveSync))
        grooveSync = (p->load() >= 0.5f);
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::melodyLock))
        melodyLock = (p->load() >= 0.5f);
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::forceDiatonic))
        forceDiatonic = (p->load() >= 0.5f);
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::songKey))
        songKeyIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::scaleType))
        scaleTypeIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::pedalBassMode))
        pedalBass = (p->load() >= 0.5f);
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::compingStyle))
        compingIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::abCompare))
        abCompareOrig = (p->load() >= 0.5f);
    if (auto* p = apvts_.getRawParameterValue(ParamIDs::splitMidiChannels))
        splitMidiCh = (p->load() >= 0.5f);

    reharmEngine_.setOperatingMode(static_cast<engine::OperatingMode>(std::clamp(modeIdx, 0, 2)));
    reharmEngine_.setMutationAmount(mutation);
    reharmEngine_.setTransposeSemitones(std::clamp(transposeIdx - 12, -12, 12));
    reharmEngine_.setGrooveSyncEnabled(grooveSync);
    reharmEngine_.setMelodyLockEnabled(melodyLock);
    reharmEngine_.setForceDiatonicEnabled(forceDiatonic);
    reharmEngine_.setSongKeyPitchClass(songKeyIdx);
    reharmEngine_.setScaleType(static_cast<engine::ScaleType>(std::clamp(scaleTypeIdx, 0, 6)));
    reharmEngine_.setPedalBassModeEnabled(pedalBass);
    reharmEngine_.setCompingStyle(static_cast<engine::CompingStyle>(std::clamp(compingIdx, 0, 2)));
    reharmEngine_.setCompareOriginalMode(abCompareOrig);
    reharmEngine_.setSplitMidiChannelsEnabled(splitMidiCh);
    reharmEngine_.updateRulesFromPresets(genreIdx, artistIdx, envIdx);

    // 旧バージョンの状態データ等で capturedSequence_ が空だった場合のフォールバック
    if (!reharmEngine_.hasCapturedSequence() && !hasCustomDroppedMidi_)
    {
        const bool isEightBars = (lengthIdx == 1);
        const auto presetSeq = engine::ChordTemplateBank::createPresetSequence(
            genreIdx, presetIdx, isEightBars, 960.0);
        reharmEngine_.setSourceMidiSequence(presetSeq, 960.0);
    }

    // UI（Editor）を開かなくても正しい設定・移調・ロック状態でMIDIが出力されるようヘッドレス再計算を実行
    reharmEngine_.triggerOfflineReharmonize(false);
    return true;
}

bool TriLayerReharmAudioProcessor::saveUserPresetToFile(const juce::File& targetFile)
{
    (void) getUserPresetsDirectory();

    auto xml = createFullPresetXml();
    if (xml == nullptr)
        return false;

    if (const auto parentDir = targetFile.getParentDirectory(); !parentDir.isDirectory())
    {
        (void) parentDir.createDirectory();
    }

    return xml->writeTo(targetFile);
}

bool TriLayerReharmAudioProcessor::loadUserPresetFromFile(const juce::File& sourceFile)
{
    if (!sourceFile.existsAsFile())
        return false;

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(sourceFile));
    if (xml == nullptr)
        return false;

    return restoreFromPresetXml(*xml);
}

} // namespace reharm

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new reharm::TriLayerReharmAudioProcessor();
}
