#include "MidiReharmonizerEngine.h"
#include "ChordAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <set>

namespace reharm::engine
{

namespace
{
    /**
     * @brief コードクオリティに対応するルートからの相対ピッチクラス集合（半音単位）を返す
     */
    struct ChordIntervals
    {
        std::vector<int> coreIntervals;    ///< ルート、3rd、5th、7th
        std::vector<int> tensionIntervals; ///< 9th、11th、13th などのテンション
        int thirdInterval { -1 };          ///< ガイドトーン判定用の3rd（ない場合は -1）
        int seventhInterval { -1 };        ///< ガイドトーン判定用の7th（ない場合は -1）
    };

    [[nodiscard]] ChordIntervals getChordIntervals(ChordQuality quality)
    {
        switch (quality)
        {
            case ChordQuality::MajorTriad:
                return { { 0, 4, 7 }, { 2, 9 }, 4, -1 };
            case ChordQuality::MinorTriad:
                return { { 0, 3, 7 }, { 2, 5 }, 3, -1 };
            case ChordQuality::DiminishedTriad:
                return { { 0, 3, 6 }, {}, 3, -1 };
            case ChordQuality::AugmentedTriad:
                return { { 0, 4, 8 }, { 2 }, 4, -1 };
            case ChordQuality::Major7:
                return { { 0, 4, 7, 11 }, { 2, 9 }, 4, 11 };
            case ChordQuality::Minor7:
                return { { 0, 3, 7, 10 }, { 2, 5 }, 3, 10 };
            case ChordQuality::Dominant7:
                return { { 0, 4, 7, 10 }, { 2, 9 }, 4, 10 };
            case ChordQuality::Minor7b5:
                return { { 0, 3, 6, 10 }, { 5, 8 }, 3, 10 };
            case ChordQuality::Diminished7:
                return { { 0, 3, 6, 9 }, {}, 3, 9 };
            case ChordQuality::MinorMajor7:
                return { { 0, 3, 7, 11 }, { 2 }, 3, 11 };
            case ChordQuality::Sus4:
                return { { 0, 5, 7 }, { 2 }, 5, -1 };
            case ChordQuality::Dominant7Sus4:
                return { { 0, 5, 7, 10 }, { 2 }, 5, 10 };
        }
        return { { 0, 4, 7, 11 }, {}, 4, 11 };
    }

    /**
     * @brief ピッチクラス（0〜11）を正規化するヘルパー
     */
    [[nodiscard]] constexpr int normalizePitchClass(int noteOrPc) noexcept
    {
        const int r = noteOrPc % 12;
        return r < 0 ? r + 12 : r;
    }

    /**
     * @brief v1.5 機能I: 指定されたスケール／モード種別を構成するルートからのインターバル配列（7音）を返す
     */
    [[nodiscard]] constexpr std::array<int, 7> getScaleIntervals(ScaleType scaleType) noexcept
    {
        switch (scaleType)
        {
            case ScaleType::MajorIonian:
                return { 0, 2, 4, 5, 7, 9, 11 };
            case ScaleType::NaturalMinorAeolian:
                return { 0, 2, 3, 5, 7, 8, 10 };
            case ScaleType::HarmonicMinor:
                return { 0, 2, 3, 5, 7, 8, 11 };
            case ScaleType::MelodicMinor:
                return { 0, 2, 3, 5, 7, 9, 11 };
            case ScaleType::Dorian:
                return { 0, 2, 3, 5, 7, 9, 10 };
            case ScaleType::Mixolydian:
                return { 0, 2, 4, 5, 7, 9, 10 };
            case ScaleType::Lydian:
                return { 0, 2, 4, 6, 7, 9, 11 };
        }
        return { 0, 2, 4, 5, 7, 9, 11 };
    }

    /**
     * @brief 入力された構成音集合（ピッチクラス集合）から最も尤もらしいルートとコードクオリティを簡易推定する
     */
    void inferChordFromNotes(const std::vector<int>& notes, int& outRootPc, ChordQuality& outQuality)
    {
        if (notes.empty())
        {
            outRootPc = 0;
            outQuality = ChordQuality::Major7;
            return;
        }

        // 最低音を仮のルートとする
        const int bassNote = *std::min_element(notes.begin(), notes.end());
        outRootPc = normalizePitchClass(bassNote);

        std::array<bool, 12> hasInterval {};
        for (int n : notes)
        {
            const int rel = normalizePitchClass(n - outRootPc);
            hasInterval[static_cast<size_t>(rel)] = true;
        }

        const bool hasMaj3 = hasInterval[4];
        const bool hasMin3 = hasInterval[3];
        const bool hasP4   = hasInterval[5];
        const bool hasDim5 = hasInterval[6];
        const bool hasMaj7 = hasInterval[11];
        const bool hasMin7 = hasInterval[10];

        if (hasMin3 && hasDim5 && hasMin7)
            outQuality = ChordQuality::Minor7b5;
        else if (hasMin3 && hasMaj7)
            outQuality = ChordQuality::MinorMajor7;
        else if (hasMin3 && hasMin7)
            outQuality = ChordQuality::Minor7;
        else if (hasMaj3 && hasMin7)
            outQuality = ChordQuality::Dominant7;
        else if (hasMaj3 && hasMaj7)
            outQuality = ChordQuality::Major7;
        else if (!hasMaj3 && !hasMin3 && hasP4 && hasMin7)
            outQuality = ChordQuality::Dominant7Sus4;
        else if (!hasMaj3 && !hasMin3 && hasP4)
            outQuality = ChordQuality::Sus4;
        else if (hasMin3)
            outQuality = ChordQuality::Minor7; // リハーモナイズ用に7thへ拡張
        else if (hasMaj3)
            outQuality = ChordQuality::MajorTriad;
        else
            outQuality = ChordQuality::Major7;
    }
} // anonymous namespace

//==============================================================================
MidiReharmonizerEngine::MidiReharmonizerEngine()
    : activeProfile_(synthesizeVoicingProfile(GenreRules {}, ArtistRules {}, EnvironmentRules {}))
{
    capturedSequence_.reserve(4096);
    reharmonizedSequence_.reserve(4096);
    grooveMapPpq_.reserve(kMaxGrooveMapSize);
}

void MidiReharmonizerEngine::prepare(double sampleRate, int maxBlockSize) noexcept
{
    currentSampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentMaxBlockSize_ = maxBlockSize;
}

void MidiReharmonizerEngine::reset() noexcept
{
}

void MidiReharmonizerEngine::setOperatingMode(OperatingMode newMode) noexcept
{
    mode_.store(newMode, std::memory_order_release);
}

OperatingMode MidiReharmonizerEngine::getOperatingMode() const noexcept
{
    return mode_.load(std::memory_order_acquire);
}

void MidiReharmonizerEngine::clearCapturedData()
{
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    capturedSequence_.clear();
}

void MidiReharmonizerEngine::setSourceMidiSequence(
    const juce::MidiMessageSequence& sequence,
    double ticksPerQuarterNote)
{
    const double safeTpqn = ticksPerQuarterNote > 0.0 ? ticksPerQuarterNote : 960.0;

    // v1.5 フェイルセーフ1: 巨大MIDIファイルD&D時のフリーズ防止用ハードリミット（最大8小節 = 32.0 PPQ、終端NoteOff猶予 = 33.0 PPQ）
    constexpr double kMaxImportSpanPpq    = 32.0;
    constexpr double kMaxImportNoteOffPpq = 33.0;

    std::vector<TimedMidiEvent> importedEvents;
    importedEvents.reserve(std::min<size_t>(static_cast<size_t>(sequence.getNumEvents()), 2048));

    double firstNoteOnPpq = std::numeric_limits<double>::max();
    int importedNoteOnCount = 0;

    for (int i = 0; i < sequence.getNumEvents(); ++i)
    {
        const auto* holder = sequence.getEventPointer(i);
        if (holder == nullptr)
            continue;

        const auto& rawMsg = holder->message;
        if (!rawMsg.isNoteOnOrOff() && !rawMsg.isController() && !rawMsg.isPitchWheel())
            continue;

        // Running Status 等で Velocity 0 の NoteOn が来た場合は明示的な NoteOff へ正規化
        juce::MidiMessage normalizedMsg(rawMsg);
        if (rawMsg.isNoteOn(true) && rawMsg.getVelocity() == 0)
        {
            normalizedMsg = juce::MidiMessage::noteOff(rawMsg.getChannel(), rawMsg.getNoteNumber());
            normalizedMsg.setTimeStamp(rawMsg.getTimeStamp());
        }

        const double eventPpq = std::max(0.0, normalizedMsg.getTimeStamp() / safeTpqn);
        if (normalizedMsg.isNoteOn())
        {
            if (firstNoteOnPpq == std::numeric_limits<double>::max())
            {
                firstNoteOnPpq = eventPpq;
            }
            else
            {
                firstNoteOnPpq = std::min(firstNoteOnPpq, eventPpq);
            }

            // 最初の NoteOn（firstNoteOnPpq）から 8小節（32.0 PPQ）を超える新規 NoteOn はインポートせず切り捨て
            if ((eventPpq - firstNoteOnPpq) >= kMaxImportSpanPpq)
                continue;

            ++importedNoteOnCount;
        }
        else if (firstNoteOnPpq < std::numeric_limits<double>::max())
        {
            // 終端の NoteOff 等も firstNoteOnPpq + 33.0 PPQ を超えるものは無視（切り捨て）
            if ((eventPpq - firstNoteOnPpq) > kMaxImportNoteOffPpq)
                continue;
        }

        TimedMidiEvent ev;
        ev.ppqPosition = eventPpq;
        ev.samplePosition = 0;
        ev.message = normalizedMsg;
        importedEvents.push_back(ev);
    }

    // DAWの途中小節（例: 9小節目 = 32.0 PPQ等）から書き出されたMIDIファイルの場合、
    // 先頭のノートが第1小節（0.0 PPQ付近）から始まるようにオフセットを正規化
    if (firstNoteOnPpq < std::numeric_limits<double>::max() && firstNoteOnPpq >= 1.0)
    {
        const double barAlignedOffsetPpq = (firstNoteOnPpq >= 4.0)
                                               ? (std::floor(firstNoteOnPpq / 4.0) * 4.0)
                                               : std::floor(firstNoteOnPpq);
        if (barAlignedOffsetPpq > 0.0)
        {
            for (auto& ev : importedEvents)
            {
                ev.ppqPosition = std::max(0.0, ev.ppqPosition - barAlignedOffsetPpq);
            }
        }
    }

    // オフセット正規化後の 0.0〜32.0 PPQ（NoteOffは最大33.0 PPQ）範囲外イベントを最終クランプ・除去
    importedEvents.erase(
        std::remove_if(importedEvents.begin(), importedEvents.end(),
            [](const TimedMidiEvent& ev)
            {
                if (ev.message.isNoteOn())
                    return ev.ppqPosition >= kMaxImportSpanPpq;
                return ev.ppqPosition > kMaxImportNoteOffPpq;
            }),
        importedEvents.end());

    // タイムスタンプ（PPQ）昇順にソート（同一時刻の場合は NoteOff を NoteOn より先に処理）
    std::stable_sort(importedEvents.begin(), importedEvents.end(),
        [](const TimedMidiEvent& a, const TimedMidiEvent& b)
        {
            if (std::abs(a.ppqPosition - b.ppqPosition) > 1.0e-6)
                return a.ppqPosition < b.ppqPosition;
            return a.message.isNoteOff() && !a.message.isNoteOn() && b.message.isNoteOn();
        });

    DBG("MidiReharmonizerEngine::setSourceMidiSequence: Loaded "
        << importedEvents.size() << " events (" << importedNoteOnCount
        << " NoteOns), TPQN=" << safeTpqn);

    {
        const juce::SpinLock::ScopedLockType lock(sequenceLock_);
        capturedSequence_ = std::move(importedEvents);
    }
}

void MidiReharmonizerEngine::updateRules(const GenreRules& genre,
                                         const ArtistRules& artist,
                                         const EnvironmentRules& env)
{
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    activeProfile_ = synthesizeVoicingProfile(genre, artist, env);
    activeProfile_.mutationAmount    = mutationAmount_.load(std::memory_order_relaxed);
    activeProfile_.melodyLock        = melodyLock_.load(std::memory_order_relaxed);
    activeProfile_.forceDiatonic     = forceDiatonic_.load(std::memory_order_relaxed);
    activeProfile_.songKeyPitchClass = songKeyPitchClass_.load(std::memory_order_relaxed);
    activeProfile_.scaleType         = static_cast<ScaleType>(
        std::clamp(scaleType_.load(std::memory_order_relaxed), 0, 6));
    activeProfile_.pedalBassMode     = pedalBassMode_.load(std::memory_order_relaxed);
    activeProfile_.compingStyle      = static_cast<CompingStyle>(
        std::clamp(compingStyle_.load(std::memory_order_relaxed), 0, 2));
    activeProfile_.splitMidiChannels = splitMidiChannels_.load(std::memory_order_relaxed);
}

void MidiReharmonizerEngine::setMutationAmount(float mutationPercent) noexcept
{
    const float clamped = std::clamp(mutationPercent, 0.0f, 100.0f);
    mutationAmount_.store(clamped, std::memory_order_release);

    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    activeProfile_.mutationAmount = clamped;
}

float MidiReharmonizerEngine::getMutationAmount() const noexcept
{
    return mutationAmount_.load(std::memory_order_acquire);
}

void MidiReharmonizerEngine::setTransposeSemitones(int semitones) noexcept
{
    const int clamped = std::clamp(semitones, -12, 12);
    transposeSemitones_.store(clamped, std::memory_order_release);

    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    const int delta = clamped - renderedTransposeSemitones_;
    if (delta != 0 && !reharmonizedSequence_.empty())
    {
        for (auto& ev : reharmonizedSequence_)
        {
            if (ev.message.isNoteOnOrOff())
            {
                const int shiftedNote = std::clamp(ev.message.getNoteNumber() + delta, 0, 127);
                ev.message.setNoteNumber(shiftedNote);
            }
        }
        renderedTransposeSemitones_ = clamped;
    }
}

int MidiReharmonizerEngine::getTransposeSemitones() const noexcept
{
    return transposeSemitones_.load(std::memory_order_acquire);
}

bool MidiReharmonizerEngine::toggleChordBlockLockByIndex(size_t blockIndex)
{
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    if (blockIndex >= lastProgression_.size() || blockIndex >= lastOptimalVoicings_.size())
        return false;

    auto& block = lastProgression_[blockIndex];
    const double startPpq = block.startPpq;
    const double endPpq   = block.startPpq + block.durationPpq;

    // 既にロックされている場合はロック解除
    auto it = std::find_if(lockedBlocks_.begin(), lockedBlocks_.end(),
        [&](const ChordBlock& locked)
        {
            return std::abs(locked.startPpq - startPpq) < 0.12
                && std::abs((locked.startPpq + locked.durationPpq) - endPpq) < 0.12;
        });

    if (it != lockedBlocks_.end())
    {
        lockedBlocks_.erase(it);
        block.isLocked = false;
        block.lockedVoicing = {};
        return false;
    }

    // 新規ロック登録：直近のViterbi計算結果のボイシングを保存して固定
    block.isLocked = true;
    block.lockedVoicing = lastOptimalVoicings_[blockIndex];
    lockedBlocks_.push_back(block);

    std::sort(lockedBlocks_.begin(), lockedBlocks_.end(),
        [](const ChordBlock& a, const ChordBlock& b)
        {
            return a.startPpq < b.startPpq;
        });

    return true;
}

bool MidiReharmonizerEngine::toggleChordBlockLockAtPpq(double ppqPosition)
{
    size_t targetIndex = std::numeric_limits<size_t>::max();
    {
        const juce::SpinLock::ScopedLockType lock(sequenceLock_);
        for (size_t i = 0; i < lastProgression_.size(); ++i)
        {
            const double start = lastProgression_[i].startPpq;
            const double end   = start + lastProgression_[i].durationPpq + 0.10;
            if (ppqPosition >= start - 0.10 && ppqPosition <= end)
            {
                targetIndex = i;
                break;
            }
        }
    }

    if (targetIndex == std::numeric_limits<size_t>::max())
        return false;

    return toggleChordBlockLockByIndex(targetIndex);
}

void MidiReharmonizerEngine::clearLockedChordBlocks()
{
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    lockedBlocks_.clear();
    for (auto& block : lastProgression_)
    {
        block.isLocked = false;
        block.lockedVoicing = {};
    }
}

bool MidiReharmonizerEngine::hasLockedChordBlocks() const noexcept
{
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    return !lockedBlocks_.empty();
}

std::vector<ChordBlock> MidiReharmonizerEngine::getLockedChordBlocksSnapshot() const
{
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    return lockedBlocks_;
}

void MidiReharmonizerEngine::setLockedChordBlocksSnapshot(std::vector<ChordBlock> lockedBlocks)
{
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    lockedBlocks_ = std::move(lockedBlocks);
}

void MidiReharmonizerEngine::setCompareOriginalMode(bool compareOriginal) noexcept
{
    compareOriginalMode_.store(compareOriginal, std::memory_order_release);
}

bool MidiReharmonizerEngine::isCompareOriginalMode() const noexcept
{
    return compareOriginalMode_.load(std::memory_order_acquire);
}

std::vector<ChordBlockViewInfo> MidiReharmonizerEngine::getChordBlockViews() const
{
    std::vector<ChordBlockViewInfo> views;
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);

