#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

#include "VoicingDataTypes.h"

namespace reharm::engine
{

/**
 * @brief 4つのジャンルを代表する定番コード進行テンプレート・バンク
 */
enum class TemplateId : int
{
    Jazz2516 = 0,          ///< 1. Jazz: 2-5-1-6 (Dm7 - G7 - Cmaj7 - A7)
    CityPopJustTheTwoOfUs, ///< 2. City Pop: Just The Two of Us (Dbmaj7 - C7 - Fm7 - Ebm7 Ab7)
    NeoSoul4321,           ///< 3. Neo-Soul: 4-3-2-1 (Fmaj7 - Em7 - Dm7 - Cmaj7)
    CinematicPedalSus4     ///< 4. Cinematic: ペダルポイント上のsus4展開 (Csus4 - C - Bbsus4/C - Bb/C)
};

class ChordTemplateBank
{
public:
    ChordTemplateBank() = delete;

    /**
     * @brief UIのComboBoxに表示するテンプレート名リストを返します。
     */
    [[nodiscard]] static juce::StringArray getTemplateNames();

    /**
     * @brief 指定されたテンプレートIDに対応する4小節分の基本和音 `juce::MidiMessageSequence` を生成します。
     * @param templateId          選択されたテンプレートID
     * @param ticksPerQuarterNote タイムベース（デフォルト: 960 TPQN）
     */
    [[nodiscard]] static juce::MidiMessageSequence createTemplateSequence(
        TemplateId templateId,
        double ticksPerQuarterNote = 960.0);
};

} // namespace reharm::engine
