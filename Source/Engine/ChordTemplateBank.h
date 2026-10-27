#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

#include "VoicingDataTypes.h"

namespace reharm::engine
{

/**
 * @brief Phase 12: 8ジャンル × 各4種 = 計32種類の定番コード進行プリセットバンク
 *
 * 対応ジャンル (0〜7):
 *   0: Jazz
 *   1: Neo-Soul
 *   2: City Pop
 *   3: Cinematic
 *   4: Lo-Fi
 *   5: Modern R&B
 *   6: Gospel
 *   7: 8-Bit (Chiptune)
 */
enum class TemplateId : int
{
    Jazz2516 = 0,
    CityPopJustTheTwoOfUs,
    NeoSoul4321,
    CinematicPedalSus4
};

class ChordTemplateBank
{
public:
    static constexpr int kNumGenres = 8;
    static constexpr int kNumPresetsPerGenre = 4;
    static constexpr int kTotalPresets = kNumGenres * kNumPresetsPerGenre;

    ChordTemplateBank() = delete;

    /**
     * @brief 8ジャンルの名称一覧を返します。
     */
    [[nodiscard]] static juce::StringArray getGenreNames();

    /**
     * @brief 指定されたジャンルインデックス（0〜7）に属する4種類のコード進行プリセット名リストを返します。
     */
    [[nodiscard]] static juce::StringArray getProgressionNamesForGenre(int genreIndex);

    /**
     * @brief 指定されたジャンルインデックス（0〜7）と進行インデックス（0〜3）に対応する
     *        4小節分（または8小節分）の基本和音 `juce::MidiMessageSequence` を生成します。
     * @param genreIndex          ジャンルインデックス (0〜7)
     * @param progressionIndex    進行プリセットインデックス (0〜3)
     * @param isEightBars         true の場合は後半（5〜8小節）の展開・ターンアラウンドを含む8小節版を生成
     * @param ticksPerQuarterNote タイムベース（デフォルト: 960 TPQN）
     */
    [[nodiscard]] static juce::MidiMessageSequence createPresetSequence(
        int genreIndex,
        int progressionIndex,
        bool isEightBars,
        double ticksPerQuarterNote = 960.0);

    /**
     * @brief 4小節版を生成するオーバーロード（下位互換用）
     */
    [[nodiscard]] static juce::MidiMessageSequence createPresetSequence(
        int genreIndex,
        int progressionIndex,
        double ticksPerQuarterNote = 960.0);

    // 下位互換用API
    [[nodiscard]] static juce::StringArray getTemplateNames();
    [[nodiscard]] static juce::MidiMessageSequence createTemplateSequence(
        TemplateId templateId,
        double ticksPerQuarterNote = 960.0);
};

} // namespace reharm::engine