    const size_t count = std::min(lastProgression_.size(), lastOptimalVoicings_.size());
    views.reserve(count);

    const int transposeShift = std::clamp(transposeSemitones_.load(std::memory_order_relaxed), -12, 12);

    for (size_t i = 0; i < count; ++i)
    {
        const auto& block = lastProgression_[i];
        const auto& voicing = lastOptimalVoicings_[i];

        ChordBlockViewInfo info;
        info.blockIndex     = i;
        info.startPpq       = block.startPpq;
        info.durationPpq    = block.durationPpq;
        info.isLocked       = block.isLocked;
        info.isPassingChord = block.isPassingChord;
        info.midiNotes.reserve(voicing.midiNotes.size());

        for (int n : voicing.midiNotes)
        {
            info.midiNotes.push_back(std::clamp(n + transposeShift, 0, 127));
        }

        views.push_back(std::move(info));
    }

    return views;
}

bool MidiReharmonizerEngine::shiftChordBlockVoicing(size_t blockIndex, int inversionSteps, int octaveShift)
{
    {
        const juce::SpinLock::ScopedLockType lock(sequenceLock_);
        if (blockIndex >= lastProgression_.size() || blockIndex >= lastOptimalVoicings_.size())
            return false;

        Voicing shifted = lastOptimalVoicings_[blockIndex];
        if (shifted.midiNotes.empty())
            return false;

        std::sort(shifted.midiNotes.begin(), shifted.midiNotes.end());
        const bool keepPedalBass = activeProfile_.pedalBassMode && shifted.midiNotes.size() >= 2;
        const size_t rotateStartIdx = keepPedalBass ? 1 : 0;

        // 1. 展開形（転回形）のシフト（+1: 最低音をオクターブ上げて上へ展開 / -1: 最高音をオクターブ下げて下へ展開）
        if (inversionSteps > 0)
        {
            for (int step = 0; step < inversionSteps; ++step)
            {
                int movedNote = shifted.midiNotes[rotateStartIdx];
                const int currentTop = shifted.midiNotes.back();
                while (movedNote <= currentTop)
                    movedNote += 12;

                // 音域上限を超えすぎる場合はボイシング全体を1オクターブ下げて自然な音域内で循環させる
                if (movedNote > std::min(104, activeProfile_.maxPitch + 12))
                {
                    for (size_t i = rotateStartIdx; i < shifted.midiNotes.size(); ++i)
                        shifted.midiNotes[i] = std::max(24, shifted.midiNotes[i] - 12);
                    movedNote = std::max(24, movedNote - 12);
                }

                shifted.midiNotes[rotateStartIdx] = std::clamp(movedNote, 24, 108);
                std::sort(shifted.midiNotes.begin() + static_cast<std::ptrdiff_t>(rotateStartIdx), shifted.midiNotes.end());
            }
        }
        else if (inversionSteps < 0)
        {
            for (int step = 0; step < -inversionSteps; ++step)
            {
                int movedNote = shifted.midiNotes.back();
                const int currentBottom = shifted.midiNotes[rotateStartIdx];
                while (movedNote >= currentBottom)
                    movedNote -= 12;

                // 音域下限を下回りすぎる場合はボイシング全体を1オクターブ上げて自然な音域内で循環させる
                if (movedNote < std::max(28, activeProfile_.minPitch - 12))
                {
                    for (size_t i = rotateStartIdx; i < shifted.midiNotes.size(); ++i)
                        shifted.midiNotes[i] = std::min(108, shifted.midiNotes[i] + 12);
                    movedNote = std::min(108, movedNote + 12);
                }

                shifted.midiNotes.back() = std::clamp(movedNote, 24, 108);
                std::sort(shifted.midiNotes.begin() + static_cast<std::ptrdiff_t>(rotateStartIdx), shifted.midiNotes.end());
            }
        }

        // 2. オクターブ単位のシフト（+1 = +12半音, -1 = -12半音）
        if (octaveShift != 0)
        {
            const int semitoneDelta = octaveShift * 12;
            for (size_t i = rotateStartIdx; i < shifted.midiNotes.size(); ++i)
            {
                shifted.midiNotes[i] = std::clamp(shifted.midiNotes[i] + semitoneDelta, 24, 108);
            }
            std::sort(shifted.midiNotes.begin(), shifted.midiNotes.end());
        }

        auto& block = lastProgression_[blockIndex];
        block.isLocked = true;
        block.lockedVoicing = shifted;
        lastOptimalVoicings_[blockIndex] = shifted;

        const double startPpq = block.startPpq;
        const double endPpq   = block.startPpq + block.durationPpq;

        auto it = std::find_if(lockedBlocks_.begin(), lockedBlocks_.end(),
            [&](const ChordBlock& locked)
            {
                return std::abs(locked.startPpq - startPpq) < 0.12
                    && std::abs((locked.startPpq + locked.durationPpq) - endPpq) < 0.12;
            });

        if (it != lockedBlocks_.end())
        {
            *it = block;
        }
        else
        {
            lockedBlocks_.push_back(block);
            std::sort(lockedBlocks_.begin(), lockedBlocks_.end(),
                [](const ChordBlock& a, const ChordBlock& b)
                {
                    return a.startPpq < b.startPpq;
                });
        }
    }

    return triggerOfflineReharmonize(true);
}

bool MidiReharmonizerEngine::hasCapturedSequence() const noexcept
{
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    return !capturedSequence_.empty();
}

std::unique_ptr<juce::XmlElement> MidiReharmonizerEngine::createEngineStateXml() const
{
    std::vector<TimedMidiEvent> capturedCopy;
    std::vector<ChordBlock> lockedCopy;

    {
        const juce::SpinLock::ScopedLockType lock(sequenceLock_);
        capturedCopy = capturedSequence_;
        lockedCopy   = lockedBlocks_;
    }

    auto engineXml = std::make_unique<juce::XmlElement>("EngineState");
    engineXml->setAttribute("version", "1.5");

    // 1. capturedSequence_（インポート／キャプチャ済みの元MIDIデータ）のシリアライズ
    auto* capturedXml = engineXml->createNewChildElement("CapturedSequence");
    for (const auto& ev : capturedCopy)
    {
        const int rawSize = ev.message.getRawDataSize();
        if (rawSize <= 0 || ev.message.getRawData() == nullptr)
            continue;

        auto* evXml = capturedXml->createNewChildElement("Event");
        evXml->setAttribute("ppq", ev.ppqPosition);
        evXml->setAttribute("sample", juce::String(ev.samplePosition));

        const juce::MemoryBlock rawMidiBlock(ev.message.getRawData(), static_cast<size_t>(rawSize));
        evXml->setAttribute("midiBase64", rawMidiBlock.toBase64Encoding());
    }

    // 2. lockedBlocks_（各ブロックのLock状態および固定ボイシング）のシリアライズ
    auto* lockedXml = engineXml->createNewChildElement("LockedBlocks");
    for (const auto& block : lockedCopy)
    {
        auto* blockXml = lockedXml->createNewChildElement("LockedBlock");
        blockXml->setAttribute("rootPc", block.rootPitchClass);
        blockXml->setAttribute("quality", static_cast<int>(block.quality));
        blockXml->setAttribute("startPpq", block.startPpq);
        blockXml->setAttribute("durationPpq", block.durationPpq);
        blockXml->setAttribute("baseVelocity", block.baseVelocity);
        blockXml->setAttribute("hasTargetTop", block.targetTopNote.has_value());
        blockXml->setAttribute("targetTop", block.targetTopNote.value_or(-1));
        blockXml->setAttribute("isPassing", block.isPassingChord);

        juce::StringArray noteTokens;
        for (int noteNum : block.lockedVoicing.midiNotes)
        {
            noteTokens.add(juce::String(noteNum));
        }
        blockXml->setAttribute("notes", noteTokens.joinIntoString(","));
    }

    return engineXml;
}

void MidiReharmonizerEngine::restoreEngineStateFromXml(const juce::XmlElement& engineXml)
{
    if (!engineXml.hasTagName("EngineState"))
        return;

    std::vector<TimedMidiEvent> restoredCaptured;
    const auto* capturedXml = engineXml.getChildByName("CapturedSequence");
    if (capturedXml != nullptr)
    {
        for (const auto* evXml : capturedXml->getChildWithTagNameIterator("Event"))
        {
            const juce::String base64Midi = evXml->getStringAttribute("midiBase64");
            if (base64Midi.isEmpty())
                continue;

            juce::MemoryBlock rawMidiBlock;
            if (!rawMidiBlock.fromBase64Encoding(base64Midi) || rawMidiBlock.getSize() == 0)
                continue;

            TimedMidiEvent ev;
            ev.ppqPosition    = std::max(0.0, evXml->getDoubleAttribute("ppq", 0.0));
            ev.samplePosition = evXml->getStringAttribute("sample", "0").getLargeIntValue();
            ev.message        = juce::MidiMessage(rawMidiBlock.getData(), static_cast<int>(rawMidiBlock.getSize()), ev.ppqPosition);
            restoredCaptured.push_back(std::move(ev));
        }
    }

    std::vector<ChordBlock> restoredLocked;
    const auto* lockedXml = engineXml.getChildByName("LockedBlocks");
    if (lockedXml != nullptr)
    {
        for (const auto* blockXml : lockedXml->getChildWithTagNameIterator("LockedBlock"))
        {
            ChordBlock block;
            block.rootPitchClass = std::clamp(blockXml->getIntAttribute("rootPc", 0), 0, 11);

            const int qualityInt = std::clamp(blockXml->getIntAttribute("quality", 0), 0, 8);
            block.quality        = static_cast<ChordQuality>(qualityInt);
            block.startPpq       = std::max(0.0, blockXml->getDoubleAttribute("startPpq", 0.0));
            block.durationPpq    = std::max(0.05, blockXml->getDoubleAttribute("durationPpq", 1.0));
            block.baseVelocity   = std::clamp(blockXml->getIntAttribute("baseVelocity", 90), 1, 127);

            if (blockXml->getBoolAttribute("hasTargetTop", false))
            {
                const int top = blockXml->getIntAttribute("targetTop", -1);
                if (top >= 0 && top <= 127)
                    block.targetTopNote = top;
            }

            block.isPassingChord = blockXml->getBoolAttribute("isPassing", false);
            block.isLocked       = true;

            juce::StringArray noteTokens;
            noteTokens.addTokens(blockXml->getStringAttribute("notes"), ",", "");
            noteTokens.removeEmptyStrings(true);
            for (const auto& token : noteTokens)
            {
                block.lockedVoicing.midiNotes.push_back(std::clamp(token.getIntValue(), 0, 127));
            }

            if (!block.lockedVoicing.isEmpty())
            {
                restoredLocked.push_back(std::move(block));
            }
        }

        std::sort(restoredLocked.begin(), restoredLocked.end(),
            [](const ChordBlock& a, const ChordBlock& b)
            {
                return a.startPpq < b.startPpq;
            });
    }

    {
        const juce::SpinLock::ScopedLockType lock(sequenceLock_);
        if (capturedXml != nullptr)
            capturedSequence_ = std::move(restoredCaptured);
        if (lockedXml != nullptr)
            lockedBlocks_ = std::move(restoredLocked);
    }
}

void MidiReharmonizerEngine::setMelodyLockEnabled(bool enabled) noexcept
{
    melodyLock_.store(enabled, std::memory_order_release);
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    activeProfile_.melodyLock = enabled;
}

bool MidiReharmonizerEngine::isMelodyLockEnabled() const noexcept
{
    return melodyLock_.load(std::memory_order_acquire);
}

void MidiReharmonizerEngine::setForceDiatonicEnabled(bool enabled) noexcept
{
    forceDiatonic_.store(enabled, std::memory_order_release);
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    activeProfile_.forceDiatonic = enabled;
}

bool MidiReharmonizerEngine::isForceDiatonicEnabled() const noexcept
{
    return forceDiatonic_.load(std::memory_order_acquire);
}

void MidiReharmonizerEngine::setSongKeyPitchClass(int keyPitchClass) noexcept
{
    const int normalized = normalizePitchClass(keyPitchClass);
    songKeyPitchClass_.store(normalized, std::memory_order_release);
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    activeProfile_.songKeyPitchClass = normalized;
}

int MidiReharmonizerEngine::getSongKeyPitchClass() const noexcept
{
    return songKeyPitchClass_.load(std::memory_order_acquire);
}

void MidiReharmonizerEngine::setScaleType(ScaleType type) noexcept
{
    const int clamped = std::clamp(static_cast<int>(type), 0, 6);
    scaleType_.store(clamped, std::memory_order_release);
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    activeProfile_.scaleType = static_cast<ScaleType>(clamped);
}

ScaleType MidiReharmonizerEngine::getScaleType() const noexcept
{
    return static_cast<ScaleType>(std::clamp(scaleType_.load(std::memory_order_acquire), 0, 6));
}

