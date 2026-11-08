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

bool PianoRollComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& rawPath : files)
    {
        const auto trimmed = rawPath.trim().unquoted();
        if (trimmed.endsWithIgnoreCase(".mid") || trimmed.endsWithIgnoreCase(".midi"))
            return true;

        const juce::File f = juce::File::createFileWithoutCheckingPath(trimmed);
        const auto ext = f.getFileExtension();
        if (ext.equalsIgnoreCase(".mid") || ext.equalsIgnoreCase(".midi"))
            return true;
    }
    return false;
}

void PianoRollComponent::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(files, x, y);
    setFileDragHighlight(true);
}

void PianoRollComponent::fileDragMove(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(files, x, y);
    setFileDragHighlight(true);
}

void PianoRollComponent::fileDragExit(const juce::StringArray& files)
{
    juce::ignoreUnused(files);
    setFileDragHighlight(false);
}

void PianoRollComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x, y);
    setFileDragHighlight(false);

    if (onFilesDroppedCallback)
    {
        onFilesDroppedCallback(files);
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

    if (!notes_.empty() || !ghostNotes_.empty())
    {
        for (const auto& gn : ghostNotes_)
        {
            lowestPitch  = std::min(lowestPitch, gn.noteNumber);
            highestPitch = std::max(highestPitch, gn.noteNumber);
            maxTick      = std::max(maxTick, gn.endTick);
        }

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
            labelData.blockIndex = chordLabels_.size();
            labelData.startTick = blockStartTick;
            labelData.endTick = blockEndTick;
            labelData.highestPitch = blockHighestPitch;
            labelData.chordName = engine::ChordAnalyzer::analyzeChordName(clusterPitches);

            chordLabels_.push_back(std::move(labelData));
        }
    }

    repaint();
}

void PianoRollComponent::setGhostMidiSequence(const juce::MidiMessageSequence& sequence, int ticksPerQuarterNote)
{
    const int safeTpqn = std::max(48, ticksPerQuarterNote);
    juce::MidiMessageSequence matchedSeq(sequence);
    matchedSeq.updateMatchedPairs();

    ghostNotes_.clear();
    ghostNotes_.reserve(static_cast<size_t>(matchedSeq.getNumEvents()));

    int lowestPitch  = minDisplayPitch_ + 4;
    int highestPitch = maxDisplayPitch_ - 6;
    for (const auto& n : notes_)
    {
        lowestPitch  = std::min(lowestPitch, n.noteNumber);
        highestPitch = std::max(highestPitch, n.noteNumber);
    }

    for (int i = 0; i < matchedSeq.getNumEvents(); ++i)
    {
        const auto* holder = matchedSeq.getEventPointer(i);
        if (holder == nullptr)
            continue;

        const auto& msg = holder->message;
        if (msg.isNoteOn())
        {
            const double startTick = msg.getTimeStamp();
            double endTick = startTick + static_cast<double>(safeTpqn) * 0.5;

            if (holder->noteOffObject != nullptr)
            {
                endTick = std::max(startTick + 60.0, holder->noteOffObject->message.getTimeStamp());
            }

            const int noteNum = msg.getNoteNumber();
            const uint8_t vel = msg.getVelocity();

            ghostNotes_.push_back(NoteRectData { noteNum, vel, startTick, endTick });
            lowestPitch  = std::min(lowestPitch, noteNum);
            highestPitch = std::max(highestPitch, noteNum);
        }
    }

    if (!ghostNotes_.empty() || !notes_.empty())
    {
        minDisplayPitch_ = std::clamp(lowestPitch - 4, 24, 72);
        maxDisplayPitch_ = std::clamp(highestPitch + 6, minDisplayPitch_ + 20, 108);
    }

    repaint();
}

void PianoRollComponent::setPlayheadPosition(double ppqPosition, bool isPlaying)
{
    const bool playStateChanged = (isPlaying_ != isPlaying);
    const bool posChanged       = std::abs(playheadPpq_ - ppqPosition) > 0.008;

    playheadPpq_ = ppqPosition;
    isPlaying_   = isPlaying;

    if (playStateChanged || (isPlaying_ && posChanged))
    {
        repaint();
    }
}

