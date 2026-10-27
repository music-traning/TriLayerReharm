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

    std::vector<TimedMidiEvent> importedEvents;
    importedEvents.reserve(static_cast<size_t>(sequence.getNumEvents()));

    for (int i = 0; i < sequence.getNumEvents(); ++i)
    {
        const auto* holder = sequence.getEventPointer(i);
        if (holder == nullptr)
            continue;

        const auto& msg = holder->message;
        if (msg.isNoteOnOrOff() || msg.isController() || msg.isPitchWheel())
        {
            TimedMidiEvent ev;
            ev.ppqPosition = std::max(0.0, msg.getTimeStamp() / safeTpqn);
            ev.samplePosition = 0;
            ev.message = msg;
            importedEvents.push_back(ev);
        }
    }

    // タイムスタンプ（PPQ）昇順にソート（同一時刻の場合は NoteOff を NoteOn より先に処理）
    std::stable_sort(importedEvents.begin(), importedEvents.end(),
        [](const TimedMidiEvent& a, const TimedMidiEvent& b)
        {
            if (std::abs(a.ppqPosition - b.ppqPosition) > 1.0e-6)
                return a.ppqPosition < b.ppqPosition;
            return a.message.isNoteOff() && a.message.isNoteOn() == false && b.message.isNoteOn();
        });

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
    activeProfile_.mutationAmount = mutationAmount_.load(std::memory_order_relaxed);
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
        case 0: // Artist A (Smooth & Tight Voice Leading)
            artist.spreadTargetSemitones = 14.0f;
            artist.voiceLeadingWeight = 3.5f;
            artist.topNoteLeapWeight = 4.0f;
            artist.timingJitterMs = 3.0f;
            artist.strumSpreadMs = 6.0f;
            artist.topVoiceVelocityBoost = 12.0f;
            break;
        case 1: // Artist B (Wide Expressive Strum)
            artist.spreadTargetSemitones = 19.0f;
            artist.voiceLeadingWeight = 1.8f;
            artist.topNoteLeapWeight = 2.2f;
            artist.timingJitterMs = 7.0f;
            artist.strumSpreadMs = 18.0f;
            artist.topVoiceVelocityBoost = 15.0f;
            break;
        case 2: // Artist C (Rhythmic Drunk Groove)
            artist.spreadTargetSemitones = 12.0f;
            artist.voiceLeadingWeight = 2.5f;
            artist.topNoteLeapWeight = 3.0f;
            artist.timingJitterMs = 14.0f;
            artist.strumSpreadMs = 10.0f;
            artist.velocityHumanizeRange = 16.0f;
            artist.topVoiceVelocityBoost = 6.0f;
            break;
        case 3: // Artist D (Minimal & Precise)
        default:
            artist.spreadTargetSemitones = 16.0f;
            artist.voiceLeadingWeight = 4.0f;
            artist.topNoteLeapWeight = 4.5f;
            artist.timingJitterMs = 1.5f;
            artist.strumSpreadMs = 2.0f;
            artist.velocityHumanizeRange = 4.0f;
            artist.topVoiceVelocityBoost = 8.0f;
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

    const bool useTritoneSub = !chord.isPassingChord
                               && !isBasicSafeZone
                               && ((profile.allowTritoneSub && chord.quality == ChordQuality::Dominant7)
                                   || (normMutation > 0.82f && (chord.quality == ChordQuality::Dominant7 || chord.quality == ChordQuality::Minor7)));

    const int effectiveRootPc = useTritoneSub
                                    ? normalizePitchClass(chord.rootPitchClass + 6)
                                    : normalizePitchClass(chord.rootPitchClass);

    const auto intervals = getChordIntervals(chord.quality);

    // 許容されるピッチクラス集合を構築
    std::array<bool, 12> allowedPitchClasses {};
    for (int iv : intervals.coreIntervals)
        allowedPitchClasses[static_cast<size_t>(normalizePitchClass(effectiveRootPc + iv))] = true;

    if (!isBasicSafeZone && (profile.allowTensions || normMutation > 0.45f))
    {
        for (int iv : intervals.tensionIntervals)
            allowedPitchClasses[static_cast<size_t>(normalizePitchClass(effectiveRootPc + iv))] = true;
    }

    // Chaos領域（70%〜100%）ではオルタードテンション（b9, #9, #11, b13）やモーダルインターチェンジ音を追加解禁
    if (isChaosZone)
    {
        constexpr std::array<int, 4> kAlteredAndModalIntervals { 1, 3, 6, 8 }; // b9, #9(m3), #11(b5), b13(#5)
        for (int altIv : kAlteredAndModalIntervals)
        {
            allowedPitchClasses[static_cast<size_t>(normalizePitchClass(effectiveRootPc + altIv))] = true;
        }
    }

    // 指定音域 [minPitch, maxPitch] 内で使用可能なMIDIノート番号を列挙
    std::vector<int> candidateNotes;
    candidateNotes.reserve(36);
    for (int pitch = profile.minPitch; pitch <= profile.maxPitch; ++pitch)
    {
        if (allowedPitchClasses[static_cast<size_t>(normalizePitchClass(pitch))])
            candidateNotes.push_back(pitch);
    }

    std::vector<Voicing> results;
    if (static_cast<int>(candidateNotes.size()) < profile.minVoices)
        return results;

    results.reserve(512);
    // 計算爆発を防ぐための候補数上限ガード
    constexpr size_t kMaxCandidatesPerChord = 1024;

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
            // 完成したボイシングの全体スパンチェック
            const int totalSpan = currentNotes.back() - currentNotes.front();
            if (totalSpan >= 4 && totalSpan <= profile.maxTotalSpan)
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

            // [枝刈り1] 同一ピッチクラス（オクターブ重複）はボイシング内で最大2回まで、
            //           かつルート・5th以外の重複は1回までに制限して不自然な重複と計算爆発を防ぐ
            const bool isRoot = (nextPc == effectiveRootPc);
            const int maxPcDuplicates = isRoot ? 2 : 1;
            if (pitchClassCount[static_cast<size_t>(nextPc)] >= maxPcDuplicates)
                continue;

            if (!currentNotes.empty())
            {
                const int prevPitch = currentNotes.back();
                const int adjacentInterval = nextPitch - prevPitch;
                const int totalSpanSoFar = nextPitch - currentNotes.front();

                // [枝刈り2] 全体スパンが上限を超えたら、これ以降のノート（さらに高音）も全て超えるためbreakで枝刈り
                if (totalSpanSoFar > profile.maxTotalSpan)
                    break;

                // [枝刈り3] 隣接声部間のインターバルが広すぎる配置をハードリミットで除外
                //           最低音（Bass）と第2音の間は maxBassToTenorInterval、それより上は maxAdjacentInterval を適用
                const int maxAllowedGap = (currentCount == 1)
                                              ? profile.maxBassToTenorInterval
                                              : profile.maxAdjacentInterval;
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

    const bool useTritoneSub = !chord.isPassingChord
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
        cost += profile.omitRootPenalty * safetyPenaltyScale;
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

    // 5. ターゲット・トップノート（元のメロディ音）が指定されている場合の追従コスト
    if (chord.targetTopNote.has_value())
    {
        const int topDiff = std::abs(currentVoicing.getTopNote() - *chord.targetTopNote);
        cost += static_cast<float>(topDiff) * 1.5f * (1.0f - chaosFactor * 0.85f);
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

    // トップノート（最高音）の跳躍ペナルティも Mutation に応じてスケール
    const float topNoteLeap = static_cast<float>(std::abs(currentVoicing.getTopNote() - prevVoicing.getTopNote()));
    transitionCost += topNoteLeap * profile.topNoteLeapWeight * vlScale;

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

    // 1. 各時間ステップ（各コードブロック）の候補生成と動的計画法による前方確率（最小累積コスト）計算
    for (size_t t = 0; t < numSteps; ++t)
    {
        auto candidates = generateCandidateVoicings(progression[t], profile);

        // 万が一ハードリミットが厳しすぎて候補が0件になった場合のフォールバック（ルート＋3rd＋7thの基本形）
        if (candidates.empty())
        {
            const int baseMidi = std::clamp(60 + normalizePitchClass(progression[t].rootPitchClass) - 12,
                                            profile.minPitch,
                                            profile.maxPitch - 12);
            candidates.push_back(Voicing { { baseMidi, baseMidi + 4, baseMidi + 7, baseMidi + 11 } });
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
                    currentLayer[j].voicing, progression[0], profile);
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
                    currentLayer[j].voicing, progression[t], profile);

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

        // 元のコードの終端時刻（startPpq + durationPpq）と次のコードの開始時刻から実質グリッド幅を推定
        const double nextStartPpq = (i + 1 < numBlocks)
                                        ? baseProgression[i + 1].startPpq
                                        : (current.startPpq + std::ceil(totalSpanPpq));
        const double gridSlotPpq  = std::max(totalSpanPpq + kNoteGapPpq, nextStartPpq - current.startPpq);

        // Chaos極限領域（85%〜100%）かつ1小節（約3.4拍以上）の長い白玉コードの場合：
        // バップのように3分割〜4分割し、「裏コードのさらに裏」や連続ドミナント・チェインを挿入
        if (mutation >= 92.0f && gridSlotPpq >= 3.4)
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
                current, subOfSubRoot, current.startPpq + stepPpq, noteDur, true, targetNext.targetTopNote);
            beat2.rootPitchClass = preApproachRoot;
            beat2.quality = ((i % 2) == 0) ? ChordQuality::Minor7b5 : ChordQuality::Dominant7;
            result.push_back(beat2);

            // Beat 3: 裏コードのさらに裏 (Dominant7 resolving to bII7)
            ChordBlock beat3 = computeApproachChordForTarget(
                current, tritoneSubRoot, current.startPpq + stepPpq * 2.0, noteDur, false, targetNext.targetTopNote);
            beat3.rootPitchClass = subOfSubRoot;
            result.push_back(beat3);

            // Beat 4: ターゲットへ半音下行解決する裏コード (Tritone Substitution: bII7)
            ChordBlock beat4 = computeApproachChordForTarget(
                current, targetRootPc, current.startPpq + stepPpq * 3.0, noteDur, true, targetNext.targetTopNote);
            result.push_back(beat4);
        }
        else if (mutation >= 80.0f && gridSlotPpq >= 3.4)
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
                targetNext.targetTopNote
            );
            result.push_back(preApproach);

            // 後半2（4拍目）: ターゲットへ向かう裏コード (bII7)
            ChordBlock finalApproach = computeApproachChordForTarget(
                current,
                targetRootPc,
                current.startPpq + halfPpq + quarterPpq,
                std::max(0.20, quarterPpq - kNoteGapPpq),
                true,
                targetNext.targetTopNote
            );
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
            const bool isAlreadySecondaryDom = (normalizePitchClass(current.rootPitchClass) == normalizePitchClass(targetRootPc + 7));
            const bool useTritoneSub = isAlreadySecondaryDom
                                       || profile.allowTritoneSub
                                       || (mutation >= 48.0f);

            ChordBlock passingChord = computeApproachChordForTarget(
                current,
                targetRootPc,
                secondStart,
                secondDur,
                useTritoneSub,
                targetNext.targetTopNote
            );
            result.push_back(passingChord);
        }
    }

    return result;
}