void MidiReharmonizerEngine::setPedalBassModeEnabled(bool enabled) noexcept
{
    pedalBassMode_.store(enabled, std::memory_order_release);
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    activeProfile_.pedalBassMode = enabled;
}

bool MidiReharmonizerEngine::isPedalBassModeEnabled() const noexcept
{
    return pedalBassMode_.load(std::memory_order_acquire);
}

void MidiReharmonizerEngine::setCompingStyle(CompingStyle style) noexcept
{
    const int clamped = std::clamp(static_cast<int>(style), 0, 2);
    compingStyle_.store(clamped, std::memory_order_release);
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    activeProfile_.compingStyle = static_cast<CompingStyle>(clamped);
}

CompingStyle MidiReharmonizerEngine::getCompingStyle() const noexcept
{
    return static_cast<CompingStyle>(std::clamp(compingStyle_.load(std::memory_order_acquire), 0, 2));
}

void MidiReharmonizerEngine::setSplitMidiChannelsEnabled(bool enabled) noexcept
{
    splitMidiChannels_.store(enabled, std::memory_order_release);
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    activeProfile_.splitMidiChannels = enabled;
}

bool MidiReharmonizerEngine::isSplitMidiChannelsEnabled() const noexcept
{
    return splitMidiChannels_.load(std::memory_order_acquire);
}

void MidiReharmonizerEngine::setGrooveSyncEnabled(bool enabled) noexcept
{
    grooveSyncEnabled_.store(enabled, std::memory_order_release);
}

bool MidiReharmonizerEngine::isGrooveSyncEnabled() const noexcept
{
    return grooveSyncEnabled_.load(std::memory_order_acquire);
}

void MidiReharmonizerEngine::recordTransientPpq(double ppqPosition) noexcept
{
    if (ppqPosition < 0.0)
        return;

    const juce::SpinLock::ScopedTryLockType tryLock(sequenceLock_);
    if (!tryLock.isLocked())
        return;

    // 4小節ループ（16.0 PPQ）周期の相対位置としても扱えるように正規化または生PPQを記録
    const double wrappedPpq = std::fmod(ppqPosition, 16.0);

    if (!hasLiveAudioTransients_)
    {
        grooveMapPpq_.clear();
        grooveWriteIndex_ = 0;
        hasLiveAudioTransients_ = true;
    }

    if (grooveMapPpq_.size() < kMaxGrooveMapSize)
    {
        grooveMapPpq_.push_back(wrappedPpq);
    }
    else
    {
        grooveMapPpq_[grooveWriteIndex_] = wrappedPpq;
        grooveWriteIndex_ = (grooveWriteIndex_ + 1) % kMaxGrooveMapSize;
    }
}

void MidiReharmonizerEngine::setGrooveMap(std::span<const double> groovePpqPositions)
{
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    grooveMapPpq_.assign(groovePpqPositions.begin(), groovePpqPositions.end());
    grooveWriteIndex_ = 0;
    hasLiveAudioTransients_ = !grooveMapPpq_.empty();
}

void MidiReharmonizerEngine::clearGrooveMap()
{
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    grooveMapPpq_.clear();
    grooveWriteIndex_ = 0;
    hasLiveAudioTransients_ = false;
}

void MidiReharmonizerEngine::applyGrooveMapToSequence(
    juce::MidiMessageSequence& sequence,
    std::span<const double> grooveMapPpq,
    double ticksPerQuarterNote) const
{
    if (grooveMapPpq.empty() || sequence.getNumEvents() == 0)
        return;

    const double safeTpqn = ticksPerQuarterNote > 0.0 ? ticksPerQuarterNote : 960.0;
    sequence.updateMatchedPairs();

    // 各 NoteOn イベントを最も近いトランジェント（GrooveMap）のタイミングへ吸着（シフト）
    for (int i = 0; i < sequence.getNumEvents(); ++i)
    {
        auto* holder = sequence.getEventPointer(i);
        if (holder == nullptr || !holder->message.isNoteOn())
            continue;

        const double originalStartPpq = holder->message.getTimeStamp() / safeTpqn;
        const double originalLoopPpq  = std::fmod(originalStartPpq, 16.0);
        const double loopBasePpq      = originalStartPpq - originalLoopPpq;

        double bestTransientLoopPpq = originalLoopPpq;
        double minDiffPpq = std::numeric_limits<double>::max();

        for (double transientPpq : grooveMapPpq)
        {
            const double tLoop = std::fmod(std::max(0.0, transientPpq), 16.0);
            const double diff = std::abs(tLoop - originalLoopPpq);
            if (diff < minDiffPpq)
            {
                minDiffPpq = diff;
                bestTransientLoopPpq = tLoop;
            }
        }

        // 吸着許容ウィンドウ（最大 1.0 拍 = 四分音符以内のトランジェントへ吸着）
        if (minDiffPpq <= 1.0)
        {
            const double snappedStartTick = std::max(0.0, (loopBasePpq + bestTransientLoopPpq) * safeTpqn);
            const double deltaTicks = snappedStartTick - holder->message.getTimeStamp();

            holder->message.setTimeStamp(snappedStartTick);

            // ペアになっている NoteOff が逆転しないよう保護
            if (holder->noteOffObject != nullptr)
            {
                const double oldEndTick = holder->noteOffObject->message.getTimeStamp();
                holder->noteOffObject->message.setTimeStamp(std::max(snappedStartTick + 60.0, oldEndTick + deltaTicks * 0.25));
            }
        }
    }

    sequence.sort();
    sequence.updateMatchedPairs();
}

void MidiReharmonizerEngine::updateRulesFromPresets(int genreIndex, int artistIndex, int environmentIndex)
{
    GenreRules genre;
    switch (genreIndex)
    {
        case 0: // Jazz
            genre.allowTritoneSub = true;
            genre.allowTensions = true;
            genre.preferredStyle = VoicingStyle::Drop2;
            genre.omitRootPenalty = 4.0f; // ルートレス許容
            genre.guideToneMissingPenalty = 45.0f;
            genre.minorSecondPenalty = 40.0f;
            break;
        case 1: // Neo-Soul
            genre.allowTritoneSub = true;
            genre.allowTensions = true;
            genre.preferredStyle = VoicingStyle::Close;
            genre.omitRootPenalty = 8.0f;
            genre.guideToneMissingPenalty = 30.0f;
            genre.minorSecondPenalty = 18.0f; // クラスター（半音ぶつかり）を美学として許容
            break;
        case 2: // City Pop
            genre.allowTritoneSub = false;
            genre.allowTensions = true;
            genre.preferredStyle = VoicingStyle::Drop2;
            genre.omitRootPenalty = 15.0f;
            genre.guideToneMissingPenalty = 40.0f;
            genre.minorSecondPenalty = 45.0f;
            break;
        case 3: // Cinematic
            genre.allowTritoneSub = false;
            genre.allowTensions = true;
            genre.preferredStyle = VoicingStyle::Spread;
            genre.omitRootPenalty = 25.0f;
            genre.guideToneMissingPenalty = 25.0f;
            genre.minorSecondPenalty = 55.0f;
            break;
        case 4: // Lo-Fi (Warm close/drop2 voicings with gentle tensions)
            genre.allowTritoneSub = true;
            genre.allowTensions = true;
            genre.preferredStyle = VoicingStyle::Close;
            genre.omitRootPenalty = 6.0f;
            genre.guideToneMissingPenalty = 35.0f;
            genre.minorSecondPenalty = 22.0f;
            break;
        case 5: // Modern R&B (Lush spread/drop2 9th/11th voicings)
            genre.allowTritoneSub = true;
            genre.allowTensions = true;
            genre.preferredStyle = VoicingStyle::Drop2;
            genre.omitRootPenalty = 10.0f;
            genre.guideToneMissingPenalty = 38.0f;
            genre.minorSecondPenalty = 26.0f;
            break;
        case 6: // Gospel (Rich chromatic passing & altered tensions)
            genre.allowTritoneSub = true;
            genre.allowTensions = true;
            genre.preferredStyle = VoicingStyle::Spread;
            genre.omitRootPenalty = 12.0f;
            genre.guideToneMissingPenalty = 42.0f;
            genre.minorSecondPenalty = 20.0f;
            break;
        case 7: // 8-Bit (Chiptune: Clean 3-4 note triadic/7th close voicings)
        default:
            genre.allowTritoneSub = false;
            genre.allowTensions = false;
            genre.preferredStyle = VoicingStyle::Close;
            genre.omitRootPenalty = 30.0f;
            genre.guideToneMissingPenalty = 35.0f;
            genre.minorSecondPenalty = 65.0f;
            break;
    }

    ArtistRules artist;
    switch (artistIndex)
    {
        case 0: // 1. Rootless Master (ex. Bill E.): omitRootPenalty無効化、minorSecondPenalty低減、密集和音とレイドバックを好む
            artist.forceRootless = true;
            artist.minorSecondPenaltyScale = 0.35f;
            artist.preferredStyleOverride = VoicingStyle::Close;
            artist.spreadTargetSemitones = 11.0f;
            artist.voiceLeadingWeight = 3.6f;
            artist.topNoteLeapWeight = 3.8f;
            artist.bassNoteLeapWeight = 1.8f;
            artist.timingJitterMs = 8.5f;
            artist.strumSpreadMs = 14.0f;
            artist.velocityHumanizeRange = 12.0f;
            artist.topVoiceVelocityBoost = 11.0f;
            artist.baseVelocityOffset = -4.0f;
            break;

        case 1: // 2. Quartal Power (ex. McCoy T.): 隣接声部が完全4度（5半音）の場合にコストを下げる評価、ベロシティ強め
            artist.quartalIntervalBonus = 18.0f;
            artist.spreadTargetSemitones = 15.0f;
            artist.voiceLeadingWeight = 2.2f;
            artist.topNoteLeapWeight = 2.6f;
            artist.bassNoteLeapWeight = 2.0f;
            artist.timingJitterMs = 3.0f;
            artist.strumSpreadMs = 3.0f;
            artist.velocityHumanizeRange = 6.0f;
            artist.topVoiceVelocityBoost = 16.0f;
            artist.baseVelocityOffset = 14.0f;
            break;

        case 2: // 3. Melody & Bass (ex. Joe P.): VoicingStyle::Drop2を強く選好、ベース音とトップ音のVoice Leading重みを高く設定
            artist.preferredStyleOverride = VoicingStyle::Drop2;
            artist.styleMismatchPenaltyScale = 2.8f;
            artist.spreadTargetSemitones = 16.0f;
            artist.voiceLeadingWeight = 3.8f;
            artist.topNoteLeapWeight = 5.5f;
            artist.bassNoteLeapWeight = 4.5f;
            artist.timingJitterMs = 4.5f;
            artist.strumSpreadMs = 12.0f;
            artist.velocityHumanizeRange = 9.0f;
            artist.topVoiceVelocityBoost = 13.0f;
            artist.baseVelocityOffset = 0.0f;
            break;

        case 3: // 4. Standard (Default): 既存の標準的なバランス
        default:
            artist.spreadTargetSemitones = 16.0f;
            artist.voiceLeadingWeight = 2.5f;
            artist.topNoteLeapWeight = 3.2f;
            artist.bassNoteLeapWeight = 1.2f;
            artist.timingJitterMs = 4.0f;
            artist.strumSpreadMs = 7.0f;
            artist.velocityHumanizeRange = 8.0f;
            artist.topVoiceVelocityBoost = 10.0f;
            artist.baseVelocityOffset = 0.0f;
            break;
    }

    EnvironmentRules env;
    switch (environmentIndex)
    {
        case 0: // Rainy (Mid-High focus, delicate 4 voices)
            env.minPitch = 48;
            env.maxPitch = 84;
            env.minVoices = 4;
            env.maxVoices = 4;
            env.lowIntervalLimitPitch = 55;
            break;
        case 1: // VHS (Warm mid-range, compact 3-4 voices)
            env.minPitch = 45;
            env.maxPitch = 76;
            env.minVoices = 3;
            env.maxVoices = 4;
            env.lowIntervalLimitPitch = 53;
            break;
        case 2: // Deep Space (Wide range, lush 4-5 voices)
        default:
            env.minPitch = 38;
            env.maxPitch = 86;
            env.minVoices = 4;
            env.maxVoices = 5;
            env.maxBassToTenorInterval = 22;
            env.maxTotalSpan = 30;
            env.lowIntervalLimitPitch = 50;
            break;
    }

    updateRules(genre, artist, env);
}

juce::MidiMessageSequence MidiReharmonizerEngine::getReharmonizedMidiSequence(double ticksPerQuarterNote) const
{
    juce::MidiMessageSequence seq;

    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    for (const auto& ev : reharmonizedSequence_)
    {
        juce::MidiMessage msg(ev.message);
        msg.setTimeStamp(ev.ppqPosition * ticksPerQuarterNote);
        seq.addEvent(msg);
    }

    seq.updateMatchedPairs();
    return seq;
}

juce::MidiMessageSequence MidiReharmonizerEngine::getCapturedMidiSequence(double ticksPerQuarterNote) const
{
    juce::MidiMessageSequence seq;

    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    for (const auto& ev : capturedSequence_)
    {
        if (!ev.message.isNoteOnOrOff())
            continue;

        juce::MidiMessage msg(ev.message);
        msg.setTimeStamp(ev.ppqPosition * ticksPerQuarterNote);
        seq.addEvent(msg);
    }

    seq.updateMatchedPairs();
    return seq;
}

juce::String MidiReharmonizerEngine::analyzeChordName(const std::vector<int>& pitches)
{
    return ChordAnalyzer::analyzeChordName(pitches);
}

VoicingProfile MidiReharmonizerEngine::getCurrentProfile() const noexcept
{
    const juce::SpinLock::ScopedLockType lock(sequenceLock_);
    return activeProfile_;
}

void MidiReharmonizerEngine::processMidiBlock(
    juce::MidiBuffer& midiMessages,
    const juce::Optional<juce::AudioPlayHead::PositionInfo>& posInfo,
    int numSamples)
{
    const auto currentMode = getOperatingMode();

    switch (currentMode)
    {
        case OperatingMode::Bypass:
            break;

        case OperatingMode::Capture:
            captureIncomingMidi(midiMessages, posInfo);
            break;

        case OperatingMode::PlaybackReharm:
            injectReharmonizedMidi(midiMessages, posInfo, numSamples);
            break;
    }
}

