#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace reharm::engine
{

/**
 * @brief MIDIノート配列（ボイシング）からコードネームを逆解析するクラス
 *
 * 最低音（ベース）および構成音のインターバル（Pitch Class Set）を分析し、
 * 基本クオリティ（Maj, m, 7, maj7, m7, m7b5, dim7, aug, sus4）および
 * テンション（b9, 9, #9, 11, #11, b13, 13）を判定してコードシンボル文字列を返します。
 */
class ChordAnalyzer
{
public:
    ChordAnalyzer() = delete;

    /**
     * @brief 指定されたMIDIノート番号の配列からコードネーム文字列を解析して返します。
     * @param pitches 同時に発音している構成音のMIDIノート番号配列
     * @return 解析されたコードネーム（例: "Dm9", "Dbmaj9", "G7(#11)", "Csus4"）
     */
    [[nodiscard]] static juce::String analyzeChordName(const std::vector<int>& pitches);

    /**
     * @brief ピッチクラス（0〜11）を音名文字列（"C", "Db", "D", ...）に変換します。
     */
    [[nodiscard]] static juce::String pitchClassToName(int pitchClass);
};

} // namespace reharm::engine