//==============================================================================
// オフライン・リハーモナイズ実行パイプライン
//==============================================================================
bool MidiReharmonizerEngine::triggerOfflineReharmonize(bool usePreviewProgressionIfEmpty)
{
    std::vector<ChordBlock> progression;
    VoicingProfile profileCopy;

    {
        const juce::SpinLock::ScopedLockType lock(sequenceLock_);
        profileCopy = activeProfile_;
        if (!capturedSequence_.empty())
        {
            progression = extractChordProgressionFromCaptured();
        }
    }

    // キャプチャされたMIDIがまだ無い場合、UI上でボイシング変化を即座に確認できるよう
    // デフォルトの2-5-1-6進行（Dm7 -> G7 -> Cmaj7 -> A7）を生成
    if (progression.empty() && usePreviewProgressionIfEmpty)
    {
        progression = {
            ChordBlock { 2, ChordQuality::Minor7,    0.0,  3.8, 88, 72 }, // Dm7
            ChordBlock { 7, ChordQuality::Dominant7, 4.0,  3.8, 92, 71 }, // G7
            ChordBlock { 0, ChordQuality::Major7,    8.0,  3.8, 86, 71 }, // Cmaj7
            ChordBlock { 9, ChordQuality::Dominant7, 12.0, 3.8, 90, 69 }  // A7
        };
    }

    if (progression.empty())
        return false;

    // Phase 13: ボイシング探索の前段階で Harmonic Rhythm を動的に分割し、
    //           直後のターゲットコードへ向かうパッシングコード（裏コード・セカンダリードミナント等）を挿入
    progression = subdivideAndInsertPassingChords(progression, profileCopy);

    // Phase 3 & 13: 分割されたパッシングコードを含む全時間ステップを Viterbi トレリスへ統合し、
    //               曲全体を通して最もボイスリーディングが滑らかな最適ボイシング経路を探索
    const auto optimalVoicings = findOptimalVoicingPath(progression, profileCopy);

    // 発音タイミング変調（ストラム／ジッター）およびベロシティ変調を適用してMIDIイベント列へ変換
    auto generatedEvents = renderVoicingPathToMidiEvents(progression, optimalVoicings, profileCopy);

    {
        const juce::SpinLock::ScopedLockType lock(sequenceLock_);
        reharmonizedSequence_ = std::move(generatedEvents);
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

    // 開始PPQの近いノート（許容誤差 0.125 PPQ = 32分音符相当）を同一の和音ブロックとしてクラスタリング
    constexpr double kChordOnsetTolerancePpq = 0.125;
    size_t i = 0;
    while (i < notes.size())
    {
        const double clusterStartPpq = notes[i].startPpq;
        double clusterEndPpq = notes[i].endPpq;
        int velocitySum = 0;
        int maxPitch = -1;
        std::vector<int> clusterPitches;

        while (i < notes.size() && (notes[i].startPpq - clusterStartPpq) <= kChordOnsetTolerancePpq)
        {
            clusterPitches.push_back(notes[i].pitch);
            clusterEndPpq = std::max(clusterEndPpq, notes[i].endPpq);
            velocitySum += notes[i].velocity;
            maxPitch = std::max(maxPitch, notes[i].pitch);
            ++i;
        }

        ChordBlock block;
        inferChordFromNotes(clusterPitches, block.rootPitchClass, block.quality);
        block.startPpq = clusterStartPpq;
        block.durationPpq = std::max(0.25, clusterEndPpq - clusterStartPpq);
        block.baseVelocity = std::clamp(velocitySum / static_cast<int>(clusterPitches.size()), 1, 127);
        if (maxPitch >= 0)
            block.targetTopNote = maxPitch;

        blocks.push_back(block);
    }

    return blocks;
}

std::vector<TimedMidiEvent> MidiReharmonizerEngine::renderVoicingPathToMidiEvents(
    std::span<const ChordBlock> progression,
    std::span<const Voicing> voicings,
    const VoicingProfile& profile) const
{
    std::vector<TimedMidiEvent> events;
    const size_t count = std::min(progression.size(), voicings.size());
    events.reserve(count * 10);

    // 決定論的シードによる乱数生成器（オフライン再実行時の再現性を担保）
    std::mt19937 rng(20261028u);
    std::uniform_real_distribution<float> jitterDist(-profile.timingJitterMs, profile.timingJitterMs);
    std::uniform_real_distribution<float> velDist(-profile.velocityHumanizeRange, profile.velocityHumanizeRange);

    // 基準テンポ120BPM相当でのミリ秒 -> PPQ変換係数 (120BPM = 1秒に2拍 = 500ms/PPQ)
    constexpr double kPpqPerMsAt120Bpm = 1.0 / 500.0;

    for (size_t step = 0; step < count; ++step)
    {
        const auto& chord = progression[step];
        const auto& voicing = voicings[step];

        const size_t numVoices = voicing.midiNotes.size();
        for (size_t v = 0; v < numVoices; ++v)
        {
            const int noteNumber = voicing.midiNotes[v];
            const bool isTopVoice = (v + 1 == numVoices);

            // ストラム遅延（下からv番目の声部に応じて発音タイミングをずらす）＋ランダムジッター
            const double strumOffsetMs = static_cast<double>(v) * static_cast<double>(profile.strumSpreadMs);
            const double jitterOffsetMs = static_cast<double>(jitterDist(rng));
            const double totalOffsetPpq = (strumOffsetMs + jitterOffsetMs) * kPpqPerMsAt120Bpm;

            const double noteOnPpq = std::max(0.0, chord.startPpq + totalOffsetPpq);
            const double noteOffPpq = std::max(noteOnPpq + 0.05, chord.startPpq + chord.durationPpq);

            // ベロシティ変調（トップノート強調＋ヒューマナイズ）
            const float boost = isTopVoice ? profile.topVoiceVelocityBoost : 0.0f;
            const int finalVelocity = std::clamp(
                static_cast<int>(std::round(static_cast<float>(chord.baseVelocity) + boost + velDist(rng))),
                1,
                127
            );

            TimedMidiEvent onEv;
            onEv.ppqPosition = noteOnPpq;
            onEv.message = juce::MidiMessage::noteOn(1, noteNumber, static_cast<juce::uint8>(finalVelocity));
            events.push_back(onEv);

            TimedMidiEvent offEv;
            offEv.ppqPosition = noteOffPpq;
            offEv.message = juce::MidiMessage::noteOff(1, noteNumber);
            events.push_back(offEv);
        }
    }

    // PPQ順（時系列順）にソート
    std::sort(events.begin(), events.end(), [](const TimedMidiEvent& a, const TimedMidiEvent& b)
    {
        return a.ppqPosition < b.ppqPosition;
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
