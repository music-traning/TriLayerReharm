#include "PianoRollComponent.h"
#include "../Engine/ChordAnalyzer.h"
#include <algorithm>
#include <cmath>

namespace reharm::ui
{

PianoRollComponent::PianoRollComponent()
{
    notes_.reserve(128);
}

bool PianoRollComponent::isBlackKey(int midiNoteNumber) noexcept
{
    const int pc = ((midiNoteNumber % 12) + 12) % 12;
    return (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
}

void PianoRollComponent::setAccentColour(juce::Colour newColour) noexcept
{
    accentColour_ = newColour;
    repaint();
}

void PianoRollComponent::setFileDragHighlight(bool isDraggingOver)
{
    if (isFileDragHighlighted_ != isDraggingOver)
    {
        isFileDragHighlighted_ = isDraggingOver;
        repaint();
    }
}

void PianoRollComponent::setSourceLabelText(const juce::String& newSourceLabel)
{
    sourceLabelText_ = newSourceLabel;
    repaint();
}

void PianoRollComponent::setMidiSequence(const juce::MidiMessageSequence& sequence, int ticksPerQuarterNote)
{
    ticksPerQuarterNote_ = std::max(48, ticksPerQuarterNote);
    currentSequence_ = sequence;
    currentSequence_.updateMatchedPairs();

    notes_.clear();

    int lowestPitch = 127;
    int highestPitch = 0;
    double maxTick = static_cast<double>(ticksPerQuarterNote_) * 16.0;

    for (int i = 0; i < currentSequence_.getNumEvents(); ++i)
    {
        const auto* holder = currentSequence_.getEventPointer(i);
        if (holder == nullptr)
            continue;

        const auto& msg = holder->message;
        if (msg.isNoteOn())
        {
            const double startTick = msg.getTimeStamp();
            double endTick = startTick + static_cast<double>(ticksPerQuarterNote_) * 0.5;

            if (holder->noteOffObject != nullptr)
            {
                endTick = std::max(startTick + 60.0, holder->noteOffObject->message.getTimeStamp());
            }

            const int noteNum = msg.getNoteNumber();
            const uint8_t vel = msg.getVelocity();

            notes_.push_back(NoteRectData { noteNum, vel, startTick, endTick });

            lowestPitch = std::min(lowestPitch, noteNum);
            highestPitch = std::max(highestPitch, noteNum);
            maxTick = std::max(maxTick, endTick);
        }
    }

    if (!notes_.empty())
    {
        const double tpqn = static_cast<double>(ticksPerQuarterNote_);
        minDisplayPitch_ = std::clamp(lowestPitch - 4, 24, 72);
        // 上部にコードネームラベルおよびヘッダーを描画する余白として +6 半音分を確保
        maxDisplayPitch_ = std::clamp(highestPitch + 6, minDisplayPitch_ + 20, 108);
        totalDisplayTicks_ = std::max(tpqn * 8.0, std::ceil(maxTick / tpqn) * tpqn);
    }

    // 同時に発音している（ストラム遅延を含む）ノート群をコードブロックとしてクラスタリングし、コードネームを逆解析
    chordLabels_.clear();
    if (!notes_.empty())
    {
        std::vector<NoteRectData> sortedNotes = notes_;
        std::sort(sortedNotes.begin(), sortedNotes.end(),
            [](const NoteRectData& a, const NoteRectData& b)
            {
                return a.startTick < b.startTick;
            });

        // ストラム（アルペジオ遅延）を同一コードとして束ねる許容幅（約16分音符＝0.25拍分）
        const double clusterToleranceTicks = static_cast<double>(ticksPerQuarterNote_) * 0.25;

        size_t idx = 0;
        while (idx < sortedNotes.size())
        {
            const double blockStartTick = sortedNotes[idx].startTick;
            double blockEndTick = sortedNotes[idx].endTick;
            int blockHighestPitch = sortedNotes[idx].noteNumber;
            std::vector<int> clusterPitches;

            while (idx < sortedNotes.size()
                   && (sortedNotes[idx].startTick - blockStartTick) <= clusterToleranceTicks)
            {
                clusterPitches.push_back(sortedNotes[idx].noteNumber);
                blockEndTick = std::max(blockEndTick, sortedNotes[idx].endTick);
                blockHighestPitch = std::max(blockHighestPitch, sortedNotes[idx].noteNumber);
                ++idx;
            }

            ChordLabelData labelData;
            labelData.startTick = blockStartTick;
            labelData.endTick = blockEndTick;
            labelData.highestPitch = blockHighestPitch;
            labelData.chordName = engine::ChordAnalyzer::analyzeChordName(clusterPitches);

            chordLabels_.push_back(std::move(labelData));
        }
    }

    repaint();
}

juce::Rectangle<float> PianoRollComponent::getDragBadgeBounds() const noexcept
{
    const auto bounds = getLocalBounds().toFloat();
    constexpr float badgeWidth = 168.0f;
    constexpr float badgeHeight = 26.0f;
    return {
        bounds.getRight() - badgeWidth - 10.0f,
        bounds.getY() + 8.0f,
        badgeWidth,
        badgeHeight
    };
}

juce::File PianoRollComponent::exportSequenceToTempMidiFile() const
{
    if (currentSequence_.getNumEvents() == 0)
        return {};

    // 1. juce::MidiFile オブジェクトを生成し、タイムベース (TPQN) を設定
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ticksPerQuarterNote_);

    // 2. トラック用の MidiMessageSequence を準備して追加
    juce::MidiMessageSequence exportTrack(currentSequence_);
    exportTrack.updateMatchedPairs();
    midiFile.addTrack(exportTrack);

    // 3. OSの一時ディレクトリに TriLayerReharm_Export.mid を書き出し
    const juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const juce::File exportFile = tempDir.getChildFile("TriLayerReharm_Export.mid");

    if (exportFile.existsAsFile())
        exportFile.deleteFile();

    juce::FileOutputStream outputStream(exportFile);
    if (!outputStream.openedOk())
        return {};

    if (!midiFile.writeTo(outputStream))
        return {};

    outputStream.flush();
    return exportFile;
}

void PianoRollComponent::mouseMove(const juce::MouseEvent& event)
{
    const bool hoveringBadge = getDragBadgeBounds().contains(event.position);
    if (hoveringBadge != isHoveringDragBadge_)
    {
        isHoveringDragBadge_ = hoveringBadge;
        setMouseCursor(isHoveringDragBadge_ ? juce::MouseCursor::DraggingHandCursor
                                            : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void PianoRollComponent::mouseExit(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    if (isHoveringDragBadge_)
    {
        isHoveringDragBadge_ = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void PianoRollComponent::mouseDown(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    isDragExportInProgress_ = false;
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (isDragExportInProgress_)
        return;

    // 誤クリック防止のため、5px以上のドラッグ移動が検知された時点でD&Dを開始
    // （右上の「DRAG MIDI TO DAW」バッジ上、またはピアノロール領域からのドラッグで発動）
    if (event.getDistanceFromDragStart() < 5)
        return;

    isDragExportInProgress_ = true;

    const juce::File exportedMidiFile = exportSequenceToTempMidiFile();
    if (exportedMidiFile.existsAsFile())
    {
        juce::StringArray filesToDrag;
        filesToDrag.add(exportedMidiFile.getFullPathName());

        // OSネイティブの外部ファイルドラッグ＆ドロップを開始（Cubase等のタイムラインへ直接ドロップ可能）
        juce::DragAndDropContainer::performExternalDragDropOfFiles(filesToDrag, false, this);
    }
}

void PianoRollComponent::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    isDragExportInProgress_ = false;
}

void PianoRollComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xff0d0f14));
    g.fillRoundedRectangle(bounds, 8.0f);

    constexpr float kKeyHeaderWidth = 38.0f;
    const auto rollArea = bounds.withTrimmedLeft(kKeyHeaderWidth).reduced(2.0f);
    const auto keyHeaderArea = bounds.withWidth(kKeyHeaderWidth).reduced(2.0f, 2.0f);

    const int numPitches = std::max(1, maxDisplayPitch_ - minDisplayPitch_ + 1);
    const float rowHeight = rollArea.getHeight() / static_cast<float>(numPitches);

    // 1. 背景の白鍵・黒鍵グリッド描画
    for (int pitch = minDisplayPitch_; pitch <= maxDisplayPitch_; ++pitch)
    {
        const int rowIndexFromTop = maxDisplayPitch_ - pitch;
        const float y = rollArea.getY() + static_cast<float>(rowIndexFromTop) * rowHeight;

        const bool black = isBlackKey(pitch);
        const bool isOctaveRootC = ((pitch % 12) == 0);

        g.setColour(black ? juce::Colour(0xff12151e) : juce::Colour(0xff181c28));
        g.fillRect(rollArea.getX(), y, rollArea.getWidth(), rowHeight);

        g.setColour(isOctaveRootC ? juce::Colour(0xff333a4f) : juce::Colour(0xff202534));
        g.drawHorizontalLine(static_cast<int>(std::round(y + rowHeight)), rollArea.getX(), rollArea.getRight());

        g.setColour(black ? juce::Colour(0xff1e2230) : juce::Colour(0xff2c3244));
        g.fillRect(keyHeaderArea.getX(), y, keyHeaderArea.getWidth() - 2.0f, std::max(1.0f, rowHeight - 0.5f));

        if (isOctaveRootC)
        {
            const int octaveNum = (pitch / 12) - 1;
            g.setColour(juce::Colours::white.withAlpha(0.75f));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText("C" + juce::String(octaveNum),
                       juce::Rectangle<float>(keyHeaderArea.getX() + 3.0f, y - 2.0f, keyHeaderArea.getWidth() - 5.0f, std::max(rowHeight, 11.0f)),
                       juce::Justification::centredLeft,
                       false);
        }
    }

    // 2. 垂直グリッド（拍・小節線）
    const double ticksPerBeat = static_cast<double>(ticksPerQuarterNote_);
    const int numBeats = static_cast<int>(std::ceil(totalDisplayTicks_ / ticksPerBeat));
    for (int beat = 0; beat <= numBeats; ++beat)
    {
        const float normX = static_cast<float>((static_cast<double>(beat) * ticksPerBeat) / totalDisplayTicks_);
        const float x = rollArea.getX() + normX * rollArea.getWidth();
        const bool isBarLine = (beat % 4 == 0);

        g.setColour(isBarLine ? juce::Colour(0xff384057) : juce::Colour(0xff222736));
        g.drawVerticalLine(static_cast<int>(std::round(x)), rollArea.getY(), rollArea.getBottom());
    }

    // 3. MIDIノートの長方形描画（ベロシティをアルファ値へマッピング）
    for (const auto& note : notes_)
    {
        if (note.noteNumber < minDisplayPitch_ || note.noteNumber > maxDisplayPitch_)
            continue;

        const int rowIndexFromTop = maxDisplayPitch_ - note.noteNumber;
        const float y = rollArea.getY() + static_cast<float>(rowIndexFromTop) * rowHeight;

        const float startRatio = static_cast<float>(note.startTick / totalDisplayTicks_);
        const float endRatio   = static_cast<float>(note.endTick / totalDisplayTicks_);

        const float x = rollArea.getX() + startRatio * rollArea.getWidth();
        const float w = std::max(3.0f, (endRatio - startRatio) * rollArea.getWidth());
        const float h = std::max(2.0f, rowHeight - 1.0f);

        const float normVel = std::clamp(static_cast<float>(note.velocity) / 127.0f, 0.0f, 1.0f);
        const float alpha   = 0.28f + normVel * 0.70f;

        const juce::Rectangle<float> noteRect(x, y + 0.5f, w, h);

        g.setColour(accentColour_.withAlpha(alpha));
        g.fillRect(noteRect);

        g.setColour(juce::Colours::white.withAlpha(alpha * 0.55f));
        g.drawRect(noteRect, 1.0f);
    }

    // 3.5. 各コードブロックの真上にリアルタイム解析されたコードネームを描画
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    for (const auto& chordLabel : chordLabels_)
    {
        if (chordLabel.chordName.isEmpty())
            continue;

        const float startRatio = static_cast<float>(chordLabel.startTick / totalDisplayTicks_);
        const float endRatio   = static_cast<float>(chordLabel.endTick / totalDisplayTicks_);

        const float blockX = rollArea.getX() + startRatio * rollArea.getWidth();
        const float blockW = std::max(44.0f, (endRatio - startRatio) * rollArea.getWidth() - 4.0f);

        // トップノートの1段上に配置（上端ヘッダーと重ならないようY座標をクランプ）
        const int topRowIndex = maxDisplayPitch_ - chordLabel.highestPitch;
        const float noteTopY = rollArea.getY() + static_cast<float>(topRowIndex) * rowHeight;
        constexpr float kPillHeight = 19.0f;
        const float pillY = std::clamp(noteTopY - kPillHeight - 3.0f,
                                       rollArea.getY() + 32.0f,
                                       rollArea.getBottom() - kPillHeight - 4.0f);

        // テキスト幅に応じた半透明黒帯（ピル型背景）のサイズ計算
        const float estimatedTextW = std::max(46.0f, static_cast<float>(chordLabel.chordName.length()) * 7.8f + 16.0f);
        const float pillW = std::min(blockW, estimatedTextW);
        const juce::Rectangle<float> pillRect(blockX + 2.0f, pillY, pillW, kPillHeight);

        // 半透明の黒帯＋アクセントカラーの左端インジケーター＆枠線
        g.setColour(juce::Colour(0xff07090e).withAlpha(0.88f));
        g.fillRoundedRectangle(pillRect, 4.0f);

        g.setColour(accentColour_.withAlpha(0.85f));
        g.drawRoundedRectangle(pillRect, 4.0f, 1.0f);
        g.fillRoundedRectangle(pillRect.getX(), pillRect.getY(), 3.0f, pillRect.getHeight(), 2.0f);

        // 白色フォントでコードネームを描画
        g.setColour(juce::Colours::white);
        g.drawText(chordLabel.chordName,
                   pillRect.withTrimmedLeft(5.0f),
                   juce::Justification::centred,
                   false);
    }

    // 4. 右上の「DRAG MIDI TO DAW」エクスポートバッジ描画
    const auto badgeRect = getDragBadgeBounds();
    const juce::Colour badgeBg = isHoveringDragBadge_
                                     ? accentColour_.withAlpha(0.92f)
                                     : juce::Colour(0xff1b2030).withAlpha(0.92f);
    const juce::Colour badgeBorder = isHoveringDragBadge_
                                         ? juce::Colours::white
                                         : accentColour_.withAlpha(0.80f);

    g.setColour(badgeBg);
    g.fillRoundedRectangle(badgeRect, 6.0f);
    g.setColour(badgeBorder);
    g.drawRoundedRectangle(badgeRect, 6.0f, 1.2f);

    // ドラッググリップアイコン（6つのドット）を描画
    g.setColour(isHoveringDragBadge_ ? juce::Colours::black : accentColour_);
    const float gripStartX = badgeRect.getX() + 10.0f;
    const float gripStartY = badgeRect.getCentreY() - 4.0f;
    for (int col = 0; col < 2; ++col)
    {
        for (int row = 0; row < 3; ++row)
        {
            g.fillEllipse(gripStartX + static_cast<float>(col) * 4.5f,
                          gripStartY + static_cast<float>(row) * 3.8f,
                          2.2f,
                          2.2f);
        }
    }

    g.setColour(isHoveringDragBadge_ ? juce::Colours::black : juce::Colours::white);
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.drawText("DRAG MIDI TO DAW",
               badgeRect.withTrimmedLeft(22.0f),
               juce::Justification::centredLeft,
               false);

    // 5. 左上の現在のソースMIDI情報ラベル描画
    g.setColour(juce::Colour(0xff11131a).withAlpha(0.82f));
    g.fillRoundedRectangle(rollArea.getX() + 8.0f, rollArea.getY() + 6.0f, 310.0f, 22.0f, 4.0f);
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.setFont(juce::FontOptions(11.5f));
    g.drawText(sourceLabelText_,
               juce::Rectangle<float>(rollArea.getX() + 14.0f, rollArea.getY() + 6.0f, 298.0f, 22.0f),
               juce::Justification::centredLeft,
               true);

    // 6. 外部MIDIファイル (.mid) ドラッグオーバー時の視覚フィードバックオーバーレイ
    if (isFileDragHighlighted_)
    {
        g.setColour(accentColour_.withAlpha(0.22f));
        g.fillRoundedRectangle(bounds.reduced(1.5f), 8.0f);

        g.setColour(accentColour_.brighter(0.3f));
        g.drawRoundedRectangle(bounds.reduced(2.0f), 8.0f, 2.5f);

        const auto bannerRect = bounds.withSizeKeepingCentre(360.0f, 44.0f);
        g.setColour(juce::Colour(0xff0d0f14).withAlpha(0.92f));
        g.fillRoundedRectangle(bannerRect, 8.0f);
        g.setColour(accentColour_);
        g.drawRoundedRectangle(bannerRect, 8.0f, 1.5f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText("DROP MIDI FILE (.mid) TO REHARMONIZE",
                   bannerRect,
                   juce::Justification::centred,
                   false);
    }

    // コンポーネント外枠
    g.setColour(juce::Colour(0xff2b3042));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
}

} // namespace reharm::ui