void PianoRollComponent::setChordBlockViews(const std::vector<engine::ChordBlockViewInfo>& blockViews)
{
    if (blockViews.empty())
    {
        repaint();
        return;
    }

    chordLabels_.clear();
    chordLabels_.reserve(blockViews.size());

    const double tpqn = static_cast<double>(ticksPerQuarterNote_);

    for (const auto& view : blockViews)
    {
        ChordLabelData label;
        label.blockIndex     = view.blockIndex;
        label.startTick      = view.startPpq * tpqn;
        label.endTick        = (view.startPpq + view.durationPpq) * tpqn;
        label.isLocked       = view.isLocked;
        label.isPassingChord = view.isPassingChord;
        label.highestPitch   = view.midiNotes.empty()
                                   ? 72
                                   : *std::max_element(view.midiNotes.begin(), view.midiNotes.end());
        label.chordName      = engine::ChordAnalyzer::analyzeChordName(view.midiNotes);

        chordLabels_.push_back(std::move(label));
    }

    repaint();
}

int PianoRollComponent::findChordBlockIndexAtPosition(juce::Point<float> pos) const noexcept
{
    if (chordLabels_.empty())
        return -1;

    // 1. まずコードネーム・ロックピル矩形への直接ヒットを最優先で判定
    for (const auto& label : chordLabels_)
    {
        if (!label.lastPillRect.isEmpty() && label.lastPillRect.expanded(2.0f).contains(pos))
        {
            return static_cast<int>(label.blockIndex);
        }
    }

    // 2. ピアノロール領域内のコードブロック水平区間へのクリックを判定
    constexpr float kKeyHeaderWidth = 38.0f;
    const auto rollArea = getLocalBounds().toFloat().withTrimmedLeft(kKeyHeaderWidth).reduced(2.0f);
    if (!rollArea.contains(pos) || pos.y < rollArea.getY() + 30.0f)
        return -1;

    for (size_t i = 0; i < chordLabels_.size(); ++i)
    {
        const auto& label = chordLabels_[i];
        const float startRatio = static_cast<float>(label.startTick / totalDisplayTicks_);
        const float endRatio   = (i + 1 < chordLabels_.size())
                                     ? static_cast<float>(chordLabels_[i + 1].startTick / totalDisplayTicks_)
                                     : static_cast<float>(label.endTick / totalDisplayTicks_);

        const float blockX    = rollArea.getX() + startRatio * rollArea.getWidth();
        const float blockEndX = rollArea.getX() + endRatio * rollArea.getWidth();

        if (pos.x >= blockX && pos.x <= std::max(blockX + 8.0f, blockEndX))
        {
            return static_cast<int>(label.blockIndex);
        }
    }

    return -1;
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
    const int hoveredBlock   = hoveringBadge ? -1 : findChordBlockIndexAtPosition(event.position);

    if (hoveringBadge != isHoveringDragBadge_ || hoveredBlock != hoveredBlockIndex_)
    {
        isHoveringDragBadge_ = hoveringBadge;
        hoveredBlockIndex_   = hoveredBlock;

        if (isHoveringDragBadge_)
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        else if (hoveredBlockIndex_ >= 0)
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);

        repaint();
    }
}