void MidiReharmonizerEngine::extractAuditionMidiForBlock(
    juce::MidiBuffer& outAuditionMidi,
    double loopStartPpq,
    double blockDurationPpq,
    double ppqPerSample,
    int numSamples) const noexcept
{
    const juce::SpinLock::ScopedTryLockType tryLock(sequenceLock_);
    if (!tryLock.isLocked() || reharmonizedSequence_.empty() || ppqPerSample <= 0.0)
        return;

    const double kLoopLengthPpq = (reharmonizedSequence_.back().ppqPosition > 16.5) ? 32.0 : 16.0;
    const double wrappedStart = std::fmod(std::max(0.0, loopStartPpq), kLoopLengthPpq);
    const double loopEndPpq   = wrappedStart + blockDurationPpq;

    auto emitRange = [&](double rangeStartPpq, double rangeEndPpq, double basePpqOffset)
    {
        auto it = std::lower_bound(
            reharmonizedSequence_.begin(),
            reharmonizedSequence_.end(),
            rangeStartPpq,
            [](const TimedMidiEvent& ev, double targetPpq)
            {
                return ev.ppqPosition < targetPpq;
            }
        );

        for (; it != reharmonizedSequence_.end() && it->ppqPosition < rangeEndPpq; ++it)
        {
            const double offsetPpq = (it->ppqPosition - rangeStartPpq) + basePpqOffset;
            const int sampleOffset = juce::jlimit(
                0,
                std::max(0, numSamples - 1),
                static_cast<int>(std::round(offsetPpq / ppqPerSample))
            );
            outAuditionMidi.addEvent(it->message, sampleOffset);
        }
    };

    if (loopEndPpq < kLoopLengthPpq)
    {
        emitRange(wrappedStart, loopEndPpq, 0.0);
    }
    else
    {
        const double firstPartPpq = kLoopLengthPpq - wrappedStart;
        emitRange(wrappedStart, kLoopLengthPpq + 0.001, 0.0);

        const int wrapSample = juce::jlimit(
            0,
            std::max(0, numSamples - 1),
            static_cast<int>(std::round(firstPartPpq / ppqPerSample))
        );
        outAuditionMidi.addEvent(juce::MidiMessage::allNotesOff(1), wrapSample);
        outAuditionMidi.addEvent(juce::MidiMessage::allNotesOff(2), wrapSample);

        const double wrappedEndPpq = std::fmod(loopEndPpq, kLoopLengthPpq);
        emitRange(0.0, wrappedEndPpq, firstPartPpq);
    }
}

//==============================================================================
// Phase 3-1 & Phase 8: 和音候補の生成と枝刈り (Mutation 量に応じたテンション・代理コード拡張)
//==============================================================================
std::vector<Voicing> MidiReharmonizerEngine::generateCandidateVoicings(
    const ChordBlock& chord,
    const VoicingProfile& profile) const
{
    const float normMutation = std::clamp(profile.mutationAmount / 100.0f, 0.0f, 1.0f);
    // 0% 付近 (Basic: < 0.2f) では代理コード・テンションを封印し、基本和音の構成音のみに制限
    const bool isBasicSafeZone = (normMutation < 0.20f);
    // 70% 以上 (Chaos: > 0.70f) ではオルタードテンションおよびモーダルインターチェンジを解禁
    const bool isChaosZone = (normMutation > 0.70f);

    // ダイアトニックスケール拘束 (forceDiatonic) が有効な場合はスケール外の裏コード化を禁止
    const bool useTritoneSub = !profile.forceDiatonic
                               && !chord.isPassingChord
                               && !isBasicSafeZone
                               && ((profile.allowTritoneSub && chord.quality == ChordQuality::Dominant7)
                                   || (normMutation > 0.82f && (chord.quality == ChordQuality::Dominant7 || chord.quality == ChordQuality::Minor7)));

    const int effectiveRootPc = useTritoneSub
                                    ? normalizePitchClass(chord.rootPitchClass + 6)
                                    : normalizePitchClass(chord.rootPitchClass);

    const auto intervals = getChordIntervals(chord.quality);

    // v1.5 和声の進化2 & 機能I: ダイアトニックスケール拘束 (Key Awareness + Scale/Mode拡張) 用のスケールマスク構築
    std::array<bool, 12> diatonicScaleMask {};
    if (profile.forceDiatonic)
    {
        const int keyPc = normalizePitchClass(profile.songKeyPitchClass);
        const auto scaleIntervals = getScaleIntervals(profile.scaleType);
        for (int scaleIv : scaleIntervals)
        {
            diatonicScaleMask[static_cast<size_t>(normalizePitchClass(keyPc + scaleIv))] = true;
        }
    }

    // 許容されるピッチクラス集合を構築
    std::array<bool, 12> allowedPitchClasses {};
    for (int iv : intervals.coreIntervals)
        allowedPitchClasses[static_cast<size_t>(normalizePitchClass(effectiveRootPc + iv))] = true;

    if (!isBasicSafeZone && (profile.allowTensions || normMutation > 0.45f))
    {
        std::vector<int> candidateTensionIntervals = intervals.tensionIntervals;
        if (profile.forceDiatonic)
        {
            // スケール／モード拘束ON時は、Lydian(#11=6)やDorian/MelodicMinor(13th=9)等のモード特性音も
            // スケール内かつ非アボイドであればテンション候補として評価可能にする
            constexpr std::array<int, 5> kModalTensionIntervals { 2, 5, 6, 8, 9 };
            for (int modalIv : kModalTensionIntervals)
            {
                if (std::find(candidateTensionIntervals.begin(), candidateTensionIntervals.end(), modalIv) == candidateTensionIntervals.end()
                    && std::find(intervals.coreIntervals.begin(), intervals.coreIntervals.end(), modalIv) == intervals.coreIntervals.end())
                {
                    candidateTensionIntervals.push_back(modalIv);
                }
            }
        }

        for (int iv : candidateTensionIntervals)
        {
            const int tensionPc = normalizePitchClass(effectiveRootPc + iv);

            if (profile.forceDiatonic)
            {
                // 1) 指定された songKey と scaleType から生成されるスケールに属さないテンションを除外
                if (!diatonicScaleMask[static_cast<size_t>(tensionPc)])
                    continue;

                // 2) コア構成音に対して半音上（短9度）で衝突するアボイドノート（例: Maj7/Dom7上のナチュラル11th、m7上のb13等）を除外
                bool isAvoidNote = false;
                for (int coreIv : intervals.coreIntervals)
                {
                    const int corePc = normalizePitchClass(effectiveRootPc + coreIv);
                    if (normalizePitchClass(tensionPc - corePc) == 1)
                    {
                        isAvoidNote = true;
                        break;
                    }
                }
                if (isAvoidNote)
                    continue;
            }

            allowedPitchClasses[static_cast<size_t>(tensionPc)] = true;
        }
    }

    // Chaos領域（70%〜100%）では、裏コードが許可されているジャンル（allowTritoneSub == true）かつ
    // ダイアトニックスケール拘束（forceDiatonic）がOFFの場合のみオルタードテンション（b9, #9, #11, b13）を追加解禁
    if (isChaosZone && profile.allowTritoneSub && !profile.forceDiatonic)
    {
        constexpr std::array<int, 4> kAlteredAndModalIntervals { 1, 3, 6, 8 }; // b9, #9(m3), #11(b5), b13(#5)
        for (int altIv : kAlteredAndModalIntervals)
        {
            allowedPitchClasses[static_cast<size_t>(normalizePitchClass(effectiveRootPc + altIv))] = true;
        }
    }

    // v1.5 和声の進化1 & 3: Melody Lock（最高音固定）および Pedal Bass（最低音固定）の制約判定
    const bool enforceMelodyLock = profile.melodyLock && chord.targetTopNote.has_value();
    const bool enforcePedalBass  = profile.pedalBassMode && chord.forcedBassNote.has_value();

    int lockedTopNote  = enforceMelodyLock ? std::clamp(*chord.targetTopNote, 24, 120) : -1;
    int lockedBassNote = enforcePedalBass  ? std::clamp(*chord.forcedBassNote, 12, 108) : -1;

    if (enforceMelodyLock && enforcePedalBass && lockedBassNote >= lockedTopNote - 4)
    {
        while (lockedBassNote >= lockedTopNote - 6 && lockedBassNote >= 24)
        {
            lockedBassNote -= 12;
        }
    }

    const int minInnerPitch = enforcePedalBass  ? (lockedBassNote + 1) : profile.minPitch;
    const int maxInnerPitch = enforceMelodyLock ? (lockedTopNote - 1)  : profile.maxPitch;

    // 指定音域内で使用可能なMIDIノート番号を昇順で列挙
    std::vector<int> candidateNotes;
    candidateNotes.reserve(40);

    if (enforcePedalBass)
    {
        candidateNotes.push_back(lockedBassNote);
    }

    for (int pitch = minInnerPitch; pitch <= maxInnerPitch; ++pitch)
    {
        if (allowedPitchClasses[static_cast<size_t>(normalizePitchClass(pitch))])
            candidateNotes.push_back(pitch);
    }

    if (enforceMelodyLock)
    {
        if (candidateNotes.empty() || candidateNotes.back() != lockedTopNote)
            candidateNotes.push_back(lockedTopNote);
    }

    std::vector<Voicing> results;
    if (static_cast<int>(candidateNotes.size()) < profile.minVoices)
        return results;

    results.reserve(512);
    // 計算爆発を防ぐための候補数上限ガード
    constexpr size_t kMaxCandidatesPerChord = 1024;

    // Melody Lock や Pedal Bass で上下端が固定されている場合は、スパン制限やバス〜テナー間隔制限を自動拡張して候補枯渇を防ぐ
    int effectiveMaxTotalSpan = profile.maxTotalSpan;
    int effectiveMaxBassToTenor = profile.maxBassToTenorInterval;
    if (enforceMelodyLock || enforcePedalBass)
    {
        effectiveMaxTotalSpan   = std::max(effectiveMaxTotalSpan, 42);
        effectiveMaxBassToTenor = std::max(effectiveMaxBassToTenor, 26);
        if (enforceMelodyLock && enforcePedalBass)
        {
            effectiveMaxTotalSpan = std::max(effectiveMaxTotalSpan, (lockedTopNote - lockedBassNote) + 2);
        }
    }

    std::vector<int> currentNotes;
    currentNotes.reserve(static_cast<size_t>(profile.maxVoices));
    std::array<int, 12> pitchClassCount {};

    // バックトラッキングによる組み合わせ生成とハードリミット枝刈り
    auto backtrack = [&](auto& self, size_t startIndex, int targetVoiceCount) -> void
    {
        if (results.size() >= kMaxCandidatesPerChord)
            return;

        const int currentCount = static_cast<int>(currentNotes.size());
        if (currentCount == targetVoiceCount)
        {
            if (enforcePedalBass && currentNotes.front() != lockedBassNote)
                return;
            if (enforceMelodyLock && currentNotes.back() != lockedTopNote)
                return;

            // 完成したボイシングの全体スパンチェック
            const int totalSpan = currentNotes.back() - currentNotes.front();
            if (totalSpan >= 4 && totalSpan <= effectiveMaxTotalSpan)
            {
                results.push_back(Voicing { currentNotes });
            }
            return;
        }

        // 残りの候補ノート数が必要声部数に満たない場合は即座に枝刈り
        const int remainingNeeded = targetVoiceCount - currentCount;
        if (candidateNotes.size() - startIndex < static_cast<size_t>(remainingNeeded))
            return;

        for (size_t i = startIndex; i < candidateNotes.size(); ++i)
        {
            const int nextPitch = candidateNotes[i];
            const int nextPc = normalizePitchClass(nextPitch);

            // [Pedal Bass 拘束] 第1声（最低音）は必ず lockedBassNote に固定
            if (enforcePedalBass && currentCount == 0 && nextPitch != lockedBassNote)
                break;

            // [Melody Lock 拘束] 最終声（最高音）は必ず lockedTopNote に固定し、内声は lockedTopNote 未満に制限
            if (enforceMelodyLock)
            {
                if (currentCount == targetVoiceCount - 1)
                {
                    if (nextPitch != lockedTopNote)
                        continue;
                }
                else if (nextPitch >= lockedTopNote)
                {
                    break;
                }
            }

            // [枝刈り1] 同一ピッチクラス（オクターブ重複）はボイシング内で最大2回まで、
            //           かつルート・ペダルベース・ロックメロディ以外の重複は1回までに制限して不自然な重複と計算爆発を防ぐ
            const bool isFixedEndpoint = (enforcePedalBass && currentCount == 0)
                                         || (enforceMelodyLock && currentCount == targetVoiceCount - 1);
            const bool isRoot = (nextPc == effectiveRootPc) || isFixedEndpoint;
            const int maxPcDuplicates = isRoot ? 2 : 1;
            if (pitchClassCount[static_cast<size_t>(nextPc)] >= maxPcDuplicates)
                continue;

            if (!currentNotes.empty())
            {
                const int prevPitch = currentNotes.back();
                const int adjacentInterval = nextPitch - prevPitch;
                const int totalSpanSoFar = nextPitch - currentNotes.front();

                // [枝刈り2] 全体スパンが上限を超えたら、これ以降のノート（さらに高音）も全て超えるためbreakで枝刈り
                if (totalSpanSoFar > effectiveMaxTotalSpan)
                    break;

                // [枝刈り3] 隣接声部間のインターバルが広すぎる配置をハードリミットで除外
                //           最低音（Bass）と第2音の間は effectiveMaxBassToTenor、それより上は maxAdjacentInterval を適用
                const int maxAllowedGap = (currentCount == 1)
                                              ? effectiveMaxBassToTenor
                                              : ((enforceMelodyLock && currentCount == targetVoiceCount - 1)
                                                     ? std::max(profile.maxAdjacentInterval, 19)
                                                     : profile.maxAdjacentInterval);
                if (adjacentInterval > maxAllowedGap)
                    break;

                // [枝刈り4] 極端な低音域での半音・全音クラスターをハードリミットで除外
                if (prevPitch < profile.lowIntervalLimitPitch && adjacentInterval < 3)
                    continue;
            }

            currentNotes.push_back(nextPitch);
            pitchClassCount[static_cast<size_t>(nextPc)]++;

            self(self, i + 1, targetVoiceCount);

            pitchClassCount[static_cast<size_t>(nextPc)]--;
            currentNotes.pop_back();

            if (results.size() >= kMaxCandidatesPerChord)
                return;
        }
    };

    // ハードリミット: 構成音が 3〜5音 (profile.minVoices 〜 profile.maxVoices) に収まる候補のみ生成
    const int minV = std::clamp(profile.minVoices, 3, 5);
    const int maxV = std::clamp(profile.maxVoices, minV, 5);

    for (int voiceCount = minV; voiceCount <= maxV; ++voiceCount)
    {
        backtrack(backtrack, 0, voiceCount);
    }

    return results;
}

