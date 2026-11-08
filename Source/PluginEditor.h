#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "UI/TriLayerOrbComponent.h"
#include "UI/PianoRollComponent.h"

namespace reharm
{

/**
 * @brief TriLayerReharm プラグインのメインエディタ（GUI）クラス (Phase 12)
 *
 * - 8ジャンル × 4進行（計32種）の2段構えプリセットバンク選択
 * - `juce::UndoManager` と連動した Undo / Redo ボタン ＆ キーボードショートカット (Ctrl+Z / Ctrl+Y / Cmd+Shift+Z)
 * - 内部プレビューシンセ再生用 `Audition (Preview)` トグルボタン
 * - Audio Groove Sync トグルボタン ＆ トランジェント連動 LED / Orb バウンス演出
 * - MIDI D&D Import / Export ＆ リアルタイムコード解析表示
 */
class TriLayerReharmAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 public juce::DragAndDropContainer,
                                                 public juce::FileDragAndDropTarget,
                                                 private juce::AudioProcessorValueTreeState::Listener,
                                                 private juce::AsyncUpdater,
                                                 private juce::Timer
{
public:
    explicit TriLayerReharmAudioProcessorEditor(TriLayerReharmAudioProcessor&);
    ~TriLayerReharmAudioProcessorEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    //==============================================================================
    // juce::FileDragAndDropTarget オーバーライド（Phase 6: D&D Import）
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void updateProgressionComboItemsForGenre(int genreIndex);
    void syncEngineAndRefreshViews();
    bool importMidiFileToEngine(const juce::File& midiFileToLoad);
    void loadSelectedProgressionTemplate();

    void performUndo();
    void performRedo();
    void resetMutationToZeroForLoadedChords();
    void applyLayerSafeSweetSpot();
    void resetToOriginalProgression();

    // v1.5 機能J: お気に入り状態のユーザープリセット保存・読み込み
    void saveUserPresetInteractive();
    void showLoadUserPresetMenu();
    void applyLoadedUserPresetFile(const juce::File& presetFile);

    //==============================================================================
    TriLayerReharmAudioProcessor& processorRef_;
    juce::AudioProcessorValueTreeState& apvtsRef_;

    juce::Label titleLabel_;
    juce::Label genreLabel_;
    juce::Label templateLabel_;
    juce::Label lengthLabel_;
    juce::Label artistLabel_;
    juce::Label environmentLabel_;
    juce::Label mutationLabel_;
    juce::Label modeLabel_;
    juce::Label transposeLabel_;
    juce::Label compingLabel_;
    juce::Label songKeyLabel_;
    juce::Label scaleTypeLabel_;

    // Phase 12 & 14 & v1.5: ジャンル選択（8種）、コード進行プリセット選択（各4種＝計32種）、長さ選択（4 Bars / 8 Bars）、移調選択（-12〜+12）、コンピング＆キー・スケール
    juce::ComboBox genreCombo_;
    juce::ComboBox templateCombo_;
    juce::ComboBox lengthCombo_;
    juce::ComboBox artistCombo_;
    juce::ComboBox environmentCombo_;
    juce::ComboBox modeCombo_;
    juce::ComboBox transposeCombo_;
    juce::ComboBox compingCombo_;
    juce::ComboBox songKeyCombo_;
    juce::ComboBox scaleTypeCombo_;

    // Phase 12 & v1.5: Save / Load / Undo / Redo / Unlock / Layer Safe / Reset / A/B Compare ボタン
    juce::TextButton savePresetButton_ { "Save" };
    juce::TextButton loadPresetButton_ { "Load" };
    juce::TextButton undoButton_ { "Undo" };
    juce::TextButton redoButton_ { "Redo" };
    juce::TextButton unlockAllButton_ { "Unlock" };
    juce::TextButton layerSafeButton_ { "Layer Safe" };
    juce::TextButton resetButton_ { "Reset" };
    juce::TextButton abCompareButton_ { "A/B: Reharm" };
    std::unique_ptr<juce::FileChooser> activeFileChooser_;

    // Phase 8: Mutation Slider (0.0% 〜 100.0%)
    juce::Slider mutationSlider_;

    // Phase 9 & v1.5: トグルボタン群
    juce::ToggleButton grooveSyncButton_ { "Groove Sync" };
    juce::ToggleButton auditionButton_ { "Audition (Preview)" };
    juce::ToggleButton melodyLockButton_ { "Melody Lock" };
    juce::ToggleButton forceDiatonicButton_ { "Force Diatonic" };
    juce::ToggleButton pedalBassButton_ { "Pedal Bass" };
    juce::ToggleButton splitMidiChButton_ { "Split Bass/Chord Ch." };

    int lastKnownGenreIdx_ { -1 };
    int lastKnownPresetIdx_ { -1 };
    int lastKnownLengthIdx_ { -1 };
    bool hasCustomDroppedMidi_ { false };

    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAttachment> genreAttachment_;
    std::unique_ptr<ComboAttachment> lengthAttachment_;
    std::unique_ptr<ComboAttachment> artistAttachment_;
    std::unique_ptr<ComboAttachment> environmentAttachment_;
    std::unique_ptr<ComboAttachment> modeAttachment_;
    std::unique_ptr<ComboAttachment> transposeAttachment_;
    std::unique_ptr<ComboAttachment> compingAttachment_;
    std::unique_ptr<ComboAttachment> songKeyAttachment_;
    std::unique_ptr<ComboAttachment> scaleTypeAttachment_;
    std::unique_ptr<SliderAttachment> mutationAttachment_;
    std::unique_ptr<ButtonAttachment> grooveSyncAttachment_;
    std::unique_ptr<ButtonAttachment> auditionAttachment_;
    std::unique_ptr<ButtonAttachment> melodyLockAttachment_;
    std::unique_ptr<ButtonAttachment> forceDiatonicAttachment_;
    std::unique_ptr<ButtonAttachment> pedalBassAttachment_;
    std::unique_ptr<ButtonAttachment> abCompareAttachment_;
    std::unique_ptr<ButtonAttachment> splitMidiChAttachment_;

    ui::TriLayerOrbComponent orbComponent_;
    ui::PianoRollComponent pianoRollComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TriLayerReharmAudioProcessorEditor)
};

} // namespace reharm