void PianoRollComponent::mouseExit(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    if (isHoveringDragBadge_ || hoveredBlockIndex_ >= 0)
    {
        isHoveringDragBadge_ = false;
        hoveredBlockIndex_   = -1;
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
    const bool wasDragging = isDragExportInProgress_;
    isDragExportInProgress_ = false;

    if (!wasDragging && event.getDistanceFromDragStart() < 5 && !getDragBadgeBounds().contains(event.position))
    {
        const int targetBlockIdx = findChordBlockIndexAtPosition(event.position);
        if (targetBlockIdx >= 0)
        {
            // v1.5 UI/UX 2: 右クリック時は対象ブロックのボイシング展開形（転回形）またはオクターブ（Shift併用）をシフト
            if (event.mods.isPopupMenu())
            {
                if (onChordBlockVoicingShifted)
                {
                    if (event.mods.isShiftDown())
                        onChordBlockVoicingShifted(static_cast<size_t>(targetBlockIdx), 0, 1);
                    else
                        onChordBlockVoicingShifted(static_cast<size_t>(targetBlockIdx), 1, 0);
                }
            }
            // v1.5 機能B: 左クリック時は対象コードブロックの Lock（ピン留め）状態をトグル
            else if (onChordBlockLockToggled)
            {
                onChordBlockLockToggled(static_cast<size_t>(targetBlockIdx));
            }
        }
    }
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (std::abs(wheel.deltaY) <= 1.0e-4f || getDragBadgeBounds().contains(event.position))
        return;

    const int targetBlockIdx = findChordBlockIndexAtPosition(event.position);
    if (targetBlockIdx >= 0 && onChordBlockVoicingShifted)
    {
        const int stepDir = (wheel.deltaY > 0.0f) ? 1 : -1;
        // Shift または Ctrl/Cmd を押しながらホイールの場合はオクターブシフト、通常ホイールは展開形（転回形）シフト
        if (event.mods.isShiftDown() || event.mods.isCommandDown())
        {
            onChordBlockVoicingShifted(static_cast<size_t>(targetBlockIdx), 0, stepDir);
        }
        else
        {
            onChordBlockVoicingShifted(static_cast<size_t>(targetBlockIdx), stepDir, 0);
        }
    }
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

    // 1.5. v1.5: 再生中の現在Tick位置（総表示長でループ折り返し）を算出し、
    //      ロック（ピン留め）中、ホバー中、または現在再生中（発光エフェクト）のコードブロック列オーバーレイを描画
    const double totalDisplayPpq = std::max(1.0, totalDisplayTicks_ / static_cast<double>(ticksPerQuarterNote_));
    const double wrappedPlayheadPpq = (isPlaying_ && playheadPpq_ >= 0.0)
                                          ? std::fmod(playheadPpq_, totalDisplayPpq)
                                          : -1.0;
    const double activePlayheadTick = (wrappedPlayheadPpq >= 0.0)
                                          ? (wrappedPlayheadPpq * static_cast<double>(ticksPerQuarterNote_))
                                          : -1.0;

    for (size_t i = 0; i < chordLabels_.size(); ++i)
    {
        const auto& label = chordLabels_[i];
        const bool isHovered = (static_cast<int>(label.blockIndex) == hoveredBlockIndex_);
        const bool isActivePlayingBlock = (activePlayheadTick >= 0.0
                                           && activePlayheadTick >= label.startTick
                                           && activePlayheadTick < label.endTick);

        if (!label.isLocked && !isHovered && !isActivePlayingBlock)
            continue;

        const float startRatio = static_cast<float>(label.startTick / totalDisplayTicks_);
        const float endRatio   = static_cast<float>(label.endTick / totalDisplayTicks_);
        const float colX       = rollArea.getX() + startRatio * rollArea.getWidth();
        const float colW       = std::max(6.0f, (endRatio - startRatio) * rollArea.getWidth());

        const juce::Rectangle<float> colRect(colX, rollArea.getY() + 30.0f, colW, rollArea.getHeight() - 30.0f);

        // 1) 現在再生中のコードブロックの発光エフェクト（グラデーションビーム＋外枠グロー）
        if (isActivePlayingBlock)
        {
            juce::ColourGradient activeGlow(
                accentColour_.withAlpha(0.24f), colRect.getCentreX(), colRect.getY(),
                accentColour_.withAlpha(0.05f), colRect.getCentreX(), colRect.getBottom(),
                false
            );
            g.setGradientFill(activeGlow);
            g.fillRect(colRect);

            g.setColour(accentColour_.brighter(0.35f).withAlpha(0.85f));
            g.fillRect(colX, rollArea.getY() + 30.0f, colW, 3.0f);
            g.drawRect(colRect, 1.4f);
        }

        // 2) ロック中またはホバー中のハイライト
        if (label.isLocked)
        {
            g.setColour(juce::Colour(0xfffbbf24).withAlpha(isHovered ? 0.14f : 0.09f));
            g.fillRect(colRect);

            g.setColour(juce::Colour(0xfffbbf24).withAlpha(0.85f));
            g.fillRect(colX, rollArea.getY() + 30.0f, colW, 2.5f);
            g.drawVerticalLine(static_cast<int>(std::round(colX)), colRect.getY(), colRect.getBottom());
            g.drawVerticalLine(static_cast<int>(std::round(colRect.getRight())), colRect.getY(), colRect.getBottom());
        }
        else if (isHovered)
        {
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.fillRect(colRect);
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

    // 2.8. v1.5 UI強化3: 元MIDI（capturedSequence_）のゴーストノート表示
    //      リハモ結果ノートの背面に「薄い半透明の枠（ゴーストノート）」として描画し、変換前後の変化を可視化
    for (const auto& ghost : ghostNotes_)
    {
        if (ghost.noteNumber < minDisplayPitch_ || ghost.noteNumber > maxDisplayPitch_)
            continue;

        const int rowIndexFromTop = maxDisplayPitch_ - ghost.noteNumber;
        const float y = rollArea.getY() + static_cast<float>(rowIndexFromTop) * rowHeight;

        const float startRatio = static_cast<float>(ghost.startTick / totalDisplayTicks_);
        const float endRatio   = static_cast<float>(ghost.endTick / totalDisplayTicks_);

        const float x = rollArea.getX() + startRatio * rollArea.getWidth();
        const float w = std::max(3.0f, (endRatio - startRatio) * rollArea.getWidth());
        const float h = std::max(2.0f, rowHeight - 1.0f);

        const juce::Rectangle<float> ghostRect(x, y + 0.5f, w, h);

        g.setColour(juce::Colour(0xff94a3b8).withAlpha(0.08f));
        g.fillRect(ghostRect);

        g.setColour(juce::Colour(0xffcbd5e1).withAlpha(0.32f));
        g.drawRect(ghostRect, 1.0f);
    }

    // 3. MIDIノートの長方形描画（ベロシティをアルファ値へマッピング、ロック中のブロックはゴールド枠、再生中ノートは発光強調）
    for (const auto& note : notes_)
    {
        if (note.noteNumber < minDisplayPitch_ || note.noteNumber > maxDisplayPitch_)
            continue;

        bool belongsToLockedBlock = false;
        for (const auto& label : chordLabels_)
        {
            if (label.isLocked && note.startTick >= label.startTick - 40.0 && note.startTick <= label.endTick)
            {
                belongsToLockedBlock = true;
                break;
            }
        }

        const bool isNoteActiveNow = (activePlayheadTick >= 0.0
                                      && activePlayheadTick >= note.startTick
                                      && activePlayheadTick < note.endTick);

        const int rowIndexFromTop = maxDisplayPitch_ - note.noteNumber;
        const float y = rollArea.getY() + static_cast<float>(rowIndexFromTop) * rowHeight;

        const float startRatio = static_cast<float>(note.startTick / totalDisplayTicks_);
        const float endRatio   = static_cast<float>(note.endTick / totalDisplayTicks_);

        const float x = rollArea.getX() + startRatio * rollArea.getWidth();
        const float w = std::max(3.0f, (endRatio - startRatio) * rollArea.getWidth());
        const float h = std::max(2.0f, rowHeight - 1.0f);

        const float normVel = std::clamp(static_cast<float>(note.velocity) / 127.0f, 0.0f, 1.0f);
        const float alpha   = std::min(1.0f, (0.28f + normVel * 0.70f) + (isNoteActiveNow ? 0.22f : 0.0f));

        const juce::Rectangle<float> noteRect(x, y + 0.5f, w, h);

        if (isNoteActiveNow)
        {
            g.setColour(accentColour_.brighter(0.4f).withAlpha(0.30f));
            g.fillRect(noteRect.expanded(1.5f, 1.2f));
        }

        const juce::Colour fillCol = belongsToLockedBlock
                                         ? accentColour_.interpolatedWith(juce::Colour(0xfffbbf24), 0.35f)
                                         : (isNoteActiveNow ? accentColour_.brighter(0.25f) : accentColour_);
        g.setColour(fillCol.withAlpha(alpha));
        g.fillRect(noteRect);

        g.setColour(belongsToLockedBlock
                        ? juce::Colour(0xfffbbf24).withAlpha(0.95f)
                        : (isNoteActiveNow ? juce::Colours::white.withAlpha(0.95f)
                                           : juce::Colours::white.withAlpha(alpha * 0.55f)));
        g.drawRect(noteRect, (belongsToLockedBlock || isNoteActiveNow) ? 1.4f : 1.0f);
    }

    // 3.5. v1.5 機能B & C: 各コードブロックの上部にコードネーム（"Dm9", "G7alt"等）とLockアイコンを描画
    //      （パッシングコード細分化時の文字の重なりを防ぐため、スロット幅制限＋2段スタッガー回避＋縮小フィットを適用）
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    std::vector<juce::Rectangle<float>> placedPillRects;
    placedPillRects.reserve(chordLabels_.size());

    for (size_t i = 0; i < chordLabels_.size(); ++i)
    {
        const auto& chordLabel = chordLabels_[i];
        chordLabel.lastPillRect = {};
        if (chordLabel.chordName.isEmpty())
            continue;

        const float startRatio = static_cast<float>(chordLabel.startTick / totalDisplayTicks_);
        const float endRatio   = static_cast<float>(chordLabel.endTick / totalDisplayTicks_);
        const float nextStartRatio = (i + 1 < chordLabels_.size())
                                         ? static_cast<float>(chordLabels_[i + 1].startTick / totalDisplayTicks_)
                                         : endRatio;

        const float blockX = rollArea.getX() + startRatio * rollArea.getWidth();
        const float slotRightX = rollArea.getX() + std::max(endRatio, nextStartRatio) * rollArea.getWidth();
        const float availableSlotW = std::max(28.0f, slotRightX - blockX - 2.0f);

        // トップノートの1段上に配置（上端ステータスバーと重ならないよう Y >= rollArea.getY() + 33.0f に制限）
        const int topRowIndex = maxDisplayPitch_ - chordLabel.highestPitch;
        const float noteTopY = rollArea.getY() + static_cast<float>(topRowIndex) * rowHeight;
        constexpr float kPillHeight = 19.0f;
        const float minPillY = rollArea.getY() + 33.0f;
        const float maxPillY = rollArea.getBottom() - kPillHeight - 4.0f;

        float pillY = std::clamp(noteTopY - kPillHeight - 3.0f, minPillY, maxPillY);

        // 文字列長＋ロックアイコン分の希望幅を計算しつつ、ピアノロール右端を超えないようクランプ
        const float desiredTextW = std::max(44.0f, static_cast<float>(chordLabel.chordName.length()) * 7.2f + 24.0f);
        const float maxAllowedW  = std::max(32.0f, std::min(availableSlotW + 18.0f, rollArea.getRight() - blockX - 3.0f));
        const float pillW        = std::clamp(desiredTextW, 32.0f, maxAllowedW);

        juce::Rectangle<float> pillRect(blockX + 1.5f, pillY, pillW, kPillHeight);

        // 直前に配置されたコードネームピルと矩形が重なる場合、上下に1段ずらして重なりを回避
        for (const auto& prevRect : placedPillRects)
        {
            if (pillRect.expanded(1.5f, 1.0f).intersects(prevRect))
            {
                if (pillY + kPillHeight + 3.0f <= maxPillY)
                    pillY += (kPillHeight + 2.5f);
                else if (pillY - kPillHeight - 3.0f >= minPillY)
                    pillY -= (kPillHeight + 2.5f);
                else
                    pillRect.setWidth(std::max(28.0f, availableSlotW));

                pillRect.setY(pillY);
            }
        }

        placedPillRects.push_back(pillRect);
        chordLabel.lastPillRect = pillRect;

        const bool isHovered = (static_cast<int>(chordLabel.blockIndex) == hoveredBlockIndex_);
        const bool isActivePlayingBlock = (activePlayheadTick >= 0.0
                                           && activePlayheadTick >= chordLabel.startTick
                                           && activePlayheadTick < chordLabel.endTick);

        if (isActivePlayingBlock)
        {
            g.setColour(accentColour_.brighter(0.3f).withAlpha(0.38f));
            g.fillRoundedRectangle(pillRect.expanded(2.0f, 1.8f), 5.0f);
        }

        const juce::Colour borderCol = chordLabel.isLocked
                                           ? juce::Colour(0xfffbbf24)
                                           : (isActivePlayingBlock
                                                  ? juce::Colours::white
                                                  : (isHovered ? juce::Colours::white : accentColour_.withAlpha(0.85f)));
        const juce::Colour bgCol = chordLabel.isLocked
                                       ? juce::Colour(0xff231c09).withAlpha(0.94f)
                                       : (isActivePlayingBlock
                                              ? accentColour_.darker(0.55f).withAlpha(0.96f)
                                              : juce::Colour(0xff07090e).withAlpha(isHovered ? 0.96f : 0.88f));

        // 半透明の黒帯＋左端インジケーター＆枠線
        g.setColour(bgCol);
        g.fillRoundedRectangle(pillRect, 4.0f);

        g.setColour(borderCol);
        g.drawRoundedRectangle(pillRect, 4.0f, (chordLabel.isLocked || isActivePlayingBlock) ? 1.5f : 1.0f);
        g.fillRoundedRectangle(pillRect.getX(), pillRect.getY(), 3.0f, pillRect.getHeight(), 2.0f);

        // 右端に小さなロック（南京錠）アイコンを描画（幅に余裕がある場合）
        float textRightTrim = 2.0f;
        if (pillRect.getWidth() >= 42.0f)
        {
            textRightTrim = 13.0f;
            const float iconX = pillRect.getRight() - 10.5f;
            const float iconY = pillRect.getCentreY() - 3.5f;
            const juce::Colour lockCol = chordLabel.isLocked
                                             ? juce::Colour(0xfffbbf24)
                                             : juce::Colours::white.withAlpha(isHovered ? 0.80f : 0.32f);
            g.setColour(lockCol);
            // 南京錠のボディ
            g.fillRoundedRectangle(iconX, iconY + 3.2f, 6.5f, 5.0f, 1.2f);
            // 南京錠のツル（ロック時は閉じ、アンロック時は少し開く）
            juce::Path shackle;
            if (chordLabel.isLocked)
            {
                shackle.addCentredArc(iconX + 3.25f, iconY + 3.2f, 2.0f, 2.4f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            }
            else
            {
                shackle.addCentredArc(iconX + 3.25f, iconY + 2.2f, 2.0f, 2.4f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi * 0.25f, true);
            }
            g.strokePath(shackle, juce::PathStrokeType(1.1f));
        }

        // コードネーム文字列の描画（狭いブロックでも重ならないよう drawFittedText で自動縮小）
        g.setColour(chordLabel.isLocked ? juce::Colour(0xfffef08a) : juce::Colours::white);
        const auto textBounds = pillRect.withTrimmedLeft(4.5f).withTrimmedRight(textRightTrim).toNearestInt();
        g.drawFittedText(chordLabel.chordName,
                         textBounds,
                         juce::Justification::centred,
                         1,
                         0.65f);
    }

    // 3.8. v1.5 UI強化1: リアルタイム・プレイヘッド表示（現在再生位置の縦線カーソル＋上部マーカー＋発光ビーム）
    if (activePlayheadTick >= 0.0)
    {
        const float playheadNormX = static_cast<float>(activePlayheadTick / totalDisplayTicks_);
        const float playheadX = rollArea.getX() + playheadNormX * rollArea.getWidth();

        if (playheadX >= rollArea.getX() && playheadX <= rollArea.getRight())
        {
            // カーソル周囲のソフトグロー
            g.setColour(accentColour_.brighter(0.4f).withAlpha(0.26f));
            g.fillRect(playheadX - 3.0f, rollArea.getY(), 6.0f, rollArea.getHeight());

            // プレイヘッド縦線本体
            g.setColour(juce::Colours::white.withAlpha(0.95f));
            g.fillRect(playheadX - 0.8f, rollArea.getY(), 1.6f, rollArea.getHeight());

            // 上端の逆三角形プレイヘッドマーカー
            juce::Path headMarker;
            headMarker.addTriangle(playheadX - 5.5f, rollArea.getY() + 1.0f,
                                   playheadX + 5.5f, rollArea.getY() + 1.0f,
                                   playheadX,        rollArea.getY() + 8.5f);
            g.setColour(accentColour_.brighter(0.35f));
            g.fillPath(headMarker);
            g.setColour(juce::Colours::white);
            g.strokePath(headMarker, juce::PathStrokeType(1.0f));
        }
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
