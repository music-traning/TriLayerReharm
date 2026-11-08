#include "PluginEditor.h"
#include "Engine/ChordTemplateBank.h"

namespace reharm
{

namespace
{
    void configureComboBox(juce::ComboBox& box,
                           juce::Label& label,
                           const juce::String& labelText,
                           const juce::AudioProcessorValueTreeState& apvts,
                           const char* paramID)
    {
        label.setText(labelText, juce::dontSendNotification);
        label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colour(0xff94a3b8));

        if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramID)))
        {
            box.addItemList(choiceParam->choices, 1);
        }

        box.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff181c26));
        box.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2e3446));
        box.setColour(juce::ComboBox::textColourId, juce::Colours::white);
        box.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff94a3b8));
    }

    void styleActionButton(juce::TextButton& btn)
    {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e2433));
        btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff334155));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe2e8f0));
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }

    /**
     * @brief Reset ボタンや Unlock 操作時に、エンジンの Lock（ピン留め）状態を
     *        APVTS の UndoManager トランザクションと連動して Undo / Redo 復元するためのアクションクラス
     */
    class LockedBlocksUndoAction final : public juce::UndoableAction
    {
    public:
        LockedBlocksUndoAction(engine::MidiReharmonizerEngine& engineRef,
                               std::vector<engine::ChordBlock> beforeState,
                               std::vector<engine::ChordBlock> afterState)
            : engine_(engineRef),
              beforeLocks_(std::move(beforeState)),
              afterLocks_(std::move(afterState))
        {
        }

        bool perform() override
        {
            engine_.setLockedChordBlocksSnapshot(afterLocks_);
            return true;
        }

        bool undo() override
        {
            engine_.setLockedChordBlocksSnapshot(beforeLocks_);
            return true;
        }

        int getSizeInUnits() override { return 1; }

    private:
        engine::MidiReharmonizerEngine& engine_;
        std::vector<engine::ChordBlock> beforeLocks_;
        std::vector<engine::ChordBlock> afterLocks_;
    };
} // anonymous namespace

