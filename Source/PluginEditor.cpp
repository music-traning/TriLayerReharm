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
} // anonymous namespace

TriLayerReharmAudioProcessorEditor::TriLayerReharmAudioProcessorEditor(TriLayerReharmAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processorRef_(p),
      apvtsRef_(p.getAPVTS())
{
    setWantsKeyboardFocus(true);

    titleLabel_.setText("TRI-LAYER REHARM", juce::dontSendNotification);
    titleLabel_.setFont(juce::FontOptions(15.5f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    // Phase 12: Undo / Redo ボタンの構築
    styleActionButton(undoButton_);
    styleActionButton(redoButton_);
    undoButton_.setTooltip("Undo (Ctrl+Z / Cmd+Z)");
    redoButton_.setTooltip("Redo (Ctrl+Y / Cmd+Shift+Z)");
    undoButton_.onClick = [this]() { performUndo(); };
    redoButton_.onClick = [this]() { performRedo(); };
    addAndMakeVisible(undoButton_);
    addAndMakeVisible(redoButton_);

    // 1段目: Layer 1 Genre 選択（8ジャンル）
    configureComboBox(genreCombo_,       genreLabel_,       "LAYER 1 : GENRE (8 GENRES)",         apvtsRef_, ParamIDs::genre);
    configureComboBox(lengthCombo_,      lengthLabel_,      "LENGTH",                             apvtsRef_, ParamIDs::progressionLength);
    configureComboBox(artistCombo_,      artistLabel_,      "LAYER 2 : ARTIST (ORBIT)",           apvtsRef_, ParamIDs::artist);
    configureComboBox(environmentCombo_, environmentLabel_, "LAYER 3 : ENVIRONMENT (ATMOSPHERE)", apvtsRef_, ParamIDs::environment);
    configureComboBox(modeCombo_,        modeLabel_,        "OPERATING MODE",                     apvtsRef_, ParamIDs::operatingMode);

    lengthLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
    lengthCombo_.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff38bdf8).withAlpha(0.55f));
    lengthCombo_.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff38bdf8));

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
                processorRef_.getUndoManager().beginNewTransaction("Select Progression Preset");
                presetParam->setValueNotifyingHost(presetParam->convertTo0to1(static_cast<float>(selectedIdx)));
            }
        }
        hasCustomDroppedMidi_ = false;
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
        processorRef_.getUndoManager().beginNewTransaction("Change Mutation Slider");
    };
    addAndMakeVisible(mutationSlider_);

    // Phase 9: Audio Groove Sync トグルボタン
    grooveSyncButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff34d399));
    grooveSyncButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff10b981));
    grooveSyncButton_.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff64748b));
    grooveSyncButton_.onClick = [this]()
    {
        processorRef_.getUndoManager().beginNewTransaction("Toggle Groove Sync");
    };
    addAndMakeVisible(grooveSyncButton_);

    // Phase 12: Audition (Preview) トグルボタン
    auditionButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfffbbf24));
    auditionButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xfff59e0b));
    auditionButton_.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff64748b));
    auditionButton_.onClick = [this]()
    {
        processorRef_.getUndoManager().beginNewTransaction("Toggle Audition Synth");
    };
    addAndMakeVisible(auditionButton_);

    // APVTS アタッチメントによる双方向バインディング
    genreAttachment_       = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::genre,             genreCombo_);
    lengthAttachment_      = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::progressionLength, lengthCombo_);
    artistAttachment_      = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::artist,            artistCombo_);
    environmentAttachment_ = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::environment,       environmentCombo_);
    modeAttachment_        = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::operatingMode,     modeCombo_);
    mutationAttachment_    = std::make_unique<SliderAttachment>(apvtsRef_, ParamIDs::mutationAmount,   mutationSlider_);
    grooveSyncAttachment_  = std::make_unique<ButtonAttachment>(apvtsRef_, ParamIDs::enableGrooveSync, grooveSyncButton_);
    auditionAttachment_    = std::make_unique<ButtonAttachment>(apvtsRef_, ParamIDs::enableAudition,   auditionButton_);

    addAndMakeVisible(orbComponent_);
    addAndMakeVisible(pianoRollComponent_);

    // APVTS パラメーターリスナーの登録
    apvtsRef_.addParameterListener(ParamIDs::genre, this);
    apvtsRef_.addParameterListener(ParamIDs::progressionPreset, this);
    apvtsRef_.addParameterListener(ParamIDs::progressionLength, this);
    apvtsRef_.addParameterListener(ParamIDs::artist, this);
    apvtsRef_.addParameterListener(ParamIDs::environment, this);
    apvtsRef_.addParameterListener(ParamIDs::operatingMode, this);
    apvtsRef_.addParameterListener(ParamIDs::mutationAmount, this);
    apvtsRef_.addParameterListener(ParamIDs::enableGrooveSync, this);
    apvtsRef_.addParameterListener(ParamIDs::enableAudition, this);

    setResizable(true, true);
    setResizeLimits(720, 580, 1400, 1000);
    setSize(900, 640);

    const int initialGenreIdx = static_cast<int>(std::round(
        apvtsRef_.getRawParameterValue(ParamIDs::genre)->load()));
    updateProgressionComboItemsForGenre(initialGenreIdx);
    loadSelectedProgressionTemplate();

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
    apvtsRef_.removeParameterListener(ParamIDs::mutationAmount, this);
    apvtsRef_.removeParameterListener(ParamIDs::enableGrooveSync, this);
    apvtsRef_.removeParameterListener(ParamIDs::enableAudition, this);
    cancelPendingUpdate();
}