//==============================================================================
// Phase 3-2 & Phase 8: コスト関数 (Mutation スライダーによる静的・動的コスト変調)
//==============================================================================
float MidiReharmonizerEngine::calculateStaticCost(
    const Voicing& currentVoicing,
    const ChordBlock& chord,
    const VoicingProfile& profile) const noexcept
{
    if (currentVoicing.isEmpty())
        return 1.0e6f;

    float cost = 0.0f;
    const auto& notes = currentVoicing.midiNotes;
    const size_t numVoices = notes.size();

    // Mutation 正規化量: 0.0 (Basic) <-> 0.5 (Advanced: 標準 1.0x) <-> 1.0 (Chaos)
    const float normMutation = std::clamp(profile.mutationAmount / 100.0f, 0.0f, 1.0f);
    // 0% -> 2.5x ペナルティ強化 / 50% -> 1.0x 標準 / 100% -> 0.02x ペナルティほぼ無効化
    const float safetyPenaltyScale = (normMutation <= 0.5f)
                                         ? (2.5f - (normMutation * 3.0f))
                                         : std::max(0.02f, 1.0f - ((normMutation - 0.5f) * 1.96f));
    const float chaosFactor = std::clamp((normMutation - 0.5f) * 2.0f, 0.0f, 1.0f);
    const float basicStrictness = std::clamp((0.5f - normMutation) * 2.0f, 0.0f, 1.0f);

    const bool useTritoneSub = !profile.forceDiatonic
                               && !chord.isPassingChord
                               && (normMutation >= 0.20f)
                               && ((profile.allowTritoneSub && chord.quality == ChordQuality::Dominant7)
                                   || (normMutation > 0.82f && (chord.quality == ChordQuality::Dominant7 || chord.quality == ChordQuality::Minor7)));

    const int effectiveRootPc = useTritoneSub
                                    ? normalizePitchClass(chord.rootPitchClass + 6)
                                    : normalizePitchClass(chord.rootPitchClass);
    const auto chordInfo = getChordIntervals(chord.quality);

    // 1. 隣接声部間のインターバル検査（半音ぶつかり ＆ Low Interval Limit 違反）
    for (size_t i = 1; i < numVoices; ++i)
    {
        const int lowerNote = notes[i - 1];
        const int upperNote = notes[i];
        const int interval = upperNote - lowerNote;

        // 半音のぶつかり（短2度 = 1半音）: Basic時は強烈に回避、Chaos時はペナルティを破壊（むしろクラスターを選好）
        if (interval == 1)
        {
            cost += profile.minorSecondPenalty * safetyPenaltyScale;
            if (chaosFactor > 0.4f)
                cost -= 18.0f * chaosFactor; // カオス領域では半音クラスターにボーナスを与える
        }
        else if (interval == 2 && lowerNote < 60)
        {
            cost += profile.minorSecondPenalty * 0.25f * safetyPenaltyScale;
        }

        // v1.5 機能D (Quartal Power / McCoy T.): 隣接声部が完全4度（インターバル5）の場合にコストを下げる評価
        if (interval == 5 && profile.quartalIntervalBonus > 0.0f)
        {
            cost -= profile.quartalIntervalBonus;
        }

        // Low Interval Limit 違反ペナルティ
        if (lowerNote < profile.lowIntervalLimitPitch && interval < profile.lowIntervalMinSemitones)
        {
            const float pitchDepth = static_cast<float>(profile.lowIntervalLimitPitch - lowerNote);
            const float intervalDeficit = static_cast<float>(profile.lowIntervalMinSemitones - interval);
            cost += pitchDepth * intervalDeficit * profile.lowIntervalPenaltyWeight * 0.25f * safetyPenaltyScale;
        }
    }

    // 非隣接声部間の短9度（13半音）の濁りペナルティ
    for (size_t i = 0; i < numVoices; ++i)
    {
        for (size_t j = i + 1; j < numVoices; ++j)
        {
            if (notes[j] - notes[i] == 13)
                cost += profile.minorSecondPenalty * 0.6f * safetyPenaltyScale;
        }
    }

    // 2. ルート音・ガイドトーン・テンション／オルタード構成音の評価
    bool hasRoot = false;
    bool hasThird = (chordInfo.thirdInterval < 0);
    bool hasSeventh = (chordInfo.seventhInterval < 0);
    int tensionOrAlteredCount = 0;

    const int thirdPc = chordInfo.thirdInterval >= 0
                            ? normalizePitchClass(effectiveRootPc + chordInfo.thirdInterval)
                            : -1;
    const int seventhPc = chordInfo.seventhInterval >= 0
                              ? normalizePitchClass(effectiveRootPc + chordInfo.seventhInterval)
                              : -1;
    const int fifthPc = normalizePitchClass(effectiveRootPc + 7);

    for (int n : notes)
    {
        const int pc = normalizePitchClass(n);
        if (pc == effectiveRootPc)
            hasRoot = true;
        else if (pc == thirdPc)
            hasThird = true;
        else if (pc == seventhPc)
            hasSeventh = true;
        else if (pc != fifthPc)
            ++tensionOrAlteredCount;
    }

    if (!hasRoot)
    {
        // ペダルポイント・モード（分数コード状態）では最低音がペダル音で固定されるためルート省略ペナルティを免除
        if (!profile.pedalBassMode)
            cost += profile.omitRootPenalty * safetyPenaltyScale;
    }
    else if (profile.forceRootless)
    {
        // v1.5 機能D (Rootless Master / Bill E.): ルート音を省略した密集テンション配置を積極的に選好
        cost += 14.0f * safetyPenaltyScale;
    }
    if (!hasThird)
        cost += profile.guideToneMissingPenalty * safetyPenaltyScale;
    if (!hasSeventh)
        cost += profile.guideToneMissingPenalty * 0.8f * safetyPenaltyScale;

    // Basic (0%付近): テンションノートに対して強いペナルティを課し、基本コードの安全な転回形・Drop2を選ばせる
    if (basicStrictness > 0.0f)
    {
        cost += static_cast<float>(tensionOrAlteredCount) * 32.0f * basicStrictness;
    }
    // Chaos (50%〜100%): テンションやオルタード・モーダル音を含むボイシングにボーナス（負のコスト）を付与
    else if (chaosFactor > 0.0f)
    {
        cost -= static_cast<float>(tensionOrAlteredCount) * 16.0f * chaosFactor;
    }

    // 3. ボイシング全体の広がり（Spread Target）からの乖離コスト
    const float currentSpread = static_cast<float>(currentVoicing.getSpread());
    cost += std::abs(currentSpread - profile.spreadTargetSemitones) * profile.spreadDeviationWeight * safetyPenaltyScale;

    // 4. ボイシングスタイル（Drop2など）との整合性評価
    if (profile.preferredStyle == VoicingStyle::Drop2)
    {
        if (!currentVoicing.isDrop2Like())
            cost += profile.styleMismatchPenalty * (1.0f + basicStrictness * 1.5f) * (1.0f - chaosFactor * 0.9f);
    }
    else if (profile.preferredStyle == VoicingStyle::Close)
    {
        if (currentSpread > 12.0f)
            cost += profile.styleMismatchPenalty * safetyPenaltyScale;
    }

    // 5. ターゲット・トップノート（元のメロディ音）が指定されている場合の追従コスト ＆ Melody Lock 絶対維持チェック
    if (chord.targetTopNote.has_value())
    {
        const int topDiff = std::abs(currentVoicing.getTopNote() - *chord.targetTopNote);
        if (profile.melodyLock && topDiff != 0)
        {
            cost += 1.0e5f + static_cast<float>(topDiff) * 1000.0f;
        }
        else
        {
            cost += static_cast<float>(topDiff) * 1.5f * (1.0f - chaosFactor * 0.85f);
        }
    }

    // 6. ペダルポイント・モード (Pedal Bass) 有効時の最低音一致チェック
    if (profile.pedalBassMode && chord.forcedBassNote.has_value())
    {
        const int bassDiff = std::abs(currentVoicing.getBassNote() - *chord.forcedBassNote);
        if (bassDiff != 0)
        {
            cost += 1.0e5f + static_cast<float>(bassDiff) * 1000.0f;
        }
    }

    return cost;
}

float MidiReharmonizerEngine::calculateTransitionCost(
    const Voicing& prevVoicing,
    const Voicing& currentVoicing,
    const VoicingProfile& profile) const noexcept
{
    if (prevVoicing.isEmpty() || currentVoicing.isEmpty())
        return 0.0f;

    const auto& prevNotes = prevVoicing.midiNotes;
    const auto& currNotes = currentVoicing.midiNotes;

    float voiceLeadingDistance = 0.0f;

    // 声部数が同一の場合は1対1で声部間の移動距離を計算
    if (prevNotes.size() == currNotes.size())
    {
        for (size_t i = 0; i < currNotes.size(); ++i)
        {
            voiceLeadingDistance += static_cast<float>(std::abs(currNotes[i] - prevNotes[i]));
        }
    }
    else
    {
        // 声部数が異なる場合は各声部から最近傍の声部への移動距離＋声部数変化ペナルティを計算
        for (int cNote : currNotes)
        {
            int minDist = 127;
            for (int pNote : prevNotes)
                minDist = std::min(minDist, std::abs(cNote - pNote));
            voiceLeadingDistance += static_cast<float>(minDist);
        }

        const float voiceCountDiff = static_cast<float>(
            std::abs(static_cast<int>(currNotes.size()) - static_cast<int>(prevNotes.size()))
        );
        voiceLeadingDistance += voiceCountDiff * 3.0f;
    }

    // Mutation 量によるボイスリーディング重みのスケーリング:
    // 0% (Basic): 1.8x (最小移動を厳格に維持)
    // 50% (Advanced): 1.0x (標準)
    // 100% (Chaos): -0.35x (移動距離ペナルティを無効化し、むしろ大胆な跳躍・配置転換を誘発)
    const float normMutation = std::clamp(profile.mutationAmount / 100.0f, 0.0f, 1.0f);
    const float vlScale = (normMutation <= 0.5f)
                              ? (1.8f - normMutation * 1.6f)
                              : (1.0f - (normMutation - 0.5f) * 2.7f);

    const float normalizedMovement = voiceLeadingDistance / static_cast<float>(currNotes.size());
    float transitionCost = normalizedMovement * profile.voiceLeadingWeight * vlScale;

    // トップノート（最高音）およびベースノート（最低音）の跳躍ペナルティも Mutation ＆ 達人スタイルに応じてスケール
    const float topNoteLeap = static_cast<float>(std::abs(currentVoicing.getTopNote() - prevVoicing.getTopNote()));
    transitionCost += topNoteLeap * profile.topNoteLeapWeight * vlScale;

    const float bassNoteLeap = static_cast<float>(std::abs(currentVoicing.getBassNote() - prevVoicing.getBassNote()));
    transitionCost += bassNoteLeap * profile.bassNoteLeapWeight * vlScale;

    return transitionCost;
}

float MidiReharmonizerEngine::evaluateVoicingCost(
    const Voicing* prevVoicing,
    const Voicing& currentVoicing,
    const ChordBlock& chord,
    const VoicingProfile& profile) const noexcept
{
    float totalCost = calculateStaticCost(currentVoicing, chord, profile);
    if (prevVoicing != nullptr)
    {
        totalCost += calculateTransitionCost(*prevVoicing, currentVoicing, profile);
    }
    return totalCost;
}

//==============================================================================
// Phase 3-3: 経路探索 (Viterbi Algorithm / 動的計画法)
//==============================================================================
std::vector<Voicing> MidiReharmonizerEngine::findOptimalVoicingPath(
    std::span<const ChordBlock> progression,
    const VoicingProfile& profile) const
{
    std::vector<Voicing> optimalPath;
    if (progression.empty())
        return optimalPath;

    // トレリス（格子）上の1ノード構造体
    struct TrellisNode
    {
        Voicing voicing;
        float minCumulativeCost { std::numeric_limits<float>::infinity() };
        int backpointer { -1 }; ///< 1つ前の時間ステップにおける最適遷移元インデックス
    };

    const size_t numSteps = progression.size();
    std::vector<std::vector<TrellisNode>> trellis(numSteps);

    // v1.5 和声の進化3: ペダルポイント・モード (Pedal Bass) の基準最低音を算出
    // 進行の最初のコードのベース音（またはダイアトニック拘束ON時は楽曲キーの主音 I度）を全ステップの最低音として固定
    std::optional<int> pedalBassMidiNote;
    if (profile.pedalBassMode)
    {
        if (progression.front().forcedBassNote.has_value())
        {
            pedalBassMidiNote = *progression.front().forcedBassNote;
        }
        else if (!profile.forceDiatonic
                 && progression.front().originalBassNote.has_value()
                 && *progression.front().originalBassNote >= profile.minPitch - 5
                 && *progression.front().originalBassNote <= profile.minPitch + 14)
        {
            pedalBassMidiNote = *progression.front().originalBassNote;
        }
        else
        {
            const int pedalPc = profile.forceDiatonic
                                    ? normalizePitchClass(profile.songKeyPitchClass)
                                    : (progression.front().originalBassNote.has_value()
                                           ? normalizePitchClass(*progression.front().originalBassNote)
                                           : normalizePitchClass(progression.front().rootPitchClass));
            int candidateBass = profile.minPitch;
            while (normalizePitchClass(candidateBass) != pedalPc)
            {
                ++candidateBass;
            }
            pedalBassMidiNote = candidateBass;
        }
    }

    // 1. 各時間ステップ（各コードブロック）の候補生成と動的計画法による前方確率（最小累積コスト）計算
    for (size_t t = 0; t < numSteps; ++t)
    {
        ChordBlock stepChord = progression[t];
        if (pedalBassMidiNote.has_value() && !stepChord.forcedBassNote.has_value())
        {
            stepChord.forcedBassNote = pedalBassMidiNote;
        }

        std::vector<Voicing> candidates;

        // v1.5 機能B: ロック（ピン留め）されているブロックは前回のボイシング結果をそのままパススルー
        if (stepChord.isLocked && !stepChord.lockedVoicing.isEmpty())
        {
            candidates.push_back(stepChord.lockedVoicing);
        }
        else
        {
            candidates = generateCandidateVoicings(stepChord, profile);
        }

        // 万が一ハードリミットが厳しすぎて候補が0件になった場合のフォールバック（Melody Lock / Pedal Bass も維持）
        if (candidates.empty())
        {
            const int baseMidi = (profile.pedalBassMode && stepChord.forcedBassNote.has_value())
                                     ? std::clamp(*stepChord.forcedBassNote, 12, 96)
                                     : std::clamp(60 + normalizePitchClass(stepChord.rootPitchClass) - 12,
                                                  profile.minPitch,
                                                  profile.maxPitch - 12);
            const int rootUpper = std::max(baseMidi + 5, 48 + normalizePitchClass(stepChord.rootPitchClass));
            const auto chordIv  = getChordIntervals(stepChord.quality);
            const int thirdIv   = chordIv.thirdInterval >= 0 ? chordIv.thirdInterval : 4;
            const int seventhIv = chordIv.seventhInterval >= 0 ? chordIv.seventhInterval : 7;

            int v1 = baseMidi;
            int v2 = rootUpper + thirdIv;
            int v3 = rootUpper + 7;
            int v4 = (profile.melodyLock && stepChord.targetTopNote.has_value())
                         ? std::clamp(*stepChord.targetTopNote, v1 + 7, 120)
                         : (rootUpper + seventhIv);

            if (v3 >= v4)
                v3 = std::max(v1 + 4, v4 - 3);
            if (v2 >= v3)
                v2 = std::max(v1 + 2, v3 - 3);

            std::vector<int> fallbackNotes { v1, v2, v3, v4 };
            std::sort(fallbackNotes.begin(), fallbackNotes.end());
            fallbackNotes.erase(std::unique(fallbackNotes.begin(), fallbackNotes.end()), fallbackNotes.end());
            candidates.push_back(Voicing { std::move(fallbackNotes) });
        }

        auto& currentLayer = trellis[t];
        currentLayer.resize(candidates.size());

        if (t == 0)
        {
            // 初期ステップ (t = 0): 静的コストのみを初期累積コストとして設定
            for (size_t j = 0; j < candidates.size(); ++j)
            {
                currentLayer[j].voicing = std::move(candidates[j]);
                currentLayer[j].minCumulativeCost = calculateStaticCost(
                    currentLayer[j].voicing, stepChord, profile);
                currentLayer[j].backpointer = -1;
            }
        }
        else
        {
            // 遷移ステップ (t >= 1): 前ステップの全ノードからの遷移コストを走査し、最小コスト経路を選択
            const auto& prevLayer = trellis[t - 1];

            for (size_t j = 0; j < candidates.size(); ++j)
            {
                currentLayer[j].voicing = std::move(candidates[j]);
                const float staticCost = calculateStaticCost(
                    currentLayer[j].voicing, stepChord, profile);

                float bestCost = std::numeric_limits<float>::infinity();
                int bestPrevIdx = 0;

                for (size_t i = 0; i < prevLayer.size(); ++i)
                {
                    const float transitionCost = calculateTransitionCost(
                        prevLayer[i].voicing, currentLayer[j].voicing, profile);
                    const float candidateTotalCost = prevLayer[i].minCumulativeCost + staticCost + transitionCost;

                    if (candidateTotalCost < bestCost)
                    {
                        bestCost = candidateTotalCost;
                        bestPrevIdx = static_cast<int>(i);
                    }
                }

                currentLayer[j].minCumulativeCost = bestCost;
                currentLayer[j].backpointer = bestPrevIdx;
            }
        }
    }

    // 2. バックトラック（最終ステップの最小コストノードから逆順にたどり、最適ボイシング列を復元）
    optimalPath.resize(numSteps);

    const auto& finalLayer = trellis.back();
    int currentBestIdx = 0;
    float minFinalCost = std::numeric_limits<float>::infinity();

    for (size_t j = 0; j < finalLayer.size(); ++j)
    {
        if (finalLayer[j].minCumulativeCost < minFinalCost)
        {
            minFinalCost = finalLayer[j].minCumulativeCost;
            currentBestIdx = static_cast<int>(j);
        }
    }

    for (int t = static_cast<int>(numSteps) - 1; t >= 0; --t)
    {
        const auto& node = trellis[static_cast<size_t>(t)][static_cast<size_t>(currentBestIdx)];
        optimalPath[static_cast<size_t>(t)] = node.voicing;
        currentBestIdx = node.backpointer;
        if (currentBestIdx < 0 && t > 0)
            currentBestIdx = 0;
    }

    return optimalPath;
}