TriLayerReharmAudioProcessorEditor::TriLayerReharmAudioProcessorEditor(TriLayerReharmAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processorRef_(p),
      apvtsRef_(p.getAPVTS())
{
    setWantsKeyboardFocus(true);

    titleLabel_.setText("TRI-LAYER 1.5", juce::dontSendNotification);
    titleLabel_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    // Phase 12 & v1.5: Save / Load / Undo / Redo / Unlock / A/B Compare ボタンの構築
    styleActionButton(savePresetButton_);
    styleActionButton(loadPresetButton_);
    savePresetButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff34d399));
    loadPresetButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff38bdf8));
    savePresetButton_.setTooltip("Save User Preset: Save current parameters, MIDI progression, and pinned chord blocks to XML preset");
    loadPresetButton_.setTooltip("Load User Preset: Load saved XML preset from TriLayerReharm/Presets or browse file");
    savePresetButton_.onClick = [this]() { saveUserPresetInteractive(); };
    loadPresetButton_.onClick = [this]() { showLoadUserPresetMenu(); };
    addAndMakeVisible(savePresetButton_);
    addAndMakeVisible(loadPresetButton_);

    styleActionButton(undoButton_);
    styleActionButton(redoButton_);
    styleActionButton(unlockAllButton_);
    unlockAllButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfffbbf24));
    undoButton_.setTooltip("Undo (Ctrl+Z / Cmd+Z)");
    redoButton_.setTooltip("Redo (Ctrl+Y / Cmd+Shift+Z)");
    unlockAllButton_.setTooltip("Unlock all pinned chord blocks");
    undoButton_.onClick = [this]() { performUndo(); };
    redoButton_.onClick = [this]() { performRedo(); };
    unlockAllButton_.onClick = [this]()
    {
        auto& engine = processorRef_.getEngine();
        if (engine.hasLockedChordBlocks())
        {
            processorRef_.beginUndoTransaction("Unlock All Chord Blocks");
            auto beforeLocks = engine.getLockedChordBlocksSnapshot();
            processorRef_.getUndoManager().perform(
                new LockedBlocksUndoAction(engine, std::move(beforeLocks), {}));
            (void) apvtsRef_.copyState();
        }
        else
        {
            engine.clearLockedChordBlocks();
        }
        syncEngineAndRefreshViews();
    };
    addAndMakeVisible(undoButton_);
    addAndMakeVisible(redoButton_);
    addAndMakeVisible(unlockAllButton_);

    // v1.5 機能F: A/Bコンペア (Original / Reharm) トグルボタン
    abCompareButton_.setClickingTogglesState(true);
    abCompareButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e2433));
    abCompareButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0284c7));
    abCompareButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff38bdf8));
    abCompareButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    abCompareButton_.setTooltip("A/B Compare: Toggle between Original MIDI bypass (Mutation=0% equivalent) and Reharm output while keeping current settings");
    abCompareButton_.onClick = [this]()
    {
        processorRef_.beginUndoTransaction("Toggle A/B Compare");
    };
    addAndMakeVisible(abCompareButton_);

    // 1段目: Layer 1 Genre 選択（8ジャンル）および各セレクター
    configureComboBox(genreCombo_,       genreLabel_,       "LAYER 1 : GENRE (8 GENRES)",         apvtsRef_, ParamIDs::genre);
    configureComboBox(lengthCombo_,      lengthLabel_,      "LENGTH",                             apvtsRef_, ParamIDs::progressionLength);
    configureComboBox(artistCombo_,      artistLabel_,      "LAYER 2 : ARTIST (ORBIT)",           apvtsRef_, ParamIDs::artist);
    configureComboBox(environmentCombo_, environmentLabel_, "LAYER 3 : ENVIRONMENT (ATMOSPHERE)", apvtsRef_, ParamIDs::environment);
    configureComboBox(modeCombo_,        modeLabel_,        "MODE",                               apvtsRef_, ParamIDs::operatingMode);
    configureComboBox(transposeCombo_,   transposeLabel_,   "TRANSPOSE",                          apvtsRef_, ParamIDs::transpose);
    configureComboBox(compingCombo_,     compingLabel_,     "COMPING STYLE (RHYTHM)",             apvtsRef_, ParamIDs::compingStyle);
    configureComboBox(songKeyCombo_,     songKeyLabel_,     "KEY",                                apvtsRef_, ParamIDs::songKey);
    configureComboBox(scaleTypeCombo_,   scaleTypeLabel_,   "SCALE / MODE",                       apvtsRef_, ParamIDs::scaleType);

    lengthLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
    lengthCombo_.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff38bdf8).withAlpha(0.55f));
    lengthCombo_.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff38bdf8));

    transposeLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffa78bfa));
    transposeCombo_.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xffa78bfa).withAlpha(0.55f));
    transposeCombo_.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffa78bfa));

    compingLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff2dd4bf));
    compingCombo_.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2dd4bf).withAlpha(0.55f));
    compingCombo_.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff2dd4bf));

    songKeyLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
    songKeyCombo_.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff38bdf8).withAlpha(0.55f));
    songKeyCombo_.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff38bdf8));

    scaleTypeLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
    scaleTypeCombo_.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff38bdf8).withAlpha(0.55f));
    scaleTypeCombo_.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff38bdf8));
    scaleTypeCombo_.setTooltip("Scale / Mode for Force Diatonic: Major (Ionian), Natural Minor (Aeolian), Harmonic Minor, Melodic Minor, Dorian, Mixolydian, Lydian");

    // 2段目: 選択中ジャンルのコード進行プリセット選択（各4種＝計32種）＋ 長さ（4 Bars / 8 Bars）
    templateLabel_.setText("PROGRESSION PRESET (32 BANK)", juce::dontSendNotification);
    templateLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    templateLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
    addAndMakeVisible(templateLabel_);
    addAndMakeVisible(lengthLabel_);

    templateCombo_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff181c26));
    templateCombo_.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff38bdf8).withAlpha(0.55f));
    templateCombo_.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    templateCombo_.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff38bdf8));
    templateCombo_.onChange = [this]()
    {
        const int selectedIdx = std::clamp(templateCombo_.getSelectedItemIndex(), 0, 3);
        if (auto* presetParam = apvtsRef_.getParameter(ParamIDs::progressionPreset))
        {
            const int currentParamIdx = static_cast<int>(std::round(
                apvtsRef_.getRawParameterValue(ParamIDs::progressionPreset)->load()));
            if (selectedIdx != currentParamIdx)
            {
                processorRef_.beginUndoTransaction("Select Progression Preset");
                presetParam->setValueNotifyingHost(presetParam->convertTo0to1(static_cast<float>(selectedIdx)));
                (void) apvtsRef_.copyState();
            }
        }
        hasCustomDroppedMidi_ = false;
        processorRef_.getEngine().clearLockedChordBlocks();
        resetMutationToZeroForLoadedChords();
        loadSelectedProgressionTemplate();
    };
    addAndMakeVisible(templateCombo_);
    addAndMakeVisible(lengthCombo_);

    addAndMakeVisible(genreLabel_);
    addAndMakeVisible(genreCombo_);
    addAndMakeVisible(artistLabel_);
    addAndMakeVisible(artistCombo_);
    addAndMakeVisible(environmentLabel_);
    addAndMakeVisible(environmentCombo_);
    addAndMakeVisible(modeLabel_);
    addAndMakeVisible(modeCombo_);
    addAndMakeVisible(transposeLabel_);
    addAndMakeVisible(transposeCombo_);
    addAndMakeVisible(compingLabel_);
    addAndMakeVisible(compingCombo_);
    addAndMakeVisible(songKeyLabel_);
    addAndMakeVisible(songKeyCombo_);
    addAndMakeVisible(scaleTypeLabel_);
    addAndMakeVisible(scaleTypeCombo_);

    // Phase 8: Mutation Slider の構築（操作開始直前に Undo トランザクションを開始）
    mutationLabel_.setText("MUTATION (0% BASIC  |  50% ADV  |  100% CHAOS)", juce::dontSendNotification);
    mutationLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    mutationLabel_.setColour(juce::Label::textColourId, juce::Colour(0xfff43f5e));
    addAndMakeVisible(mutationLabel_);

    mutationSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    mutationSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
    mutationSlider_.setTextValueSuffix(" %");
    mutationSlider_.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff181c26));
    mutationSlider_.setColour(juce::Slider::trackColourId, juce::Colour(0xfff43f5e));
    mutationSlider_.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    mutationSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    mutationSlider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff181c26));
    mutationSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff2e3446));
    mutationSlider_.onDragStart = [this]()
    {
        processorRef_.beginUndoTransaction("Change Mutation Slider");
    };
    mutationSlider_.onDragEnd = [this]()
    {
        (void) apvtsRef_.copyState();
    };
    addAndMakeVisible(mutationSlider_);

    // v1.5 機能E: "Layer Safe (Sweet Spot)" 一発セットアップボタン（Mutationスライダーの右横に配置）
    layerSafeButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff132a2e));
    layerSafeButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0d9488));
    layerSafeButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff2dd4bf));
    layerSafeButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    layerSafeButton_.setTooltip("Layer Safe (Sweet Spot): Snap to City Pop + 32% Mutation for collision-free chord layering");
    layerSafeButton_.onClick = [this]()
    {
        applyLayerSafeSweetSpot();
    };
    addAndMakeVisible(layerSafeButton_);

    // v1.5 機能G: 安全な "Reset" ボタン（Layer Safeの隣に配置、Undo保護付きで初期コード進行へ復帰）
    resetButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a161e));
    resetButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe11d48));
    resetButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfffda4af));
    resetButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    resetButton_.setTooltip("Reset to Original Chords: Reset Mutation to 0%, Transpose to 0st, and unlock all blocks (Undo-safe)");
    resetButton_.onClick = [this]()
    {
        resetToOriginalProgression();
    };
    addAndMakeVisible(resetButton_);

    // Phase 9: Audio Groove Sync トグルボタン
    grooveSyncButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff34d399));
    grooveSyncButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff10b981));
    grooveSyncButton_.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff64748b));
    grooveSyncButton_.onClick = [this]()
    {
        processorRef_.beginUndoTransaction("Toggle Groove Sync");
    };
    addAndMakeVisible(grooveSyncButton_);

    // Phase 12: Audition (Preview) トグルボタン
    auditionButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfffbbf24));
    auditionButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xfff59e0b));
    auditionButton_.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff64748b));
    auditionButton_.onClick = [this]()
    {
        processorRef_.beginUndoTransaction("Toggle Audition Synth");
    };
    addAndMakeVisible(auditionButton_);

    // v1.5 和声の進化（既存トラックとケンカしない3機能）トグルボタン群
    melodyLockButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff472b6));
    melodyLockButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffec4899));
    melodyLockButton_.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff64748b));
    melodyLockButton_.setTooltip("Melody Lock: Keep original highest note strictly locked as top voice");
    melodyLockButton_.onClick = [this]() { processorRef_.beginUndoTransaction("Toggle Melody Lock"); };
    addAndMakeVisible(melodyLockButton_);

    forceDiatonicButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff38bdf8));
    forceDiatonicButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff0284c7));
    forceDiatonicButton_.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff64748b));
    forceDiatonicButton_.setTooltip("Force Diatonic: Filter out non-diatonic & avoid notes for selected Scale Key");
    forceDiatonicButton_.onClick = [this]() { processorRef_.beginUndoTransaction("Toggle Force Diatonic"); };
    addAndMakeVisible(forceDiatonicButton_);

    pedalBassButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffc084fc));
    pedalBassButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff9333ea));
    pedalBassButton_.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff64748b));
    pedalBassButton_.setTooltip("Pedal Bass: Lock lowest bass voice across entire progression");
    pedalBassButton_.onClick = [this]() { processorRef_.beginUndoTransaction("Toggle Pedal Bass"); };
    addAndMakeVisible(pedalBassButton_);

    // v1.5 機能H: ベース音(Ch.1)と上声部(Ch.2)のMIDIチャンネル分離出力トグルボタン
    splitMidiChButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff67e8f9));
    splitMidiChButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff06b6d4));
    splitMidiChButton_.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff64748b));
    splitMidiChButton_.setTooltip("Split Bass/Chord Ch.: Output lowest Bass note on MIDI Ch.1 and Upper Chords on MIDI Ch.2");
    splitMidiChButton_.onClick = [this]() { processorRef_.beginUndoTransaction("Toggle Split Bass/Chord Ch."); };
    addAndMakeVisible(splitMidiChButton_);

    // APVTS アタッチメントによる双方向バインディング
    genreAttachment_         = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::genre,             genreCombo_);
    lengthAttachment_        = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::progressionLength, lengthCombo_);
    artistAttachment_        = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::artist,            artistCombo_);
    environmentAttachment_   = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::environment,       environmentCombo_);
    modeAttachment_          = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::operatingMode,     modeCombo_);
    transposeAttachment_     = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::transpose,         transposeCombo_);
    compingAttachment_       = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::compingStyle,      compingCombo_);
    songKeyAttachment_       = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::songKey,           songKeyCombo_);
    scaleTypeAttachment_     = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::scaleType,         scaleTypeCombo_);
    mutationAttachment_      = std::make_unique<SliderAttachment>(apvtsRef_, ParamIDs::mutationAmount,   mutationSlider_);
    grooveSyncAttachment_    = std::make_unique<ButtonAttachment>(apvtsRef_, ParamIDs::enableGrooveSync, grooveSyncButton_);
    auditionAttachment_      = std::make_unique<ButtonAttachment>(apvtsRef_, ParamIDs::enableAudition,   auditionButton_);
    melodyLockAttachment_    = std::make_unique<ButtonAttachment>(apvtsRef_, ParamIDs::melodyLock,       melodyLockButton_);
    forceDiatonicAttachment_ = std::make_unique<ButtonAttachment>(apvtsRef_, ParamIDs::forceDiatonic,    forceDiatonicButton_);
    pedalBassAttachment_     = std::make_unique<ButtonAttachment>(apvtsRef_, ParamIDs::pedalBassMode,    pedalBassButton_);
    abCompareAttachment_     = std::make_unique<ButtonAttachment>(apvtsRef_, ParamIDs::abCompare,        abCompareButton_);
    splitMidiChAttachment_   = std::make_unique<ButtonAttachment>(apvtsRef_, ParamIDs::splitMidiChannels, splitMidiChButton_);

    addAndMakeVisible(orbComponent_);
    addAndMakeVisible(pianoRollComponent_);

    // PianoRollComponent へ直接ファイルがドロップされた場合も Editor の filesDropped へ統合転送
    pianoRollComponent_.onFilesDroppedCallback = [this](const juce::StringArray& droppedFiles)
    {
        filesDropped(droppedFiles, 0, 0);
    };

    // v1.5 機能B: PianoRollComponent 上でコードブロックが左クリックされた際にLockをトグル
    pianoRollComponent_.onChordBlockLockToggled = [this](size_t blockIndex)
    {
        processorRef_.getEngine().toggleChordBlockLockByIndex(blockIndex);
        unlockAllButton_.setEnabled(processorRef_.getEngine().hasLockedChordBlocks());
        pianoRollComponent_.setChordBlockViews(processorRef_.getEngine().getChordBlockViews());
    };

    // v1.5 UI強化2: PianoRollComponent 上でマウスホイールまたは右クリックされた際にボイシングの展開形/オクターブを手動シフト＆即時再計算
    pianoRollComponent_.onChordBlockVoicingShifted = [this](size_t blockIndex, int inversionSteps, int octaveShift)
    {
        auto& engine = processorRef_.getEngine();
        if (engine.shiftChordBlockVoicing(blockIndex, inversionSteps, octaveShift))
        {
            unlockAllButton_.setEnabled(engine.hasLockedChordBlocks());
            pianoRollComponent_.setMidiSequence(engine.getReharmonizedMidiSequence());
            pianoRollComponent_.setGhostMidiSequence(engine.getCapturedMidiSequence());
            pianoRollComponent_.setChordBlockViews(engine.getChordBlockViews());
        }
    };

    // APVTS パラメーターリスナーの登録
    apvtsRef_.addParameterListener(ParamIDs::genre, this);
    apvtsRef_.addParameterListener(ParamIDs::progressionPreset, this);
    apvtsRef_.addParameterListener(ParamIDs::progressionLength, this);
    apvtsRef_.addParameterListener(ParamIDs::artist, this);
    apvtsRef_.addParameterListener(ParamIDs::environment, this);
    apvtsRef_.addParameterListener(ParamIDs::operatingMode, this);
    apvtsRef_.addParameterListener(ParamIDs::transpose, this);
    apvtsRef_.addParameterListener(ParamIDs::mutationAmount, this);
    apvtsRef_.addParameterListener(ParamIDs::enableGrooveSync, this);
    apvtsRef_.addParameterListener(ParamIDs::enableAudition, this);
    apvtsRef_.addParameterListener(ParamIDs::melodyLock, this);
    apvtsRef_.addParameterListener(ParamIDs::forceDiatonic, this);
    apvtsRef_.addParameterListener(ParamIDs::songKey, this);
    apvtsRef_.addParameterListener(ParamIDs::scaleType, this);
    apvtsRef_.addParameterListener(ParamIDs::pedalBassMode, this);
    apvtsRef_.addParameterListener(ParamIDs::compingStyle, this);
    apvtsRef_.addParameterListener(ParamIDs::abCompare, this);
    apvtsRef_.addParameterListener(ParamIDs::splitMidiChannels, this);

    setResizable(true, true);
    setResizeLimits(720, 580, 1400, 1000);
    setSize(900, 640);

    const int initialGenreIdx = static_cast<int>(std::round(
        apvtsRef_.getRawParameterValue(ParamIDs::genre)->load()));
    lastKnownLengthIdx_   = static_cast<int>(std::round(
        apvtsRef_.getRawParameterValue(ParamIDs::progressionLength)->load()));
    hasCustomDroppedMidi_ = processorRef_.hasCustomSourceMidi();

    updateProgressionComboItemsForGenre(initialGenreIdx);

    if (processorRef_.getEngine().hasCapturedSequence())
    {
        if (hasCustomDroppedMidi_)
        {
            const auto& savedLabel = processorRef_.getCustomSourceLabel();
            pianoRollComponent_.setSourceLabelText(
                savedLabel.isNotEmpty() ? savedLabel : "SOURCE: Restored MIDI");
        }
        else
        {
            const bool isEightBars = (lastKnownLengthIdx_ == 1);
            pianoRollComponent_.setSourceLabelText(
                "PRESET (" + juce::String(isEightBars ? "8 BARS" : "4 BARS") + "): ["
                + genreCombo_.getText() + "] " + templateCombo_.getText()
            );
        }
        syncEngineAndRefreshViews();
    }
    else
    {
        loadSelectedProgressionTemplate();
    }

    // トランジェント視覚フィードバック＆Undo/Redoボタン状態監視用タイマー開始 (30Hz)
    startTimerHz(30);
}