void TriLayerReharmAudioProcessorEditor::performUndo()
{
    if (processorRef_.getUndoManager().canUndo())
    {
        processorRef_.getUndoManager().undo();
        syncEngineAndRefreshViews();
    }
}

void TriLayerReharmAudioProcessorEditor::performRedo()
{
    if (processorRef_.getUndoManager().canRedo())
    {
        processorRef_.getUndoManager().redo();
        syncEngineAndRefreshViews();
    }
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

    // 2. Undo / Redo ボタンの有効状態を更新
    undoButton_.setEnabled(processorRef_.getUndoManager().canUndo());
    redoButton_.setEnabled(processorRef_.getUndoManager().canRedo());

    // 3. Groove Sync が有効な状態で新しいトランジェントが検出された場合、MIDIシーケンスを再吸着してPianoRollを更新
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
// Phase 6: juce::FileDragAndDropTarget 実装（MIDIファイルのD&Dインポート）
//==============================================================================
bool TriLayerReharmAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& filePath : files)
    {
        const juce::File f(filePath);
        if (f.hasFileExtension(".mid;.midi"))
            return true;
    }
    return false;
}

void TriLayerReharmAudioProcessorEditor::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(files, x, y);
    pianoRollComponent_.setFileDragHighlight(true);
}

void TriLayerReharmAudioProcessorEditor::fileDragMove(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(files, x, y);
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

    for (const auto& filePath : files)
    {
        const juce::File droppedFile(filePath);
        if (droppedFile.existsAsFile() && droppedFile.hasFileExtension(".mid;.midi"))
        {
            if (importMidiFileToEngine(droppedFile))
            {
                hasCustomDroppedMidi_ = true;
                pianoRollComponent_.setSourceLabelText("SOURCE: " + droppedFile.getFileName());
                syncEngineAndRefreshViews();
            }
            break;
        }
    }
}

