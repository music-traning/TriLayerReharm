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
} // anonymous namespace

TriLayerReharmAudioProcessorEditor::TriLayerReharmAudioProcessorEditor(TriLayerReharmAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processorRef_(p),
      apvtsRef_(p.getAPVTS())
{
    titleLabel_.setText("TRI-LAYER REHARM", juce::dontSendNotification);
    titleLabel_.setFont(juce::FontOptions(16.5f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    // Progression Template ComboBox の構築（Phase 7）
    templateLabel_.setText("PROGRESSION TEMPLATE", juce::dontSendNotification);
    templateLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    templateLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
    addAndMakeVisible(templateLabel_);

    templateCombo_.addItemList(engine::ChordTemplateBank::getTemplateNames(), 1);
    templateCombo_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff181c26));
    templateCombo_.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff38bdf8).withAlpha(0.55f));
    templateCombo_.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    templateCombo_.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff38bdf8));
    templateCombo_.setSelectedItemIndex(0, juce::dontSendNotification);
    templateCombo_.onChange = [this]()
    {
        loadSelectedProgressionTemplate();
    };
    addAndMakeVisible(templateCombo_);

    configureComboBox(genreCombo_,       genreLabel_,       "LAYER 1 : GENRE (CORE)",             apvtsRef_, ParamIDs::genre);
    configureComboBox(artistCombo_,      artistLabel_,      "LAYER 2 : ARTIST (ORBIT)",           apvtsRef_, ParamIDs::artist);
    configureComboBox(environmentCombo_, environmentLabel_, "LAYER 3 : ENVIRONMENT (ATMOSPHERE)", apvtsRef_, ParamIDs::environment);
    configureComboBox(modeCombo_,        modeLabel_,        "OPERATING MODE",                     apvtsRef_, ParamIDs::operatingMode);

    addAndMakeVisible(genreLabel_);
    addAndMakeVisible(genreCombo_);
    addAndMakeVisible(artistLabel_);
    addAndMakeVisible(artistCombo_);
    addAndMakeVisible(environmentLabel_);
    addAndMakeVisible(environmentCombo_);
    addAndMakeVisible(modeLabel_);
    addAndMakeVisible(modeCombo_);

    // Phase 8: Mutation Slider の構築
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
    addAndMakeVisible(mutationSlider_);

    // Phase 9: Audio Groove Sync トグルボタンの構築
    grooveSyncButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff34d399));
    grooveSyncButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff10b981));
    grooveSyncButton_.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff64748b));
    addAndMakeVisible(grooveSyncButton_);

    // APVTS アタッチメントによる双方向バインディング
    genreAttachment_       = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::genre,             genreCombo_);
    artistAttachment_      = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::artist,            artistCombo_);
    environmentAttachment_ = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::environment,       environmentCombo_);
    modeAttachment_        = std::make_unique<ComboAttachment>(apvtsRef_, ParamIDs::operatingMode,     modeCombo_);
    mutationAttachment_    = std::make_unique<SliderAttachment>(apvtsRef_, ParamIDs::mutationAmount,   mutationSlider_);
    grooveSyncAttachment_  = std::make_unique<ButtonAttachment>(apvtsRef_, ParamIDs::enableGrooveSync, grooveSyncButton_);

    addAndMakeVisible(orbComponent_);
    addAndMakeVisible(pianoRollComponent_);

    // APVTS パラメーターリスナーの登録
    apvtsRef_.addParameterListener(ParamIDs::genre, this);
    apvtsRef_.addParameterListener(ParamIDs::artist, this);
    apvtsRef_.addParameterListener(ParamIDs::environment, this);
    apvtsRef_.addParameterListener(ParamIDs::operatingMode, this);
    apvtsRef_.addParameterListener(ParamIDs::mutationAmount, this);
    apvtsRef_.addParameterListener(ParamIDs::enableGrooveSync, this);

    setResizable(true, true);
    setResizeLimits(720, 580, 1400, 1000);
    setSize(900, 640);

    loadSelectedProgressionTemplate();

    // トランジェント視覚フィードバック＆GrooveMap更新監視用タイマー開始 (30Hz)
    startTimerHz(30);
}

TriLayerReharmAudioProcessorEditor::~TriLayerReharmAudioProcessorEditor()
{
    stopTimer();
    apvtsRef_.removeParameterListener(ParamIDs::genre, this);
    apvtsRef_.removeParameterListener(ParamIDs::artist, this);
    apvtsRef_.removeParameterListener(ParamIDs::environment, this);
    apvtsRef_.removeParameterListener(ParamIDs::operatingMode, this);
    apvtsRef_.removeParameterListener(ParamIDs::mutationAmount, this);
    apvtsRef_.removeParameterListener(ParamIDs::enableGrooveSync, this);
    cancelPendingUpdate();
}