TriLayerReharmAudioProcessorEditor::~TriLayerReharmAudioProcessorEditor()
{
    stopTimer();
    apvtsRef_.removeParameterListener(ParamIDs::genre, this);
    apvtsRef_.removeParameterListener(ParamIDs::progressionPreset, this);
    apvtsRef_.removeParameterListener(ParamIDs::progressionLength, this);
    apvtsRef_.removeParameterListener(ParamIDs::artist, this);
    apvtsRef_.removeParameterListener(ParamIDs::environment, this);
    apvtsRef_.removeParameterListener(ParamIDs::operatingMode, this);
    apvtsRef_.removeParameterListener(ParamIDs::transpose, this);
    apvtsRef_.removeParameterListener(ParamIDs::mutationAmount, this);
    apvtsRef_.removeParameterListener(ParamIDs::enableGrooveSync, this);
    apvtsRef_.removeParameterListener(ParamIDs::enableAudition, this);
    apvtsRef_.removeParameterListener(ParamIDs::melodyLock, this);
    apvtsRef_.removeParameterListener(ParamIDs::forceDiatonic, this);
    apvtsRef_.removeParameterListener(ParamIDs::songKey, this);
    apvtsRef_.removeParameterListener(ParamIDs::scaleType, this);
    apvtsRef_.removeParameterListener(ParamIDs::pedalBassMode, this);
    apvtsRef_.removeParameterListener(ParamIDs::compingStyle, this);
    apvtsRef_.removeParameterListener(ParamIDs::abCompare, this);
    apvtsRef_.removeParameterListener(ParamIDs::splitMidiChannels, this);
    cancelPendingUpdate();
}