//==============================================================================
// Phase 13: Harmonic Rhythm（和声リズム）の動的分割とパッシングコード（裏コード等）の自動挿入
//==============================================================================
ChordBlock MidiReharmonizerEngine::computeApproachChordForTarget(
    const ChordBlock& sourceBlock,
    int targetRootPitchClass,
    double startPpq,
    double durationPpq,
    bool useTritoneSubstitution,
    std::optional<int> nextTargetTopNote) noexcept
{
    ChordBlock approach = sourceBlock;
    approach.startPpq      = startPpq;
    approach.durationPpq   = std::max(0.20, durationPpq);
    approach.isPassingChord = true;

    // 裏コード（ターゲットの半音上のドミナント7th: bII7 -> I）または
    // セカンダリードミナント（ターゲットの完全5度上のドミナント7th: V7/target -> target）を算出
    const int normalizedTargetRoot = normalizePitchClass(targetRootPitchClass);
    if (useTritoneSubstitution)
    {
        approach.rootPitchClass = normalizePitchClass(normalizedTargetRoot + 1);
    }
    else
    {
        approach.rootPitchClass = normalizePitchClass(normalizedTargetRoot + 7);
    }
    approach.quality = ChordQuality::Dominant7;

    // パッシングコードのベロシティを微調整し、グルーヴの抑揚を付与
    approach.baseVelocity = std::clamp(static_cast<int>(std::round(sourceBlock.baseVelocity * 0.94f)), 40, 124);

    // 元のトップノートと次のターゲットトップノートの間を滑らかに橋渡しする目標トップノートを設定
    if (sourceBlock.targetTopNote.has_value() && nextTargetTopNote.has_value())
    {
        const int srcTop = *sourceBlock.targetTopNote;
        const int dstTop = *nextTargetTopNote;
        if (srcTop != dstTop)
        {
            approach.targetTopNote = dstTop + ((srcTop > dstTop) ? 1 : -1);
        }
    }

    return approach;
}

std::vector<ChordBlock> MidiReharmonizerEngine::subdivideAndInsertPassingChords(
    std::span<const ChordBlock> baseProgression,
    const VoicingProfile& profile) const
{
    std::vector<ChordBlock> result;
    if (baseProgression.empty())
        return result;

    const float mutation = std::clamp(profile.mutationAmount, 0.0f, 100.0f);

    // 1. 0%〜30% (Basic/Safe領域): 分割は行わず、入力された和声リズムを完全に維持
    if (mutation <= 30.0f)
    {
        result.assign(baseProgression.begin(), baseProgression.end());
        return result;
    }

    const size_t numBlocks = baseProgression.size();
    result.reserve(numBlocks * 4);

    // 2拍（ハーフノート = 2.0 PPQ）を超える長いブロックのインデックスを抽出
    constexpr double kMinSubdivideDurationPpq = 2.01;
    std::vector<size_t> longBlockIndices;
    for (size_t i = 0; i < numBlocks; ++i)
    {
        if (baseProgression[i].durationPpq > kMinSubdivideDurationPpq)
        {
            longBlockIndices.push_back(i);
        }
    }

    if (longBlockIndices.empty())
    {
        result.assign(baseProgression.begin(), baseProgression.end());
        return result;
    }

    // Mutation 量に応じてどの長いブロックを分割するかを決定
    std::vector<bool> shouldSubdivide(numBlocks, false);

    if (mutation < 70.0f)
    {
        // 30%〜70% (50%付近): 4小節に1〜2回程度の頻度（8小節時は2〜4回）で、解決前（ツーファイブ解決やターンアラウンド等）に控えめに挿入
        const size_t barsScale = (numBlocks > 5) ? 2u : 1u;
        const size_t maxSplits = ((mutation < 56.0f) ? 1u : 2u) * barsScale;

        // 音楽的な優先度スコアでソート（4度上行/5度下行の強進行解決前やフレーズ末尾を最優先）
        struct CandidateRank
        {
            size_t blockIdx;
            int priorityScore;
        };
        std::vector<CandidateRank> ranked;
        ranked.reserve(longBlockIndices.size());

        for (size_t idx : longBlockIndices)
        {
            const auto& curr = baseProgression[idx];
            const auto& next = baseProgression[(idx + 1) % numBlocks];
            const int rootIntervalToNext = normalizePitchClass(next.rootPitchClass - curr.rootPitchClass);

            int score = 0;
            // 完全4度上行（=完全5度下行: rootIntervalToNext == 5）の解決前（例: G7 -> Cmaj7 や Dm7 -> G7）を最優先
            if (rootIntervalToNext == 5)
                score += 50;
            // ドミナント7thコードブロックやマイナー7thブロックは分割適性が高い
            if (curr.quality == ChordQuality::Dominant7)
                score += 30;
            else if (curr.quality == ChordQuality::Minor7)
                score += 20;
            // 4小節フレーズの2小節目・4小節目（偶数番目インデックス = 1, 3）を優先
            if ((idx % 2) == 1)
                score += 25;
            if (idx + 1 == numBlocks)
                score += 15;

            ranked.push_back({ idx, score });
        }

        std::stable_sort(ranked.begin(), ranked.end(),
            [](const CandidateRank& a, const CandidateRank& b)
            {
                return a.priorityScore > b.priorityScore;
            });

        const size_t countToEnable = std::min(maxSplits, ranked.size());
        for (size_t k = 0; k < countToEnable; ++k)
        {
            shouldSubdivide[ranked[k].blockIdx] = true;
        }
    }
    else
    {
        // 70%〜100% (Chaos領域): 2拍を超えるすべての長い白玉コードブロックを分割
        for (size_t idx : longBlockIndices)
        {
            shouldSubdivide[idx] = true;
        }
    }

    // 各ブロックを走査して時間軸分割＆アプローチコード（裏コード / セカンダリードミナント / 裏の裏）を生成
    constexpr double kNoteGapPpq = 0.08;

    for (size_t i = 0; i < numBlocks; ++i)
    {
        const auto& current = baseProgression[i];
        if (!shouldSubdivide[i])
        {
            result.push_back(current);
            continue;
        }

        const auto& targetNext = baseProgression[(i + 1) % numBlocks];
        const int targetRootPc = normalizePitchClass(targetNext.rootPitchClass);
        const double totalSpanPpq = current.durationPpq;

        // Melody Lock が ON の場合はパッシングコード挿入時も元のブロックの最高音（targetTopNote）を絶対維持
        const std::optional<int> passingTargetTop = profile.melodyLock
                                                        ? current.targetTopNote
                                                        : targetNext.targetTopNote;

        // 元のコードの終端時刻（startPpq + durationPpq）と次のコードの開始時刻から実質グリッド幅を推定
        const double nextStartPpq = (i + 1 < numBlocks)
                                        ? baseProgression[i + 1].startPpq
                                        : (current.startPpq + std::ceil(totalSpanPpq));
        const double gridSlotPpq  = std::max(totalSpanPpq + kNoteGapPpq, nextStartPpq - current.startPpq);

        // Chaos極限領域（85%〜100%）かつ1小節（約3.4拍以上）の長い白玉コードの場合：
        // バップのように3分割〜4分割し、「裏コードのさらに裏」や連続ドミナント・チェインを挿入
        if (mutation >= 92.0f && gridSlotPpq >= 3.4 && !profile.forceDiatonic)
        {
            // 4分割（1拍ずつ: 基本コード -> アプローチIIm7b5/裏の裏の裏 -> 裏コードの裏(V7/bII7) -> 裏コード(bII7) -> ターゲット）
            const double stepPpq = gridSlotPpq * 0.25;
            const double noteDur = std::max(0.20, stepPpq - kNoteGapPpq);

            // Beat 1: 前半基本コード
            ChordBlock beat1 = current;
            beat1.durationPpq = noteDur;
            result.push_back(beat1);

            // Beat 4 (ターゲット直前): 裏コード (bII7 = targetRoot + 1)
            const int tritoneSubRoot = normalizePitchClass(targetRootPc + 1);
            // Beat 3 (裏コードへ向かうセカンダリードミナント / 裏コードの裏 = tritoneSubRoot + 7 = targetRoot + 8)
            const int subOfSubRoot   = normalizePitchClass(tritoneSubRoot + 7);
            // Beat 2 (さらにその半音上または5度上のアプローチ = subOfSubRoot + 1 = targetRoot + 9)
            const int preApproachRoot = normalizePitchClass(subOfSubRoot + 1);

            // Beat 2: Pre-Approach (Related Minor7b5 or Dominant7)
            ChordBlock beat2 = computeApproachChordForTarget(
                current, subOfSubRoot, current.startPpq + stepPpq, noteDur, true, passingTargetTop);
            beat2.rootPitchClass = preApproachRoot;
            beat2.quality = ((i % 2) == 0) ? ChordQuality::Minor7b5 : ChordQuality::Dominant7;
            if (profile.melodyLock)
                beat2.targetTopNote = current.targetTopNote;
            result.push_back(beat2);

            // Beat 3: 裏コードのさらに裏 (Dominant7 resolving to bII7)
            ChordBlock beat3 = computeApproachChordForTarget(
                current, tritoneSubRoot, current.startPpq + stepPpq * 2.0, noteDur, false, passingTargetTop);
            beat3.rootPitchClass = subOfSubRoot;
            if (profile.melodyLock)
                beat3.targetTopNote = current.targetTopNote;
            result.push_back(beat3);

            // Beat 4: ターゲットへ半音下行解決する裏コード (Tritone Substitution: bII7)
            ChordBlock beat4 = computeApproachChordForTarget(
                current, targetRootPc, current.startPpq + stepPpq * 3.0, noteDur, true, passingTargetTop);
            if (profile.melodyLock)
                beat4.targetTopNote = current.targetTopNote;
            result.push_back(beat4);
        }
        else if (mutation >= 80.0f && gridSlotPpq >= 3.4 && !profile.forceDiatonic)
        {
            // 3分割（前半2拍: 基本コード -> 3拍目: 裏コードの裏 / Related II -> 4拍目: 裏コード）
            const double halfPpq    = gridSlotPpq * 0.5;
            const double quarterPpq = gridSlotPpq * 0.25;

            // 前半（2拍）: 基本コード
            ChordBlock firstHalf = current;
            firstHalf.durationPpq = std::max(0.25, halfPpq - kNoteGapPpq);
            result.push_back(firstHalf);

            const int tritoneSubRoot = normalizePitchClass(targetRootPc + 1);

            // 後半1（3拍目）: 裏コードへ向かうアプローチ（裏コードの半音上または完全5度上）
            ChordBlock preApproach = computeApproachChordForTarget(
                current,
                tritoneSubRoot,
                current.startPpq + halfPpq,
                std::max(0.20, quarterPpq - kNoteGapPpq),
                (i % 2) == 0,
                passingTargetTop
            );
            if (profile.melodyLock)
                preApproach.targetTopNote = current.targetTopNote;
            result.push_back(preApproach);

            // 後半2（4拍目）: ターゲットへ向かう裏コード (bII7)
            ChordBlock finalApproach = computeApproachChordForTarget(
                current,
                targetRootPc,
                current.startPpq + halfPpq + quarterPpq,
                std::max(0.20, quarterPpq - kNoteGapPpq),
                true,
                passingTargetTop
            );
            if (profile.melodyLock)
                finalApproach.targetTopNote = current.targetTopNote;
            result.push_back(finalApproach);
        }
        else
        {
            // 標準の2分割（前半: 基本コード / 後半: 裏コードまたはセカンダリードミナント）
            const double halfSlotPpq = gridSlotPpq * 0.5;
            const double firstDur    = std::max(0.25, halfSlotPpq - kNoteGapPpq);
            const double secondStart = current.startPpq + halfSlotPpq;
            const double secondDur   = std::max(0.25, (current.startPpq + totalSpanPpq) - secondStart);

            // 1. 前半ブロック（基本コード）
            ChordBlock firstHalf = current;
            firstHalf.durationPpq = firstDur;
            result.push_back(firstHalf);

            // 2. 後半ブロック（アプローチ領域: 裏コード or セカンダリードミナント）
            // 元のコード自体が既にセカンダリードミナント（V7/target）である場合は、必ず裏コード（bII7）へ変化させて動きを作る
            // ただし forceDiatonic が ON の場合は裏コード化を禁止
            const bool isAlreadySecondaryDom = (normalizePitchClass(current.rootPitchClass) == normalizePitchClass(targetRootPc + 7));
            const bool useTritoneSub = !profile.forceDiatonic && (profile.allowTritoneSub || isAlreadySecondaryDom);

            ChordBlock passingChord = computeApproachChordForTarget(
                current,
                targetRootPc,
                secondStart,
                secondDur,
                useTritoneSub,
                passingTargetTop
            );
            if (profile.melodyLock)
                passingChord.targetTopNote = current.targetTopNote;
            result.push_back(passingChord);
        }
    }

    return result;
}