void TriLayerReharmAudioProcessorEditor::timerCallback()
{
    // 1. サイドチェイン音声のトランジェント強度を取得し、Orbのバウンス＆LEDへ反映
    const float pulse = processorRef_.consumeTransientVisualPulse();
    const bool grooveSyncOn = grooveSyncButton_.getToggleState();
    orbComponent_.setTransientBounceLevel(pulse, grooveSyncOn);

    // 2. Groove Sync が有効な状態で新しいトランジェントが検出された場合、MIDIシーケンスを再吸着してPianoRollを更新
    if ( processorRef_.consumeGrooveMapUpdatedFlag() && grooveSyncOn )
    {
        syncEngineAndRefreshViews();
    }
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
    const int selectedIdx = std::clamp(templateCombo_.getSelectedItemIndex(), 0, 3);
    const auto templateId = static_cast<engine::TemplateId>(selectedIdx);

    constexpr double kDefaultTpqn = 960.0;
    const auto templateSeq = engine::ChordTemplateBank::createTemplateSequence(templateId, kDefaultTpqn);

    processorRef_.getEngine().setSourceMidiSequence(templateSeq, kDefaultTpqn);
    pianoRollComponent_.setSourceLabelText("TEMPLATE: " + templateCombo_.getText());

    syncEngineAndRefreshViews();
}

//==============================================================================
void TriLayerReharmAudioProcessorEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(parameterID, newValue);
    triggerAsyncUpdate();
}

void TriLayerReharmAudioProcessorEditor::handleAsyncUpdate()
{
    syncEngineAndRefreshViews();
}

void TriLayerReharmAudioProcessorEditor::syncEngineAndRefreshViews()
{
    int genreIdx = 0;
    int artistIdx = 0;
    int envIdx = 0;
    float mutationPercent = 50.0f;
    bool grooveSyncOn = false;

    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::genre))
        genreIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::artist))
        artistIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::environment))
        envIdx = static_cast<int>(std::round(p->load()));
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::mutationAmount))
        mutationPercent = p->load();
    if (auto* p = apvtsRef_.getRawParameterValue(ParamIDs::enableGrooveSync))
        grooveSyncOn = (p->load() >= 0.5f);

    juce::String genreText = genreCombo_.getText();
    if (genreText.isEmpty())
        genreText = "Jazz";

    juce::String envText = environmentCombo_.getText();
    if (envText.isEmpty())
        envText = "Rainy";

    // 1. Tri-Layer Orb の視覚状態と Mutation 歪み量を更新
    orbComponent_.setLayerState(genreText, artistIdx, envText);
    orbComponent_.setMutationAmount(mutationPercent);

    juce::Colour accentColour(0xffff8c24);
    if (genreText.containsIgnoreCase("Neo-Soul"))
        accentColour = juce::Colour(0xffa855f7);
    else if (genreText.containsIgnoreCase("City Pop"))
        accentColour = juce::Colour(0xffec4899);
    else if (genreText.containsIgnoreCase("Cinematic"))
        accentColour = juce::Colour(0xff14b8a6);

    pianoRollComponent_.setAccentColour(accentColour);

    // 2. MidiReharmonizerEngine のプロファイル、Mutation 量、および Groove Sync 状態を更新し、ビタビ探索＆動的クオンタイズを再実行
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

    // 画面右下隅にコピーライト表記を描画（C++20 UTF-8文字列リテラルを使用）
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
    // 右下のコピーライト表示領域が隠れないよう下部に少し余白を確保
    area.removeFromBottom(8);

    const int topHeight = static_cast<int>(getHeight() * 0.60f) - 16;
    auto topSection = area.removeFromTop(topHeight);
    area.removeFromTop(12);
    auto bottomSection = area;

    // --- 上部セクション: 左にコントロールパネル、右に Tri-Layer Orb ---
    auto controlColumn = topSection.removeFromLeft(340).reduced(14, 10);
    topSection.removeFromLeft(12);
    orbComponent_.setBounds(topSection);

    titleLabel_.setBounds(controlColumn.removeFromTop(20));
    controlColumn.removeFromTop(3);

    constexpr int kLabelHeight = 14;
    constexpr int kComboHeight = 22;
    constexpr int kItemSpacing = 3;

    templateLabel_.setBounds(controlColumn.removeFromTop(kLabelHeight));
    templateCombo_.setBounds(controlColumn.removeFromTop(kComboHeight));
    controlColumn.removeFromTop(kItemSpacing);

    genreLabel_.setBounds(controlColumn.removeFromTop(kLabelHeight));
    genreCombo_.setBounds(controlColumn.removeFromTop(kComboHeight));
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

    grooveSyncButton_.setBounds(controlColumn.removeFromTop(24));

    // --- 下部セクション: Piano Roll Viewer ---
    pianoRollComponent_.setBounds(bottomSection);
}

} // namespace reharm