void TriLayerReharmAudioProcessorEditor::resetMutationToZeroForLoadedChords()
{
    if (auto* mutParam = apvtsRef_.getParameter(ParamIDs::mutationAmount))
    {
        mutParam->setValueNotifyingHost(mutParam->convertTo0to1(0.0f));
    }
    processorRef_.getEngine().setMutationAmount(0.0f);
}

void TriLayerReharmAudioProcessorEditor::applyLayerSafeSweetSpot()
{
    processorRef_.beginUndoTransaction("Apply Layer Safe (Sweet Spot)");

    // A/BコンペアがOriginal側になっている場合はReharm側へ戻す
    if (auto* abParam = apvtsRef_.getParameter(ParamIDs::abCompare))
    {
        abParam->setValueNotifyingHost(0.0f);
    }

    // 1. Genre を "City Pop" (index 2: allowTritoneSub == false、裏コードを封印しナチュラルテンションのみ付加) にセット
    if (auto* genreParam = apvtsRef_.getParameter(ParamIDs::genre))
    {
        genreParam->setValueNotifyingHost(genreParam->convertTo0to1(2.0f));
    }

    // 2. Mutation Amount を 32.0% (30.0%〜35.0%付近の黄金比：過剰な拍分割を防ぎ安全な手数に抑える) にセット
    if (auto* mutParam = apvtsRef_.getParameter(ParamIDs::mutationAmount))
    {
        mutParam->setValueNotifyingHost(mutParam->convertTo0to1(32.0f));
    }

    (void) apvtsRef_.copyState();
    syncEngineAndRefreshViews();
}

void TriLayerReharmAudioProcessorEditor::resetToOriginalProgression()
{
    // 機能G: UndoManager (beginNewTransaction) を経由して安全に初期状態へリセット（誤操作時もUndoで復元可能）
    processorRef_.beginUndoTransaction("Reset to Original Progression");

    auto& engine = processorRef_.getEngine();
    if (engine.hasLockedChordBlocks())
    {
        auto beforeLocks = engine.getLockedChordBlocksSnapshot();
        processorRef_.getUndoManager().perform(
            new LockedBlocksUndoAction(engine, std::move(beforeLocks), {}));
    }
    else
    {
        engine.clearLockedChordBlocks();
    }

    // 1. Mutation を 0.0% にリセット（機能Fの原曲完全再現バイパスが発動）
    if (auto* mutParam = apvtsRef_.getParameter(ParamIDs::mutationAmount))
    {
        mutParam->setValueNotifyingHost(mutParam->convertTo0to1(0.0f));
    }

    // 2. Transpose などの移調設定を 0 st (Original = index 12) に戻す
    if (auto* transParam = apvtsRef_.getParameter(ParamIDs::transpose))
    {
        transParam->setValueNotifyingHost(transParam->convertTo0to1(12.0f));
    }

    // 3. A/B Compare トグルを通常状態 (Reharm = false) に戻す
    if (auto* abParam = apvtsRef_.getParameter(ParamIDs::abCompare))
    {
        abParam->setValueNotifyingHost(0.0f);
    }

    (void) apvtsRef_.copyState();
    syncEngineAndRefreshViews();
}

void TriLayerReharmAudioProcessorEditor::performUndo()
{
    if (processorRef_.performUndo())
    {
        syncEngineAndRefreshViews();
    }
}

void TriLayerReharmAudioProcessorEditor::performRedo()
{
    if (processorRef_.performRedo())
    {
        syncEngineAndRefreshViews();
    }
}

void TriLayerReharmAudioProcessorEditor::saveUserPresetInteractive()
{
    const juce::File presetsDir = TriLayerReharmAudioProcessor::getUserPresetsDirectory();

    // 既存プリセット数から連番付きの初期ファイル名（例: Preset_01_Jazz.xml）を自動生成
    juce::Array<juce::File> existingFiles;
    presetsDir.findChildFiles(existingFiles, juce::File::findFiles, false, "*.xml;*.tlrpreset");
    const int nextIndex = existingFiles.size() + 1;

    juce::String genreShort = genreCombo_.getText().retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
    if (genreShort.isEmpty())
        genreShort = "Reharm";

    const juce::String defaultFileName = juce::String::formatted("Preset_%02d_", nextIndex) + genreShort + ".xml";
    const juce::File defaultTargetFile = presetsDir.getChildFile(defaultFileName);

    activeFileChooser_ = std::make_unique<juce::FileChooser>(
        "Save TriLayerReharm User Preset",
        defaultTargetFile,
        "*.xml;*.tlrpreset");

    constexpr int saveFlags = juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting;

    juce::Component::SafePointer<TriLayerReharmAudioProcessorEditor> safeThis(this);
    activeFileChooser_->launchAsync(saveFlags, [safeThis](const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        juce::File target = chooser.getResult();
        if (target == juce::File {})
            return;

        if (!target.hasFileExtension(".xml") && !target.hasFileExtension(".tlrpreset"))
        {
            target = target.withFileExtension(".xml");
        }

        if (safeThis->processorRef_.saveUserPresetToFile(target))
        {
            safeThis->pianoRollComponent_.setSourceLabelText(
                "SAVED PRESET: " + target.getFileNameWithoutExtension());
        }
    });
}

void TriLayerReharmAudioProcessorEditor::showLoadUserPresetMenu()
{
    const juce::File presetsDir = TriLayerReharmAudioProcessor::getUserPresetsDirectory();

    juce::Array<juce::File> presetFiles;
    presetsDir.findChildFiles(presetFiles, juce::File::findFiles, false, "*.xml;*.tlrpreset");
    presetFiles.sort();

    juce::PopupMenu menu;
    menu.addSectionHeader("User Presets (TriLayerReharm/Presets)");

    constexpr int kQuickSaveItemId   = 100001;
    constexpr int kBrowseFileItemId  = 100002;
    constexpr int kOpenFolderItemId  = 100003;
    constexpr int kPresetBaseItemId  = 1000;

    if (presetFiles.isEmpty())
    {
        menu.addItem(-1, "(No saved presets yet)", false, false);
    }
    else
    {
        for (int i = 0; i < presetFiles.size(); ++i)
        {
            menu.addItem(kPresetBaseItemId + i, presetFiles.getReference(i).getFileNameWithoutExtension());
        }
    }

    menu.addSeparator();
    menu.addItem(kQuickSaveItemId, "Quick Save Snapshot (Auto-Numbered)");
    menu.addItem(kBrowseFileItemId, "Browse Preset File...");
    menu.addItem(kOpenFolderItemId, "Open Presets Folder...");

    juce::Component::SafePointer<TriLayerReharmAudioProcessorEditor> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadPresetButton_),
                       [safeThis, presetFiles, presetsDir](int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        if (result == kQuickSaveItemId)
        {
            const int nextIdx = presetFiles.size() + 1;
            juce::String genreShort = safeThis->genreCombo_.getText().retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
            if (genreShort.isEmpty())
                genreShort = "Reharm";

            juce::File autoFile = presetsDir.getChildFile(
                juce::String::formatted("Preset_%02d_", nextIdx) + genreShort + ".xml");
            int suffix = 1;
            while (autoFile.existsAsFile())
            {
                autoFile = presetsDir.getChildFile(
                    juce::String::formatted("Preset_%02d_", nextIdx + suffix) + genreShort + ".xml");
                ++suffix;
            }

            if (safeThis->processorRef_.saveUserPresetToFile(autoFile))
            {
                safeThis->pianoRollComponent_.setSourceLabelText(
                    "SAVED PRESET: " + autoFile.getFileNameWithoutExtension());
            }
            return;
        }

        if (result == kOpenFolderItemId)
        {
            (void) presetsDir.startAsProcess();
            return;
        }

        if (result == kBrowseFileItemId)
        {
            safeThis->activeFileChooser_ = std::make_unique<juce::FileChooser>(
                "Load TriLayerReharm User Preset",
                presetsDir,
                "*.xml;*.tlrpreset");

            constexpr int openFlags = juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles;

            safeThis->activeFileChooser_->launchAsync(openFlags, [safeThis](const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr)
                    return;

                const juce::File chosen = chooser.getResult();
                if (chosen.existsAsFile())
                {
                    safeThis->applyLoadedUserPresetFile(chosen);
                }
            });
            return;
        }

        const int fileIdx = result - kPresetBaseItemId;
        if (juce::isPositiveAndBelow(fileIdx, presetFiles.size()))
        {
            safeThis->applyLoadedUserPresetFile(presetFiles.getReference(fileIdx));
        }
    });
}