//==============================================================================
// オフライン・リハーモナイズ実行パイプライン
//==============================================================================
std::vector<ChordBlock> MidiReharmonizerEngine::applyLockedBlocksToProgression(
    std::span<const ChordBlock> candidateProgression,
    std::span<const ChordBlock> lockedBlocks)
{
    if (lockedBlocks.empty())
    {
        return { candidateProgression.begin(), candidateProgression.end() };
    }

    std::vector<ChordBlock> current(candidateProgression.begin(), candidateProgression.end());
    constexpr double kGapPpq = 0.08;

    for (const auto& locked : lockedBlocks)
    {
        const double lockStart = locked.startPpq;
        const double lockEnd   = locked.startPpq + locked.durationPpq;

        std::vector<ChordBlock> nextPass;
        nextPass.reserve(current.size() + 2);

        for (const auto& cand : current)
        {
            const double candStart = cand.startPpq;
            const double candEnd   = cand.startPpq + cand.durationPpq;

            // ロック区間と重ならない候補ブロックはそのまま維持
            if (candEnd <= lockStart + 0.12 || candStart >= lockEnd - 0.12)
            {
                nextPass.push_back(cand);
                continue;
            }

            // 候補ブロックの前半がロック区間より前に突き出している場合は前半部分だけ残す
            if (candStart < lockStart - 0.25)
            {
                ChordBlock leftPart = cand;
                leftPart.durationPpq = std::max(0.25, lockStart - candStart - kGapPpq);
                leftPart.isLocked = false;
                leftPart.lockedVoicing = {};
                nextPass.push_back(leftPart);
            }

            // 候補ブロックの後半がロック区間より後に突き出している場合は後半部分だけ残す
            if (candEnd > lockEnd + 0.25)
            {
                ChordBlock rightPart = cand;
                rightPart.startPpq = lockEnd + kGapPpq;
                rightPart.durationPpq = std::max(0.25, candEnd - rightPart.startPpq);
                rightPart.isLocked = false;
                rightPart.lockedVoicing = {};
                nextPass.push_back(rightPart);
            }
        }

        ChordBlock pinned = locked;
        pinned.isLocked = true;
        nextPass.push_back(pinned);

        std::sort(nextPass.begin(), nextPass.end(),
            [](const ChordBlock& a, const ChordBlock& b)
            {
                return a.startPpq < b.startPpq;
            });

        current = std::move(nextPass);
    }

    return current;
}

bool MidiReharmonizerEngine::triggerOfflineReharmonize(bool usePreviewProgressionIfEmpty)
{
    std::vector<ChordBlock> progression;
    std::vector<ChordBlock> lockedCopy;
    std::vector<TimedMidiEvent> capturedCopy;
    VoicingProfile profileCopy;

    {
        const juce::SpinLock::ScopedLockType lock(sequenceLock_);
        profileCopy  = activeProfile_;
        lockedCopy   = lockedBlocks_;
        capturedCopy = capturedSequence_;
        if (!capturedSequence_.empty())
        {
            progression = extractChordProgressionFromCaptured();
        }
    }

    // キャプチャされたMIDIがまだ無い場合、UI上でボイシング変化を即座に確認できるよう
    // デフォルトの2-5-1-6進行（Dm7 -> G7 -> Cmaj7 -> A7）を生成
    if (progression.empty() && usePreviewProgressionIfEmpty)
    {
        ChordBlock b0 { 2, ChordQuality::Minor7,    0.0,  3.8, 88, 72 };
        b0.originalPitches = { 50, 57, 60, 65, 72 };
        ChordBlock b1 { 7, ChordQuality::Dominant7, 4.0,  3.8, 92, 71 };
        b1.originalPitches = { 43, 53, 59, 62, 71 };
        ChordBlock b2 { 0, ChordQuality::Major7,    8.0,  3.8, 86, 71 };
        b2.originalPitches = { 48, 55, 59, 64, 71 };
        ChordBlock b3 { 9, ChordQuality::Dominant7, 12.0, 3.8, 90, 69 };
        b3.originalPitches = { 45, 55, 61, 64, 69 };
        progression = { b0, b1, b2, b3 };
    }

    if (progression.empty())
        return false;

    const int currentTranspose   = std::clamp(transposeSemitones_.load(std::memory_order_relaxed), -12, 12);
    const bool isCompareOriginal = compareOriginalMode_.load(std::memory_order_acquire);
    const bool isZeroMutation    = (profileCopy.mutationAmount < 0.1f);

    //==========================================================================
    // v1.5 機能F: 「原曲完全再現モード (Mutation < 0.1%)」および「A/Bコンペア (Original)」
    //             Viterbiアルゴリズムおよびパッシングコード分割を完全にスキップし、
    //             capturedSequence_（元MIDI）のノート情報をそのまま純粋な形でパススルー出力する
    //==========================================================================
    if (isCompareOriginal || isZeroMutation)
    {
        if (!isCompareOriginal && !lockedCopy.empty())
        {
            progression = applyLockedBlocksToProgression(progression, lockedCopy);
        }

        std::vector<Voicing> originalVoicings;
        originalVoicings.reserve(progression.size());
        for (const auto& block : progression)
        {
            if (!isCompareOriginal && block.isLocked && !block.lockedVoicing.isEmpty())
            {
                originalVoicings.push_back(block.lockedVoicing);
            }
            else if (!block.originalPitches.empty())
            {
                Voicing v;
                v.midiNotes = block.originalPitches;
                originalVoicings.push_back(std::move(v));
            }
            else
            {
                const int rootNote = 48 + normalizePitchClass(block.rootPitchClass);
                Voicing v;
                v.midiNotes = { rootNote, rootNote + 4, rootNote + 7, rootNote + 11 };
                originalVoicings.push_back(std::move(v));
            }
        }

        std::vector<TimedMidiEvent> passthroughEvents;
        if (!capturedCopy.empty() && (isCompareOriginal || lockedCopy.empty()))
        {
            // capturedSequence_ のノート・ベロシティ・発音タイミングを100%純粋にパススルーコピー（TransposeおよびCh.1/2分離のみ反映）
            passthroughEvents = capturedCopy;
            for (auto& ev : passthroughEvents)
            {
                if (!ev.message.isNoteOnOrOff())
                    continue;

                const int rawPitch = ev.message.getNoteNumber();
                int targetChannel  = 1;

                if (profileCopy.splitMidiChannels)
                {
                    bool isBassNote = false;
                    for (const auto& b : progression)
                    {
                        if (ev.ppqPosition >= b.startPpq - 0.25 && ev.ppqPosition <= b.startPpq + b.durationPpq + 0.25)
                        {
                            const int bassPitch = b.originalBassNote.value_or(
                                b.originalPitches.empty() ? -1 : b.originalPitches.front());
                            if (rawPitch == bassPitch)
                                isBassNote = true;
                            break;
                        }
                    }
                    targetChannel = isBassNote ? 1 : 2;
                }

                ev.message.setChannel(targetChannel);
                if (currentTranspose != 0)
                {
                    const int shifted = std::clamp(rawPitch + currentTranspose, 0, 127);
                    ev.message.setNoteNumber(shifted);
                }
            }
        }
        else
        {
            // 特定ブロックのみ手動ロック・ホイール転回されている場合は、揺らぎ無しの純粋な設定で各ブロックを描画
            VoicingProfile cleanProfile = profileCopy;
            cleanProfile.timingJitterMs        = 0.0f;
            cleanProfile.strumSpreadMs         = 0.0f;
            cleanProfile.velocityHumanizeRange = 0.0f;
            cleanProfile.topVoiceVelocityBoost = 0.0f;
            cleanProfile.baseVelocityOffset    = 0.0f;
            cleanProfile.compingStyle          = CompingStyle::Sustained;
            passthroughEvents = renderVoicingPathToMidiEvents(progression, originalVoicings, cleanProfile);
        }

        {
            const juce::SpinLock::ScopedLockType lock(sequenceLock_);
            lastProgression_            = std::move(progression);
            lastOptimalVoicings_        = std::move(originalVoicings);
            reharmonizedSequence_       = std::move(passthroughEvents);
            renderedTransposeSemitones_ = currentTranspose;
        }

        return true;
    }

    // Phase 13: ボイシング探索の前段階で Harmonic Rhythm を動的に分割し、
    //           直後のターゲットコードへ向かうパッシングコード（裏コード・セカンダリードミナント等）を挿入
    progression = subdivideAndInsertPassingChords(progression, profileCopy);

    // v1.5 機能B: ロック（ピン留め）されているコードブロックを進行へ統合
    if (!lockedCopy.empty())
    {
        progression = applyLockedBlocksToProgression(progression, lockedCopy);
    }

    // Phase 3 & 13: 分割されたパッシングコードを含む全時間ステップを Viterbi トレリスへ統合し、
    //               曲全体を通して最もボイスリーディングが滑らかな最適ボイシング経路を探索
    const auto optimalVoicings = findOptimalVoicingPath(progression, profileCopy);

    // 発音タイミング変調（ストラム／ジッター）、ベロシティ変調、およびトランスポーズ（移調）を適用してMIDIイベント列へ変換
    auto generatedEvents = renderVoicingPathToMidiEvents(progression, optimalVoicings, profileCopy);

    {
        const juce::SpinLock::ScopedLockType lock(sequenceLock_);
        lastProgression_           = std::move(progression);
        lastOptimalVoicings_       = optimalVoicings;
        reharmonizedSequence_      = std::move(generatedEvents);
        renderedTransposeSemitones_ = currentTranspose;
    }

    return true;
}

std::vector<ChordBlock> MidiReharmonizerEngine::extractChordProgressionFromCaptured() const
{
    std::vector<ChordBlock> blocks;

    // NoteOnとNoteOffをペアリングしてノート区間リストを作成
    struct RawNote
    {
        int pitch { 60 };
        int velocity { 90 };
        double startPpq { 0.0 };
        double endPpq { 0.0 };
    };

    std::vector<RawNote> notes;
    std::array<int, 128> activeNoteIndex;
    activeNoteIndex.fill(-1);

    for (const auto& ev : capturedSequence_)
    {
        if (ev.message.isNoteOn())
        {
            const int noteNum = ev.message.getNoteNumber();
            // 既に鳴っている同名ノートがあれば一旦閉じる
            if (activeNoteIndex[static_cast<size_t>(noteNum)] >= 0)
            {
                notes[static_cast<size_t>(activeNoteIndex[static_cast<size_t>(noteNum)])].endPpq = ev.ppqPosition;
            }
            activeNoteIndex[static_cast<size_t>(noteNum)] = static_cast<int>(notes.size());
            notes.push_back(RawNote { noteNum, ev.message.getVelocity(), ev.ppqPosition, ev.ppqPosition + 1.0 });
        }
        else if (ev.message.isNoteOff())
        {
            const int noteNum = ev.message.getNoteNumber();
            const int idx = activeNoteIndex[static_cast<size_t>(noteNum)];
            if (idx >= 0)
            {
                notes[static_cast<size_t>(idx)].endPpq = std::max(notes[static_cast<size_t>(idx)].startPpq + 0.05, ev.ppqPosition);
                activeNoteIndex[static_cast<size_t>(noteNum)] = -1;
            }
        }
    }

    if (notes.empty())
        return blocks;

    // 開始PPQの近いノート（許容誤差 0.20 PPQ = 手弾きのストラム等も同一和音として束ねる）を同一の和音ブロックとしてクラスタリング
    // v1.5 フェイルセーフ2: Viterbiトレリスの計算爆発を防ぐため、抽出する ChordBlock 数（最大64個）および 8小節（32.0 PPQ）で二重ガード
    constexpr double kChordOnsetTolerancePpq = 0.20;
    constexpr size_t kMaxExtractedChordBlocks = 64;
    constexpr double kMaxProgressionSpanPpq   = 32.0;

    size_t i = 0;
    while (i < notes.size())
    {
        if (blocks.size() >= kMaxExtractedChordBlocks || notes[i].startPpq >= kMaxProgressionSpanPpq)
        {
            break;
        }

        const double clusterStartPpq = notes[i].startPpq;
        double clusterEndPpq = std::min(notes[i].endPpq, kMaxProgressionSpanPpq);
        int velocitySum = 0;
        int maxPitch = -1;
        int minPitch = 128;
        std::vector<int> clusterPitches;

        while (i < notes.size() && (notes[i].startPpq - clusterStartPpq) <= kChordOnsetTolerancePpq)
        {
            clusterPitches.push_back(notes[i].pitch);
            clusterEndPpq = std::max(clusterEndPpq, std::min(notes[i].endPpq, kMaxProgressionSpanPpq));
            velocitySum += notes[i].velocity;
            maxPitch = std::max(maxPitch, notes[i].pitch);
            minPitch = std::min(minPitch, notes[i].pitch);
            ++i;
        }

        ChordBlock block;
        inferChordFromNotes(clusterPitches, block.rootPitchClass, block.quality);
        block.startPpq = clusterStartPpq;
        block.durationPpq = std::max(0.25, clusterEndPpq - clusterStartPpq);
        block.baseVelocity = std::clamp(velocitySum / static_cast<int>(clusterPitches.size()), 1, 127);
        if (maxPitch >= 0)
            block.targetTopNote = maxPitch;
        if (minPitch <= 127)
            block.originalBassNote = minPitch;

        std::sort(clusterPitches.begin(), clusterPitches.end());
        clusterPitches.erase(std::unique(clusterPitches.begin(), clusterPitches.end()), clusterPitches.end());
        block.originalPitches = std::move(clusterPitches);

        blocks.push_back(block);
    }

    // 短いスタッカート和音などが入力された場合でも、次のコードまでの和声スロットが2拍以上空いている場合は
    // 自然なサステイン長へ補完し、Phase 13のパッシングコード分割が有効に機能するようにする
    for (size_t b = 0; b + 1 < blocks.size(); ++b)
    {
        const double slotPpq = blocks[b + 1].startPpq - blocks[b].startPpq;
        if (slotPpq >= 2.0 && blocks[b].durationPpq < slotPpq * 0.85)
        {
            blocks[b].durationPpq = std::max(blocks[b].durationPpq, slotPpq - 0.12);
        }
    }

    DBG("MidiReharmonizerEngine::extractChordProgressionFromCaptured: Extracted "
        << blocks.size() << " ChordBlocks from " << notes.size() << " notes.");

    return blocks;
}

