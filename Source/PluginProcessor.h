#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "Engine/MidiReharmonizerEngine.h"
#include "Engine/SimpleAuditionSynth.h"

namespace reharm
{

/**
 * @brief パラメーターID定数群（APVTSとUI・エンジンの紐付け用）
 */
namespace ParamIDs
{
    inline constexpr const char* operatingMode     = "operating_mode";
    inline constexpr const char* genre             = "genre";
    inline constexpr const char* progressionPreset = "progressionPreset";
    inline constexpr const char* progressionLength = "progressionLength";
    inline constexpr const char* artist            = "artist";
    inline constexpr const char* environment       = "environment";
    inline constexpr const char* mutationAmount    = "mutationAmount";
    inline constexpr const char* enableGrooveSync  = "enableGrooveSync";
    inline constexpr const char* enableAudition    = "enableAudition";
}

/**
 * @brief TriLayerReharm プラグインのメインAudioProcessorクラス (Phase 12)
 *
 * - `juce::UndoManager` を APVTS にバインドし、パラメーターおよびプリセット変更の Undo / Redo 履歴管理に対応。
 * - `juce::Synthesiser` (`SimpleAuditionVoice` / `SimpleAuditionSound`) を内蔵し、
 *   Audition (Preview) が ON の場合はリハーモナイズされた MIDI シーケンスを内部シンセで発音してメイン出力へミックスします。
 * - Audition が OFF の場合はオーディオ・MIDIともに完全パススルーを維持します。
 */
class TriLayerReharmAudioProcessor final : public juce::AudioProcessor
{
public:
    TriLayerReharmAudioProcessor();
    ~TriLayerReharmAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    [[nodiscard]] juce::UndoManager& getUndoManager() noexcept { return undoManager_; }
    [[nodiscard]] juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }
    [[nodiscard]] engine::MidiReharmonizerEngine& getEngine() noexcept { return reharmEngine_; }

    /**
     * @brief UIスレッドから呼び出し、直近に検出されたトランジェント強度（0.0f 〜 1.0f）を取得＆減衰させます。
     */
    [[nodiscard]] float consumeTransientVisualPulse() noexcept;

    /**
     * @brief 新しいトランジェントが GrooveMap に追加されたかどうかを取得＆リセットします。
     */
    [[nodiscard]] bool consumeGrooveMapUpdatedFlag() noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /**
     * @brief 入力オーディオバッファからトランジェント（急激なアタック）を検出し、
     *        エンジンの GrooveMap へ PPQ タイムスタンプを記録します。
     */
    void detectTransientsAndRecordGroove(const juce::AudioBuffer<float>& buffer,
                                         const juce::Optional<juce::AudioPlayHead::PositionInfo>& posInfo);

    //==============================================================================
    // Phase 12: Undo / Redo 履歴管理マネージャー（apvts_ より先に初期化する必要があるため先頭に配置）
    juce::UndoManager undoManager_;
    juce::AudioProcessorValueTreeState apvts_;
    engine::MidiReharmonizerEngine reharmEngine_;

    // Phase 12: 内部プレビュー再生（Audition）用軽量ポリフォニック・シンセサイザー
    juce::Synthesiser auditionSynth_;
    double auditionPlayheadPpq_ { 0.0 };
    bool wasAuditionActive_ { false };

    std::atomic<float>* modeParamPtr_ { nullptr };
    std::atomic<float>* grooveSyncParamPtr_ { nullptr };
    std::atomic<float>* auditionParamPtr_ { nullptr };
    std::atomic<float>* lengthParamPtr_ { nullptr };

    // トランジェント検出用デュアル・エンベロープ・フォロワー状態変数
    double sampleRate_ { 44100.0 };
    float fastEnvelope_ { 0.0f };
    float slowEnvelope_ { 0.0f };
    int cooldownSamplesRemaining_ { 0 };

    // processBlock 生存確認ログ出力用サンプルカウンタ（約1秒周期）
    int debugSampleCounter_ { 0 };

    // UI視覚フィードバック用アトミック変数
    std::atomic<float> transientVisualPulse_ { 0.0f };
    std::atomic<bool> grooveMapUpdatedFlag_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TriLayerReharmAudioProcessor)
};

} // namespace reharm