void TriLayerReharmAudioProcessorEditor::applyLoadedUserPresetFile(const juce::File& presetFile)
{
    processorRef_.beginUndoTransaction("Load User Preset");
    if (!processorRef_.loadUserPresetFromFile(presetFile))
        return;

    const int restoredGenreIdx = static_cast<int>(std::round(
        apvtsRef_.getRawParameterValue(ParamIDs::genre)->load()));
    lastKnownLengthIdx_ = static_cast<int>(std::round(
        apvtsRef_.getRawParameterValue(ParamIDs::progressionLength)->load()));
    hasCustomDroppedMidi_ = processorRef_.hasCustomSourceMidi();

    updateProgressionComboItemsForGenre(restoredGenreIdx);
    pianoRollComponent_.setSourceLabelText("USER PRESET: " + presetFile.getFileNameWithoutExtension());
    syncEngineAndRefreshViews();
}

bool TriLayerReharmAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (mods.isCommandDown())
    {
        const int keyCode = key.getKeyCode();
        // Ctrl+Z / Cmd+Z (Shiftなし) -> Undo
        if ((keyCode == 'z' || keyCode == 'Z') && !mods.isShiftDown())
        {
            performUndo();
            return true;
        }
        // Ctrl+Y または Cmd+Shift+Z / Ctrl+Shift+Z -> Redo
        if (keyCode == 'y' || keyCode == 'Y' || ((keyCode == 'z' || keyCode == 'Z') && mods.isShiftDown()))
        {
            performRedo();
            return true;
        }
    }
    return false;
}

void TriLayerReharmAudioProcessorEditor::timerCallback()
{
    // 1. サイドチェイン音声のトランジェント強度を取得し、Orbのバウンス＆LEDへ反映
    const float pulse = processorRef_.consumeTransientVisualPulse();
    const bool grooveSyncOn = grooveSyncButton_.getToggleState();
    orbComponent_.setTransientBounceLevel(pulse, grooveSyncOn);

    // 2. Undo / Redo / Unlock ボタンの有効状態を更新
    undoButton_.setEnabled(processorRef_.getUndoManager().canUndo());
    redoButton_.setEnabled(processorRef_.getUndoManager().canRedo());
    unlockAllButton_.setEnabled(processorRef_.getEngine().hasLockedChordBlocks());

    // 3. v1.5 UI強化1: DAW（またはAudition）のリアルタイム再生位置（PPQ）をPianoRollへ反映
    pianoRollComponent_.setPlayheadPosition(
        processorRef_.getCurrentPlayheadPpq(),
        processorRef_.isTransportPlaying()
    );

    // 4. Groove Sync が有効な状態で新しいトランジェントが検出された場合、MIDIシーケンスを再吸着してPianoRollを更新
    if (processorRef_.consumeGrooveMapUpdatedFlag() && grooveSyncOn)
    {
        syncEngineAndRefreshViews();
    }
}

void TriLayerReharmAudioProcessorEditor::updateProgressionComboItemsForGenre(int genreIndex)
{
    const int safeGenre = std::clamp(genreIndex, 0, engine::ChordTemplateBank::kNumGenres - 1);
    const int currentPresetIdx = std::clamp(
        static_cast<int>(std::round(apvtsRef_.getRawParameterValue(ParamIDs::progressionPreset)->load())),
        0,
        engine::ChordTemplateBank::kNumPresetsPerGenre - 1
    );

    templateCombo_.clear(juce::dontSendNotification);
    templateCombo_.addItemList(engine::ChordTemplateBank::getProgressionNamesForGenre(safeGenre), 1);
    templateCombo_.setSelectedItemIndex(currentPresetIdx, juce::dontSendNotification);

    lastKnownGenreIdx_  = safeGenre;
    lastKnownPresetIdx_ = currentPresetIdx;
}

//==============================================================================
// Phase 6: juce::FileDragAndDropTarget 実装（MIDIファイルの堅牢なD&Dインポート）
//==============================================================================
bool TriLayerReharmAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& rawPath : files)
    {
        const auto trimmedPath = rawPath.trim().unquoted();
        if (trimmedPath.endsWithIgnoreCase(".mid") || trimmedPath.endsWithIgnoreCase(".midi"))
            return true;

        const juce::File candidate = juce::File::createFileWithoutCheckingPath(trimmedPath);
        const juce::String ext = candidate.getFileExtension();
        if (ext.equalsIgnoreCase(".mid") || ext.equalsIgnoreCase(".midi"))
            return true;
    }
    return false;
}

void TriLayerReharmAudioProcessorEditor::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x, y);
    if (isInterestedInFileDrag(files))
    {
        pianoRollComponent_.setFileDragHighlight(true);
    }
}

void TriLayerReharmAudioProcessorEditor::fileDragMove(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x, y);
    if (isInterestedInFileDrag(files))
    {
        pianoRollComponent_.setFileDragHighlight(true);
    }
}

void TriLayerReharmAudioProcessorEditor::fileDragExit(const juce::StringArray& files)
{
    juce::ignoreUnused(files);
    pianoRollComponent_.setFileDragHighlight(false);
}

void TriLayerReharmAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x, y);
    pianoRollComponent_.setFileDragHighlight(false);

    if (files.isEmpty())
    {
        DBG("filesDropped: Received empty file array.");
        return;
    }

    // 先頭ファイル（files[0]）から優先的に検査し、拡張子が .mid / .midi の対象ファイルを特定
    juce::File targetMidiFile(files[0].trim().unquoted());
    if (!targetMidiFile.getFileExtension().equalsIgnoreCase(".mid")
        && !targetMidiFile.getFileExtension().equalsIgnoreCase(".midi"))
    {
        for (const auto& path : files)
        {
            const juce::File candidate(path.trim().unquoted());
            const auto ext = candidate.getFileExtension();
            if (ext.equalsIgnoreCase(".mid") || ext.equalsIgnoreCase(".midi"))
            {
                targetMidiFile = candidate;
                break;
            }
        }
    }

    if (importMidiFileToEngine(targetMidiFile))
    {
        const juce::String customLabel = "SOURCE: " + targetMidiFile.getFileName();
        hasCustomDroppedMidi_ = true;
        processorRef_.setCustomSourceMidiState(true, customLabel);
        processorRef_.getEngine().clearLockedChordBlocks();
        resetMutationToZeroForLoadedChords();
        pianoRollComponent_.setSourceLabelText(customLabel);

        // 即時再計算および非同期UI更新をトリガー
        syncEngineAndRefreshViews();
        triggerAsyncUpdate();
    }
}