std::vector<TimedMidiEvent> MidiReharmonizerEngine::renderVoicingPathToMidiEvents(
    std::span<const ChordBlock> progression,
    std::span<const Voicing> voicings,
    const VoicingProfile& profile) const
{
    std::vector<TimedMidiEvent> events;
    const size_t count = std::min(progression.size(), voicings.size());
    events.reserve(count * 16);

    // 決定論的シードによる乱数生成器（オフライン再実行時の再現性を担保）
    std::mt19937 rng(20261028u);
    std::uniform_real_distribution<float> jitterDist(-profile.timingJitterMs, profile.timingJitterMs);
    std::uniform_real_distribution<float> velDist(-profile.velocityHumanizeRange, profile.velocityHumanizeRange);

    // 基準テンポ120BPM相当でのミリ秒 -> PPQ変換係数 (120BPM = 1秒に2拍 = 500ms/PPQ)
    constexpr double kPpqPerMsAt120Bpm = 1.0 / 500.0;

    // v1.5 機能A: 出力直前のMIDIノート番号に対する一括トランスポーズ（移調）量 (-12 〜 +12 半音)
    const int transposeShift = std::clamp(transposeSemitones_.load(std::memory_order_relaxed), -12, 12);

    // コンピング用の1打（発音オフセットPPQ・長さPPQ・ベロシティ倍率）を表す構造体
    struct CompingHit
    {
        double offsetPpq { 0.0 };
        double durationPpq { 1.0 };
        float velocityScale { 1.0f };
    };

    // v1.5 機能H: ベース声部（最低音 v==0）は常に Ch.1、上声部（v>=1）は splitMidiChannels ON時に Ch.2 へ出力
    const int bassChannel  = 1;
    const int upperChannel = profile.splitMidiChannels ? 2 : 1;

    auto emitVoiceNote = [&](int noteNumber,
                             int midiChannel,
                             bool isTopVoice,
                             size_t voiceIndex,
                             double hitStartPpq,
                             double hitDurationPpq,
                             int baseVelocity,
                             float velocityScale)
    {
        // ストラム遅延（下からv番目の声部に応じて発音タイミングをずらす）＋ランダムジッター
        const double strumOffsetMs  = static_cast<double>(voiceIndex) * static_cast<double>(profile.strumSpreadMs);
        const double jitterOffsetMs = static_cast<double>(jitterDist(rng));
        const double totalOffsetPpq = (strumOffsetMs + jitterOffsetMs) * kPpqPerMsAt120Bpm;

        const double noteOnPpq  = std::max(0.0, hitStartPpq + totalOffsetPpq);
        const double noteOffPpq = std::max(noteOnPpq + 0.05, hitStartPpq + hitDurationPpq);

        // ベロシティ変調（全体オフセット＋トップノート強調＋コンピング抑揚＋ヒューマナイズ）
        const float boost     = isTopVoice ? profile.topVoiceVelocityBoost : 0.0f;
        const float rawVel    = (static_cast<float>(baseVelocity) + profile.baseVelocityOffset + boost) * velocityScale
                                + velDist(rng);
        const int finalVelocity = std::clamp(static_cast<int>(std::round(rawVel)), 1, 127);

        TimedMidiEvent onEv;
        onEv.ppqPosition = noteOnPpq;
        onEv.message     = juce::MidiMessage::noteOn(midiChannel, noteNumber, static_cast<juce::uint8>(finalVelocity));
        events.push_back(onEv);

        TimedMidiEvent offEv;
        offEv.ppqPosition = noteOffPpq;
        offEv.message     = juce::MidiMessage::noteOff(midiChannel, noteNumber);
        events.push_back(offEv);
    };

    for (size_t step = 0; step < count; ++step)
    {
        const auto& chord   = progression[step];
        const auto& voicing = voicings[step];
        const size_t numVoices = voicing.midiNotes.size();
        if (numVoices == 0)
            continue;

        // 1. "Sustained (白玉)": 全声部が同時に発音し、ブロック長まで伸びる
        if (profile.compingStyle == CompingStyle::Sustained)
        {
            for (size_t v = 0; v < numVoices; ++v)
            {
                const int noteNumber  = std::clamp(voicing.midiNotes[v] + transposeShift, 0, 127);
                const int voiceCh     = (v == 0) ? bassChannel : upperChannel;
                const bool isTopVoice = (v + 1 == numVoices);
                emitVoiceNote(noteNumber, voiceCh, isTopVoice, v, chord.startPpq, chord.durationPpq, chord.baseVelocity, 1.0f);
            }
            continue;
        }

        // 2. "Neo-Soul Pocket" / "Bossa Bounce": 左手（最低音 v==0）と右手（上声部 v>=1）の独立コンピング
        std::vector<CompingHit> bassHits;
        std::vector<CompingHit> upperHits;

        if (profile.compingStyle == CompingStyle::NeoSoulPocket)
        {
            // 左手（最低音）は1拍目からどっしり長く伸ばしてポケットの土台を作る
            bassHits.push_back(CompingHit { 0.0, chord.durationPpq, 1.04f });

            // 右手（2声部目以降）はレイドバック＆裏拍（+0.5 PPQ等）にずらしてネオソウル風に刻む
            if (chord.durationPpq >= 3.2)
            {
                // 1小節（4拍）ブロック: 1拍裏 (+0.50) の長めコード -> 3拍目レイドバック (+2.05) のポケット -> 4拍裏 (+3.50) のゴーストピックアップ
                upperHits.push_back(CompingHit { 0.50, 1.25, 0.96f });
                upperHits.push_back(CompingHit { 2.05, 0.90, 0.84f });
                if (chord.durationPpq >= 3.65)
                {
                    upperHits.push_back(CompingHit { 3.50, std::max(0.20, chord.durationPpq - 3.52), 0.90f });
                }
            }
            else if (chord.durationPpq >= 1.6)
            {
                // 2拍ブロック: 1拍裏 (+0.50) -> 2拍裏 (+1.50) のシンコペーション刻み
                upperHits.push_back(CompingHit { 0.50, 0.82, 0.95f });
                if (chord.durationPpq >= 1.70)
                {
                    upperHits.push_back(CompingHit { 1.50, std::max(0.18, chord.durationPpq - 1.52), 0.86f });
                }
            }
            else
            {
                // 短いパッシングコード等: ベースよりわずかに遅らせて（+0.07 PPQ レイドバック）発音
                const double laidBackOffset = std::min(0.08, chord.durationPpq * 0.18);
                upperHits.push_back(CompingHit { laidBackOffset, std::max(0.15, chord.durationPpq - laidBackOffset), 0.94f });
            }
        }
        else // CompingStyle::BossaBounce
        {
            // 左手（最低音）は1拍目から長く保持（4拍ブロックでは1拍目〜2拍目＋3拍目〜4拍目の2分音符バウンス、またはロングトーン）
            if (chord.durationPpq >= 3.2)
            {
                bassHits.push_back(CompingHit { 0.0,  1.90, 1.02f });
                bassHits.push_back(CompingHit { 2.0,  std::max(0.50, chord.durationPpq - 2.0), 0.96f });
            }
            else
            {
                bassHits.push_back(CompingHit { 0.0, chord.durationPpq, 1.02f });
            }

            // 右手（2声部目以降）はボサノヴァ特有のシンコペーション（1拍裏 +0.5 / 2拍裏 +1.5 / 3拍裏 +2.5 / 4拍裏 +3.5）で軽快に刻む
            if (chord.durationPpq >= 3.2)
            {
                upperHits.push_back(CompingHit { 0.50, 0.82, 0.92f });
                upperHits.push_back(CompingHit { 1.50, 0.82, 0.96f });
                upperHits.push_back(CompingHit { 2.50, 0.65, 0.85f });
                if (chord.durationPpq >= 3.65)
                {
                    upperHits.push_back(CompingHit { 3.50, std::max(0.18, chord.durationPpq - 3.52), 0.90f });
                }
            }
            else if (chord.durationPpq >= 1.6)
            {
                upperHits.push_back(CompingHit { 0.50, 0.78, 0.94f });
                if (chord.durationPpq >= 1.70)
                {
                    upperHits.push_back(CompingHit { 1.50, std::max(0.18, chord.durationPpq - 1.52), 0.88f });
                }
            }
            else if (chord.durationPpq >= 0.75)
            {
                upperHits.push_back(CompingHit { 0.25, std::max(0.18, chord.durationPpq - 0.27), 0.92f });
            }
            else
            {
                upperHits.push_back(CompingHit { 0.04, std::max(0.15, chord.durationPpq - 0.04), 0.92f });
            }
        }

        // 左手ベース声部 (v == 0, Ch.1) の出力
        {
            const int bassNoteNumber = std::clamp(voicing.midiNotes[0] + transposeShift, 0, 127);
            const bool isTopVoice    = (numVoices == 1);
            for (const auto& hit : bassHits)
            {
                emitVoiceNote(
                    bassNoteNumber,
                    bassChannel,
                    isTopVoice,
                    0,
                    chord.startPpq + hit.offsetPpq,
                    hit.durationPpq,
                    chord.baseVelocity,
                    hit.velocityScale
                );
            }
        }

        // 右手上声部 (v >= 1, Ch.1 or Ch.2) の出力
        for (const auto& hit : upperHits)
        {
            for (size_t v = 1; v < numVoices; ++v)
            {
                const int noteNumber  = std::clamp(voicing.midiNotes[v] + transposeShift, 0, 127);
                const bool isTopVoice = (v + 1 == numVoices);
                emitVoiceNote(
                    noteNumber,
                    upperChannel,
                    isTopVoice,
                    v - 1, // 上声部内でのストラム順序
                    chord.startPpq + hit.offsetPpq,
                    hit.durationPpq,
                    chord.baseVelocity,
                    hit.velocityScale
                );
            }
        }
    }

    // PPQ順（時系列順）にソート（同一時刻の場合は NoteOff を NoteOn より先に処理）
    std::sort(events.begin(), events.end(), [](const TimedMidiEvent& a, const TimedMidiEvent& b)
    {
        if (std::abs(a.ppqPosition - b.ppqPosition) > 1.0e-6)
            return a.ppqPosition < b.ppqPosition;
        return a.message.isNoteOff() && !a.message.isNoteOn() && b.message.isNoteOn();
    });

    // Phase 9: Audio Groove Sync が有効な場合、環境レイヤーのヒューマナイズよりも後に
    //          GrooveMap（トランジェント位置）への吸着を最優先で上書き適用
    if (isGrooveSyncEnabled())
    {
        std::vector<double> activeGrooveMap;
        {
            const juce::SpinLock::ScopedLockType lock(sequenceLock_);
            activeGrooveMap = grooveMapPpq_;
        }

        // まだサイドチェイン音声が再生されていない場合のプレビュー用レイドバック・ポケットGrooveMap
        if (activeGrooveMap.empty())
        {
            activeGrooveMap = {
                0.08, 1.54, 2.52, 3.56,
                4.09, 5.55, 6.53, 7.54,
                8.07, 9.53, 10.52, 11.55,
                12.08, 13.54, 14.06, 15.52
            };
        }

        quantizeEventsToGrooveMap(events, activeGrooveMap);
    }

    return events;
}

void MidiReharmonizerEngine::quantizeEventsToGrooveMap(
    std::vector<TimedMidiEvent>& events,
    std::span<const double> grooveMapPpq) const
{
    if (events.empty() || grooveMapPpq.empty())
        return;

    for (auto& ev : events)
    {
        if (!ev.message.isNoteOn())
            continue;

        const double originalPpq     = ev.ppqPosition;
        const double originalLoopPpq = std::fmod(originalPpq, 16.0);
        const double loopBasePpq     = originalPpq - originalLoopPpq;

        double nearestTransientLoopPpq = originalLoopPpq;
        double minDiff = std::numeric_limits<double>::max();

        for (double transientPpq : grooveMapPpq)
        {
            const double tLoop = std::fmod(std::max(0.0, transientPpq), 16.0);
            const double diff  = std::abs(tLoop - originalLoopPpq);
            if (diff < minDiff)
            {
                minDiff = diff;
                nearestTransientLoopPpq = tLoop;
            }
        }

        // 1拍（1.0 PPQ）以内の最近傍トランジェントへ NoteOn を完全吸着（ヒューマナイズを上書き）
        if (minDiff <= 1.0)
        {
            ev.ppqPosition = std::max(0.0, loopBasePpq + nearestTransientLoopPpq);
        }
    }

    std::sort(events.begin(), events.end(), [](const TimedMidiEvent& a, const TimedMidiEvent& b)
    {
        if (std::abs(a.ppqPosition - b.ppqPosition) > 1.0e-6)
            return a.ppqPosition < b.ppqPosition;
        return a.message.isNoteOff() && !a.message.isNoteOn() && b.message.isNoteOn();
    });
}

//==============================================================================
// リアルタイムスレッド用キャプチャ＆再生フック
//==============================================================================
void MidiReharmonizerEngine::captureIncomingMidi(
    const juce::MidiBuffer& incomingBuffer,
    const juce::Optional<juce::AudioPlayHead::PositionInfo>& posInfo)
{
    if (incomingBuffer.isEmpty())
        return;

    const bool isPlaying = posInfo.hasValue() && posInfo->getIsPlaying();
    if (!isPlaying)
        return;

    const juce::SpinLock::ScopedTryLockType tryLock(sequenceLock_);
    if (!tryLock.isLocked())
        return;

    const double blockStartPpq = posInfo->getPpqPosition().orFallback(0.0);
    const double bpm = posInfo->getBpm().orFallback(120.0);
    const int64_t blockStartSample = posInfo->getTimeInSamples().orFallback(0);
    const double ppqPerSample = (bpm / 60.0) / currentSampleRate_;

    for (const auto metadata : incomingBuffer)
    {
        const auto msg = metadata.getMessage();
        const int sampleOffset = metadata.samplePosition;

        if (msg.isNoteOnOrOff() || msg.isController() || msg.isPitchWheel())
        {
            TimedMidiEvent ev;
            ev.samplePosition = blockStartSample + sampleOffset;
            ev.ppqPosition = blockStartPpq + (static_cast<double>(sampleOffset) * ppqPerSample);
            ev.message = msg;

            capturedSequence_.push_back(ev);
        }
    }
}

void MidiReharmonizerEngine::injectReharmonizedMidi(
    juce::MidiBuffer& targetBuffer,
    const juce::Optional<juce::AudioPlayHead::PositionInfo>& posInfo,
    int numSamples)
{
    // 重要: リアルタイムに入力されたMIDIイベント（Note On/Off等）を絶対に .clear() で消去せずスルー維持する
    const bool isPlaying = posInfo.hasValue() && posInfo->getIsPlaying();
    if (!isPlaying)
        return;

    const juce::SpinLock::ScopedTryLockType tryLock(sequenceLock_);
    if (!tryLock.isLocked() || reharmonizedSequence_.empty())
        return;

    const double blockStartPpq = posInfo->getPpqPosition().orFallback(0.0);
    const double bpm = posInfo->getBpm().orFallback(120.0);
    const double ppqPerSample = (bpm / 60.0) / currentSampleRate_;
    const double blockEndPpq = blockStartPpq + (static_cast<double>(numSamples) * ppqPerSample);

    auto it = std::lower_bound(
        reharmonizedSequence_.begin(),
        reharmonizedSequence_.end(),
        blockStartPpq,
        [](const TimedMidiEvent& ev, double targetPpq)
        {
            return ev.ppqPosition < targetPpq;
        }
    );

    for (; it != reharmonizedSequence_.end() && it->ppqPosition < blockEndPpq; ++it)
    {
        const double offsetPpq = it->ppqPosition - blockStartPpq;
        const int sampleOffset = juce::jlimit(
            0,
            std::max(0, numSamples - 1),
            static_cast<int>(std::round(offsetPpq / ppqPerSample))
        );

        targetBuffer.addEvent(it->message, sampleOffset);
    }
}

} // namespace reharm::engine
