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

    // Phase 12 & 14: ジャンル選択（8種）、コード進行プリセット選択（各4種＝計32種）、長さ選択（4 Bars / 8 Bars）
    juce::ComboBox genreCombo_;
    juce::ComboBox templateCombo_;
    juce::ComboBox lengthCombo_;
    juce::ComboBox artistCombo_;
    juce::ComboBox environmentCombo_;
    juce::ComboBox modeCombo_;

    // Phase 12: Undo / Redo ボタン
    juce::TextButton undoButton_ { "Undo" };
    juce::TextButton redoButton_ { "Redo" };

    // Phase 8: Mutation Slider (0.0% 〜 100.0%)
    juce::Slider mutationSlider_;

    // Phase 9: Audio Groove Sync Toggle Button
    juce::ToggleButton grooveSyncButton_ { "Groove Sync" };

    // Phase 12: Audition (Preview) 内部シンセ再生トグルボタン
    juce::ToggleButton auditionButton_ { "Audition (Preview)" };

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
    std::unique_ptr<SliderAttachment> mutationAttachment_;
    std::unique_ptr<ButtonAttachment> grooveSyncAttachment_;
    std::unique_ptr<ButtonAttachment> auditionAttachment_;

    ui::TriLayerOrbComponent orbComponent_;
    ui::PianoRollComponent pianoRollComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TriLayerReharmAudioProcessorEditor)
};

} // namespace reharm