bool TriLayerReharmAudioProcessorEditor::importMidiFileToEngine(const juce::File& midiFileToLoad)
{
    // 1. ファイルの存在確認
    if (!midiFileToLoad.existsAsFile())
    {
        DBG("Failed to import MIDI file (file does not exist): " << midiFileToLoad.getFullPathName());
        return false;
    }

    const auto ext = midiFileToLoad.getFileExtension();
    if (!ext.equalsIgnoreCase(".mid") && !ext.equalsIgnoreCase(".midi"))
    {
        DBG("Failed to import MIDI file (unsupported extension '" << ext << "'): " << midiFileToLoad.getFileName());
        return false;
    }

    // 2. FileInputStream の生成とストリーム有効性チェック
    juce::FileInputStream inputStream(midiFileToLoad);
    if (!inputStream.openedOk())
    {
        DBG("Failed to open FileInputStream for MIDI file: " << midiFileToLoad.getFullPathName());
        return false;
    }

    // 3. juce::MidiFile のインスタンス化と readFrom() によるパース確認
    juce::MidiFile midiFile;
    if (!midiFile.readFrom(inputStream, true))
    {
        DBG("Failed to parse MidiFile via readFrom(): " << midiFileToLoad.getFullPathName());
        return false;
    }

    // タイムフォーマット（TPQN または SMPTE秒単位）の正規化
    const short rawTimeFormat = midiFile.getTimeFormat();
    double ticksPerQuarterNote = 960.0;
    double timestampScale = 1.0;

    if (rawTimeFormat > 0)
    {
        ticksPerQuarterNote = static_cast<double>(rawTimeFormat);
    }
    else
    {
        // SMPTE フォーマットの場合は秒変換後、120BPM基準（1拍=0.5秒 -> 960 TPQN）へスケーリング
        midiFile.convertTimestampTicksToSeconds();
        ticksPerQuarterNote = 960.0;
        timestampScale = 960.0 * 2.0;
    }

    // 4. 複数トラックのノート情報を単一の juce::MidiMessageSequence に統合（マージ）
    juce::MidiMessageSequence mergedSequence;
    const int numTracks = midiFile.getNumTracks();
    int totalNoteOnCount = 0;

    for (int t = 0; t < numTracks; ++t)
    {
        const auto* track = midiFile.getTrack(t);
        if (track == nullptr)
            continue;

        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto* holder = track->getEventPointer(i);
            if (holder == nullptr)
                continue;

            const auto& rawMsg = holder->message;
            if (!rawMsg.isNoteOnOrOff())
                continue;

            // ドラムトラック（MIDI Ch.10）が混在している場合は和音解析のノイズにならないよう除外（単一トラックがCh.10のみの場合は後段で救済）
            if (numTracks > 1 && rawMsg.getChannel() == 10)
                continue;

            juce::MidiMessage msg(rawMsg);
            if (rawMsg.isNoteOn(true) && rawMsg.getVelocity() == 0)
            {
                msg = juce::MidiMessage::noteOff(rawMsg.getChannel(), rawMsg.getNoteNumber());
            }
            msg.setTimeStamp( std::max(0.0, rawMsg.getTimeStamp() * timestampScale) );

            if (msg.isNoteOn())
                ++totalNoteOnCount;

            mergedSequence.addEvent(msg);
        }
    }

    // 万が一全ノートが Ch.10 だった場合のフォールバック（チャネル制限なしで再収集）
    if (totalNoteOnCount == 0)
    {
        for (int t = 0; t < numTracks; ++t)
        {
            const auto* track = midiFile.getTrack(t);
            if (track == nullptr)
                continue;

            for (int i = 0; i < track->getNumEvents(); ++i)
            {
                const auto* holder = track->getEventPointer(i);
                if (holder == nullptr || !holder->message.isNoteOnOrOff())
                    continue;

                juce::MidiMessage msg(holder->message);
                if (msg.isNoteOn(true) && msg.getVelocity() == 0)
                    msg = juce::MidiMessage::noteOff(msg.getChannel(), msg.getNoteNumber());
                msg.setTimeStamp(std::max(0.0, holder->message.getTimeStamp() * timestampScale));

                if (msg.isNoteOn())
                    ++totalNoteOnCount;

                mergedSequence.addEvent(msg);
            }
        }
    }

    mergedSequence.sort();
    mergedSequence.updateMatchedPairs();

    if (mergedSequence.getNumEvents() == 0 || totalNoteOnCount == 0)
    {
        DBG("Imported MIDI file contains no note events: " << midiFileToLoad.getFileName()
            << " (Tracks: " << numTracks << ")");
        return false;
    }

    // 5. エンジンへインプットシーケンスを転送し、詳細なデバッグログを出力
    processorRef_.getEngine().setSourceMidiSequence(mergedSequence, ticksPerQuarterNote);

    DBG("Successfully imported MIDI file: " << midiFileToLoad.getFileName()
        << ", Tracks: " << numTracks
        << ", Notes count: " << totalNoteOnCount
        << ", Sequence events: " << mergedSequence.getNumEvents()
        << ", TPQN: " << ticksPerQuarterNote);

    return true;
}

void TriLayerReharmAudioProcessorEditor::loadSelectedProgressionTemplate()
{
    const int genreIdx = std::clamp(
        static_cast<int>(std::round(apvtsRef_.getRawParameterValue(ParamIDs::genre)->load())),
        0,
        engine::ChordTemplateBank::kNumGenres - 1
    );
    const int progIdx = std::clamp(
        templateCombo_.getSelectedItemIndex(),
        0,
        engine::ChordTemplateBank::kNumPresetsPerGenre - 1
    );
    const int lengthIdx = static_cast<int>(std::round(
        apvtsRef_.getRawParameterValue(ParamIDs::progressionLength)->load()));
    const bool isEightBars = (lengthIdx == 1);

    constexpr double kDefaultTpqn = 960.0;
    const auto presetSeq = engine::ChordTemplateBank::createPresetSequence(genreIdx, progIdx, isEightBars, kDefaultTpqn);

    processorRef_.getEngine().setSourceMidiSequence(presetSeq, kDefaultTpqn);
    pianoRollComponent_.setSourceLabelText(
        "PRESET (" + juce::String(isEightBars ? "8 BARS" : "4 BARS") + "): ["
        + genreCombo_.getText() + "] " + templateCombo_.getText()
    );
    lastKnownLengthIdx_ = lengthIdx;

    syncEngineAndRefreshViews();
}

//==============================================================================
void TriLayerReharmAudioProcessorEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(newValue);
    // プリセット進行または長さそのものが明示的に切り替えられた場合のみプリセットモードへ戻す
    // （Genre 変更時は D&D インポート済みのカスタム MIDI を維持して別ジャンルで再計算させる）
    if (parameterID == ParamIDs::progressionPreset
        || parameterID == ParamIDs::progressionLength)
    {
        hasCustomDroppedMidi_ = false;
        processorRef_.setCustomSourceMidiState(false, {});
        processorRef_.getEngine().clearLockedChordBlocks();
    }
    triggerAsyncUpdate();
}