bool TriLayerReharmAudioProcessorEditor::importMidiFileToEngine(const juce::File& midiFileToLoad)
{
    juce::FileInputStream inputStream(midiFileToLoad);
    if (!inputStream.openedOk())
        return false;

    juce::MidiFile midiFile;
    if (!midiFile.readFrom(inputStream))
        return false;

    const short rawTimeFormat = midiFile.getTimeFormat();
    double ticksPerQuarterNote = 960.0;
    if (rawTimeFormat > 0)
    {
        ticksPerQuarterNote = static_cast<double>(rawTimeFormat);
    }
    else
    {
        midiFile.convertTimestampTicksToSeconds();
        ticksPerQuarterNote = 0.5;
    }

    juce::MidiMessageSequence mergedSequence;
    const int numTracks = midiFile.getNumTracks();

    for (int t = 0; t < numTracks; ++t)
    {
        const auto* track = midiFile.getTrack(t);
        if (track == nullptr)
            continue;

        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto* holder = track->getEventPointer(i);
            if (holder != nullptr && holder->message.isNoteOnOrOff())
            {
                mergedSequence.addEvent(holder->message);
            }
        }
    }

    mergedSequence.sort();
    mergedSequence.updateMatchedPairs();

    if (mergedSequence.getNumEvents() == 0)
        return false;

    processorRef_.getEngine().setSourceMidiSequence(mergedSequence, ticksPerQuarterNote);
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
    if (parameterID == ParamIDs::genre
        || parameterID == ParamIDs::progressionPreset
        || parameterID == ParamIDs::progressionLength)
    {
        hasCustomDroppedMidi_ = false;
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
    float mutationPercent = 50.0f;
    bool grooveSyncOn = false;

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
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::mutationAmount))
        mutationPercent = p->load();
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::enableGrooveSync))
        grooveSyncOn = (p->load() >= 0.5f);

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

    // 1. Tri-Layer Orb の視覚状態と Mutation 歪み量を更新
    orbComponent_.setLayerState(genreText, artistIdx, envText);
    orbComponent_.setMutationAmount(mutationPercent);

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

    // 2. MidiReharmonizerEngine のプロファイル、Mutation 量、Groove Sync 状態を更新し、ビタビ探索を実行
    auto& engine = processorRef_.getEngine();
    engine.setMutationAmount(mutationPercent);
    engine.setGrooveSyncEnabled(grooveSyncOn);
    engine.updateRulesFromPresets(genreIdx, artistIdx, envIdx);
    engine.triggerOfflineReharmonize(true);

    // 3. 最新の生成済み MidiMessageSequence を PianoRollComponent に渡して再描画
    pianoRollComponent_.setMidiSequence(engine.getReharmonizedMidiSequence());
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
    auto controlColumn = topSection.removeFromLeft(340).reduced(14, 10);
    topSection.removeFromLeft(12);
    orbComponent_.setBounds(topSection);

    // ヘッダー行: 左にタイトル、右に Undo / Redo ボタン
    auto headerRow = controlColumn.removeFromTop(22);
    redoButton_.setBounds(headerRow.removeFromRight(50));
    headerRow.removeFromRight(4);
    undoButton_.setBounds(headerRow.removeFromRight(50));
    titleLabel_.setBounds(headerRow);
    controlColumn.removeFromTop(3);

    constexpr int kLabelHeight = 14;
    constexpr int kComboHeight = 22;
    constexpr int kItemSpacing = 3;

    // 1段目: Genre 選択 (8種)
    genreLabel_.setBounds(controlColumn.removeFromTop(kLabelHeight));
    genreCombo_.setBounds(controlColumn.removeFromTop(kComboHeight));
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

    artistLabel_.setBounds(controlColumn.removeFromTop(kLabelHeight));
    artistCombo_.setBounds(controlColumn.removeFromTop(kComboHeight));
    controlColumn.removeFromTop(kItemSpacing);

    environmentLabel_.setBounds(controlColumn.removeFromTop(kLabelHeight));
    environmentCombo_.setBounds(controlColumn.removeFromTop(kComboHeight));
    controlColumn.removeFromTop(kItemSpacing);

    mutationLabel_.setBounds(controlColumn.removeFromTop(kLabelHeight));
    mutationSlider_.setBounds(controlColumn.removeFromTop(kComboHeight));
    controlColumn.removeFromTop(kItemSpacing);

    modeLabel_.setBounds(controlColumn.removeFromTop(kLabelHeight));
    modeCombo_.setBounds(controlColumn.removeFromTop(kComboHeight));
    controlColumn.removeFromTop(4);

    // 最下段: Groove Sync トグル ＆ Audition (Preview) トグル
    auto toggleRow = controlColumn.removeFromTop(24);
    grooveSyncButton_.setBounds(toggleRow.removeFromLeft(toggleRow.getWidth() / 2));
    auditionButton_.setBounds(toggleRow);

    // --- 下部セクション: Piano Roll Viewer ---
    pianoRollComponent_.setBounds(bottomSection);
}

} // namespace reharm
