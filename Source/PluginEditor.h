#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "UI/TriLayerOrbComponent.h"
#include "UI/PianoRollComponent.h"

namespace reharm
{

/**
 * @brief TriLayerReharm プラグインのメインエディタ（GUI）クラス
 *
 * - `juce::DragAndDropContainer` (Phase 5: MIDI D&D Export)
 * - `juce::FileDragAndDropTarget` (Phase 6: MIDI D&D Import)
 * - Progression Template Bank & Real-time Chord Name Display (Phase 7)
 * - Mutation Slider (Phase 8)
 * - Audio Groove Sync トグルボタン ＆ トランジェント連動 LED / Orb バウンス演出 (Phase 9)
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

    void syncEngineAndRefreshViews();
    bool importMidiFileToEngine(const juce::File& midiFileToLoad);
    void loadSelectedProgressionTemplate();

    //==============================================================================
    TriLayerReharmAudioProcessor& processorRef_;
    juce::AudioProcessorValueTreeState& apvtsRef_;

    juce::Label titleLabel_;
    juce::Label templateLabel_;
    juce::Label genreLabel_;
    juce::Label artistLabel_;
    juce::Label environmentLabel_;
    juce::Label mutationLabel_;
    juce::Label modeLabel_;

    juce::ComboBox templateCombo_;
    juce::ComboBox genreCombo_;
    juce::ComboBox artistCombo_;
    juce::ComboBox environmentCombo_;
    juce::ComboBox modeCombo_;

    // Phase 8: Mutation Slider (0.0% 〜 100.0%)
    juce::Slider mutationSlider_;

    // Phase 9: Audio Groove Sync Toggle Button
    juce::ToggleButton grooveSyncButton_ { "Audio Groove Sync (Sidechain)" };

    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAttachment> genreAttachment_;
    std::unique_ptr<ComboAttachment> artistAttachment_;
    std::unique_ptr<ComboAttachment> environmentAttachment_;
    std::unique_ptr<ComboAttachment> modeAttachment_;
    std::unique_ptr<SliderAttachment> mutationAttachment_;
    std::unique_ptr<ButtonAttachment> grooveSyncAttachment_;

    ui::TriLayerOrbComponent orbComponent_;
    ui::PianoRollComponent pianoRollComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TriLayerReharmAudioProcessorEditor)
};

} // namespace reharm