void TriLayerReharmAudioProcessorEditor::handleAsyncUpdate()
{
    processorRef_.getUndoManager().beginNewTransaction();
    syncEngineAndRefreshViews();
}

void TriLayerReharmAudioProcessorEditor::syncEngineAndRefreshViews()
{
    int genreIdx = 0;
    int presetIdx = 0;
    int lengthIdx = 0;
    int artistIdx = 0;
    int envIdx = 0;
    int transposeSemitones = 0;
    float mutationPercent = 0.0f;
    bool grooveSyncOn = false;
    bool melodyLockOn = false;
    bool forceDiatonicOn = false;
    int songKeyIdx = 0;
    int scaleTypeIdx = 0;
    bool pedalBassOn = false;
    int compingIdx = 0;
    bool abCompareOriginal = false;
    bool splitMidiChOn = false;

    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::genre))
        genreIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::progressionPreset))
        presetIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::progressionLength))
        lengthIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::artist))
        artistIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::environment))
        envIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::transpose))
        transposeSemitones = std::clamp(static_cast<int>(std::round(p->load())) - 12, -12, 12);
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::mutationAmount))
        mutationPercent = p->load();
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::enableGrooveSync))
        grooveSyncOn = (p->load() >= 0.5f);
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::melodyLock))
        melodyLockOn = (p->load() >= 0.5f);
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::forceDiatonic))
        forceDiatonicOn = (p->load() >= 0.5f);
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::songKey))
        songKeyIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::scaleType))
        scaleTypeIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::pedalBassMode))
        pedalBassOn = (p->load() >= 0.5f);
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::compingStyle))
        compingIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::abCompare))
        abCompareOriginal = (p->load() >= 0.5f);
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::splitMidiChannels))
        splitMidiChOn = (p->load() >= 0.5f);

    abCompareButton_.setButtonText(abCompareOriginal ? "A/B: Original" : "A/B: Reharm");

    // ジャンル、プリセット番号、または長さ（4 Bars / 8 Bars）が変化していたら2段目のコンボボックスとベースMIDIを更新
    if (genreIdx != lastKnownGenreIdx_ || presetIdx != lastKnownPresetIdx_ || lengthIdx != lastKnownLengthIdx_)
    {
        updateProgressionComboItemsForGenre(genreIdx);
        lastKnownLengthIdx_ = lengthIdx;
        if (!hasCustomDroppedMidi_)
        {
            const bool isEightBars = (lengthIdx == 1);
            constexpr double kDefaultTpqn = 960.0;
            const auto presetSeq = engine::ChordTemplateBank::createPresetSequence(genreIdx, presetIdx, isEightBars, kDefaultTpqn);
            processorRef_.getEngine().setSourceMidiSequence(presetSeq, kDefaultTpqn);
            pianoRollComponent_.setSourceLabelText(
                "PRESET (" + juce::String(isEightBars ? "8 BARS" : "4 BARS") + "): ["
                + genreCombo_.getText() + "] " + templateCombo_.getText()
            );
        }
    }

    juce::String genreText = genreCombo_.getText();
    if (genreText.isEmpty())
        genreText = "Jazz";

    juce::String envText = environmentCombo_.getText();
    if (envText.isEmpty())
        envText = "Rainy";

    // 1. Tri-Layer Orb の視覚状態と Mutation 歪み量を更新（A/B Original 時は Orb も 0% 表示）
    orbComponent_.setLayerState(genreText, artistIdx, envText);
    orbComponent_.setMutationAmount(abCompareOriginal ? 0.0f : mutationPercent);

    // 8ジャンル別のアクセントカラー設定
    juce::Colour accentColour(0xffff8c24); // Jazz: Warm Amber
    if (genreText.containsIgnoreCase("Neo-Soul"))
        accentColour = juce::Colour(0xffa855f7);
    else if (genreText.containsIgnoreCase("City Pop"))
        accentColour = juce::Colour(0xffec4899);
    else if (genreText.containsIgnoreCase("Cinematic"))
        accentColour = juce::Colour(0xff14b8a6);
    else if (genreText.containsIgnoreCase("Lo-Fi"))
        accentColour = juce::Colour(0xffd97706);
    else if (genreText.containsIgnoreCase("Modern R&B"))
        accentColour = juce::Colour(0xff6366f1);
    else if (genreText.containsIgnoreCase("Gospel"))
        accentColour = juce::Colour(0xfffacc15);
    else if (genreText.containsIgnoreCase("8-Bit"))
        accentColour = juce::Colour(0xff22c55e);

    pianoRollComponent_.setAccentColour(accentColour);

    // 2. MidiReharmonizerEngine のプロファイル、Mutation 量、Transpose 量、A/B Compare、Ch分離、および3つの和声進化モードを更新し、再計算を実行
    auto& engine = processorRef_.getEngine();
    engine.setMutationAmount(mutationPercent);
    engine.setTransposeSemitones(transposeSemitones);
    engine.setGrooveSyncEnabled(grooveSyncOn);
    engine.setMelodyLockEnabled(melodyLockOn);
    engine.setForceDiatonicEnabled(forceDiatonicOn);
    engine.setSongKeyPitchClass(songKeyIdx);
    engine.setScaleType(static_cast<engine::ScaleType>(std::clamp(scaleTypeIdx, 0, 6)));
    engine.setPedalBassModeEnabled(pedalBassOn);
    engine.setCompingStyle(static_cast<engine::CompingStyle>(std::clamp(compingIdx, 0, 2)));
    engine.setCompareOriginalMode(abCompareOriginal);
    engine.setSplitMidiChannelsEnabled(splitMidiChOn);
    engine.updateRulesFromPresets(genreIdx, artistIdx, envIdx);
    engine.triggerOfflineReharmonize(true);

    unlockAllButton_.setEnabled(engine.hasLockedChordBlocks());

    // 3. 最新の生成済み MidiMessageSequence、元MIDIゴーストノート、および各ブロック情報（ロック状態・コードネーム用）を PianoRollComponent に渡して再描画
    pianoRollComponent_.setMidiSequence(engine.getReharmonizedMidiSequence());
    pianoRollComponent_.setGhostMidiSequence(engine.getCapturedMidiSequence());
    pianoRollComponent_.setChordBlockViews(engine.getChordBlockViews());
}

void TriLayerReharmAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient bgGrad(
        juce::Colour(0xff161922), 0.0f, 0.0f,
        juce::Colour(0xff0b0d12), 0.0f, static_cast<float>(getHeight()),
        false
    );
    g.setGradientFill(bgGrad);
    g.fillAll();

    auto topArea = getLocalBounds().reduced(16).removeFromTop(static_cast<int>(getHeight() * 0.60f) - 16);
    auto controlPanelBounds = topArea.removeFromLeft(340).toFloat();

    g.setColour(juce::Colour(0xff11131a));
    g.fillRoundedRectangle(controlPanelBounds, 10.0f);
    g.setColour(juce::Colour(0xff262936));
    g.drawRoundedRectangle(controlPanelBounds.reduced(0.5f), 10.0f, 1.0f);

    // 画面右下隅にコピーライト表記を描画
    const auto copyrightText = juce::String::fromUTF8(
        reinterpret_cast<const char*>(u8"© 2026 Universal Make Associates")
    );
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(copyrightText,
               getLocalBounds().reduced(16, 4),
               juce::Justification::bottomRight,
               false);
}

void TriLayerReharmAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromBottom(8);

    const int topHeight = static_cast<int>(getHeight() * 0.60f) - 16;
    auto topSection = area.removeFromTop(topHeight);
    area.removeFromTop(12);
    auto bottomSection = area;

    // --- 上部セクション: 左にコントロールパネル、右に Tri-Layer Orb ---
    auto controlColumn = topSection.removeFromLeft(340).reduced(14, 8);
    topSection.removeFromLeft(12);
    orbComponent_.setBounds(topSection);

    // ヘッダー行: 左にタイトル、右に Save / Load / Unlock / Undo / Redo ボタン
    auto headerRow = controlColumn.removeFromTop(22);
    redoButton_.setBounds(headerRow.removeFromRight(38));
    headerRow.removeFromRight(3);
    undoButton_.setBounds(headerRow.removeFromRight(38));
    headerRow.removeFromRight(3);
    unlockAllButton_.setBounds(headerRow.removeFromRight(46));
    headerRow.removeFromRight(3);
    loadPresetButton_.setBounds(headerRow.removeFromRight(38));
    headerRow.removeFromRight(3);
    savePresetButton_.setBounds(headerRow.removeFromRight(38));
    titleLabel_.setBounds(headerRow);
    controlColumn.removeFromTop(3);

    constexpr int kLabelHeight = 13;
    constexpr int kComboHeight = 21;
    constexpr int kItemSpacing = 3;

    // 1段目: Genre 選択 (8種) ＋ Comping Style 選択 (3種)
    constexpr int kCompingBoxWidth = 136;
    auto genreLabelRow = controlColumn.removeFromTop(kLabelHeight);
    compingLabel_.setBounds(genreLabelRow.removeFromRight(kCompingBoxWidth));
    genreLabelRow.removeFromRight(6);
    genreLabel_.setBounds(genreLabelRow);

    auto genreComboRow = controlColumn.removeFromTop(kComboHeight);
    compingCombo_.setBounds(genreComboRow.removeFromRight(kCompingBoxWidth));
    genreComboRow.removeFromRight(6);
    genreCombo_.setBounds(genreComboRow);
    controlColumn.removeFromTop(kItemSpacing);

    // 2段目: Progression Preset 選択 (各ジャンル4種＝32種) ＋ Length 選択 (4 Bars / 8 Bars)
    constexpr int kLengthBoxWidth = 82;
    auto presetLabelRow = controlColumn.removeFromTop(kLabelHeight);
    lengthLabel_.setBounds(presetLabelRow.removeFromRight(kLengthBoxWidth));
    presetLabelRow.removeFromRight(6);
    templateLabel_.setBounds(presetLabelRow);

    auto presetComboRow = controlColumn.removeFromTop(kComboHeight);
    lengthCombo_.setBounds(presetComboRow.removeFromRight(kLengthBoxWidth));
    presetComboRow.removeFromRight(6);
    templateCombo_.setBounds(presetComboRow);
    controlColumn.removeFromTop(kItemSpacing);

    // 3段目: Artist (達人スタイル) ＋ Environment (空間)
    constexpr int kEnvBoxWidth = 108;
    auto artistEnvLabelRow = controlColumn.removeFromTop(kLabelHeight);
    environmentLabel_.setBounds(artistEnvLabelRow.removeFromRight(kEnvBoxWidth));
    artistEnvLabelRow.removeFromRight(6);
    artistLabel_.setBounds(artistEnvLabelRow);

    auto artistEnvComboRow = controlColumn.removeFromTop(kComboHeight);
    environmentCombo_.setBounds(artistEnvComboRow.removeFromRight(kEnvBoxWidth));
    artistEnvComboRow.removeFromRight(6);
    artistCombo_.setBounds(artistEnvComboRow);
    controlColumn.removeFromTop(kItemSpacing);

    // 4段目: Mutation ラベル ＋ A/B Compare トグルボタン（上段）
    //        Mutation スライダー ＋ Layer Safe ボタン ＋ Reset ボタン（下段）
    auto mutationHeaderRow = controlColumn.removeFromTop(16);
    abCompareButton_.setBounds(mutationHeaderRow.removeFromRight(84));
    mutationHeaderRow.removeFromRight(4);
    mutationLabel_.setBounds(mutationHeaderRow);
    controlColumn.removeFromTop(1);

    auto mutationRow = controlColumn.removeFromTop(kComboHeight);
    resetButton_.setBounds(mutationRow.removeFromRight(48));
    mutationRow.removeFromRight(4);
    layerSafeButton_.setBounds(mutationRow.removeFromRight(70));
    mutationRow.removeFromRight(5);
    mutationSlider_.setBounds(mutationRow);
    controlColumn.removeFromTop(kItemSpacing);

    // 5段目: Operating Mode 選択 ＋ Transpose 選択 (-12 to +12) ＋ Key 選択 (12 Keys) ＋ Scale / Mode 選択 (7 Scales)
    constexpr int kScaleBoxWidth     = 106;
    constexpr int kKeyBoxWidth       = 44;
    constexpr int kTransposeBoxWidth = 68;
    auto modeLabelRow = controlColumn.removeFromTop(kLabelHeight);
    scaleTypeLabel_.setBounds(modeLabelRow.removeFromRight(kScaleBoxWidth));
    modeLabelRow.removeFromRight(4);
    songKeyLabel_.setBounds(modeLabelRow.removeFromRight(kKeyBoxWidth));
    modeLabelRow.removeFromRight(4);
    transposeLabel_.setBounds(modeLabelRow.removeFromRight(kTransposeBoxWidth));
    modeLabelRow.removeFromRight(4);
    modeLabel_.setBounds(modeLabelRow);

    auto modeComboRow = controlColumn.removeFromTop(kComboHeight);
    scaleTypeCombo_.setBounds(modeComboRow.removeFromRight(kScaleBoxWidth));
    modeComboRow.removeFromRight(4);
    songKeyCombo_.setBounds(modeComboRow.removeFromRight(kKeyBoxWidth));
    modeComboRow.removeFromRight(4);
    transposeCombo_.setBounds(modeComboRow.removeFromRight(kTransposeBoxWidth));
    modeComboRow.removeFromRight(4);
    modeCombo_.setBounds(modeComboRow);
    controlColumn.removeFromTop(3);

    // 6段目: 和声の進化3機能 (Melody Lock / Force Diatonic / Pedal Bass)
    auto harmonyLockRow = controlColumn.removeFromTop(21);
    const int thirdW = harmonyLockRow.getWidth() / 3;
    melodyLockButton_.setBounds(harmonyLockRow.removeFromLeft(thirdW));
    forceDiatonicButton_.setBounds(harmonyLockRow.removeFromLeft(thirdW + 8));
    pedalBassButton_.setBounds(harmonyLockRow);
    controlColumn.removeFromTop(2);

    // 7段目(最下段): Groove Sync トグル ＆ Split Bass/Chord Ch. トグル ＆ Audition トグル
    auto toggleRow = controlColumn.removeFromTop(22);
    grooveSyncButton_.setBounds(toggleRow.removeFromLeft(90));
    splitMidiChButton_.setBounds(toggleRow.removeFromLeft(122));
    auditionButton_.setBounds(toggleRow);

    // --- 下部セクション: Piano Roll Viewer ---
    pianoRollComponent_.setBounds(bottomSection);
}

} // namespace reharm
