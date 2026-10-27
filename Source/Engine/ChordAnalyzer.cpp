#include "ChordAnalyzer.h"

#include <algorithm>
#include <array>

namespace reharm::engine
{

namespace
{
    [[nodiscard]] constexpr int normalizePc(int noteOrPc) noexcept
    {
        const int r = noteOrPc % 12;
        return r < 0 ? r + 12 : r;
    }

    struct CandidateEvaluation
    {
        int rootPc { 0 };
        float score { -1000.0f };
        juce::String qualityAndTensions;
    };

    /**
     * @brief 特定の候補ルート `candidateRootPc` に対して Pitch Class Set を評価し、
     *        クオリティ＋テンション文字列と適合スコアを算出する
     */
    [[nodiscard]] CandidateEvaluation evaluateRootCandidate(
        int candidateRootPc,
        int bassPc,
        const std::array<bool, 12>& pcSet)
    {
        CandidateEvaluation eval;
        eval.rootPc = candidateRootPc;

        // 候補ルートからの相対インターバル有無を取得
        std::array<bool, 12> iv {};
        int matchedNotes = 0;
        for (int pc = 0; pc < 12; ++pc)
        {
            if (pcSet[static_cast<size_t>(pc)])
            {
                iv[static_cast<size_t>(normalizePc(pc - candidateRootPc))] = true;
                ++matchedNotes;
            }
        }

        const bool hasMin2 = iv[1];  // b9
        const bool hasMaj2 = iv[2];  // 9
        const bool hasMin3 = iv[3];  // m3 / #9
        const bool hasMaj3 = iv[4];  // M3
        const bool hasP4   = iv[5];  // 4 / 11
        const bool hasDim5 = iv[6];  // b5 / #11
        const bool hasP5   = iv[7];  // 5
        const bool hasAug5 = iv[8];  // #5 / b13
        const bool hasMaj6 = iv[9];  // 6 / 13 / dim7
        const bool hasMin7 = iv[10]; // b7
        const bool hasMaj7 = iv[11]; // M7

        float score = 0.0f;

        // ベース音とルート音が一致している場合は強いボーナス
        if (candidateRootPc == bassPc)
            score += 25.0f;

        // 完全5度が含まれていればルートらしさが高い
        if (hasP5)
            score += 10.0f;

        juce::String baseQuality;
        bool isSeventhChord = false;
        bool isMinor = false;
        bool isDominant = false;
        bool isMajor7 = false;
        bool isHalfDim = false;
        bool isDim7 = false;
        bool isSus4 = false;

        // 1. 基本コードクオリティの判定
        if (hasMin3 && hasDim5 && hasMaj6 && !hasMin7 && !hasMaj7 && !hasP5)
        {
            isDim7 = true;
            baseQuality = "dim7";
            score += 32.0f;
        }
        else if (hasMin3 && hasDim5 && hasMin7 && !hasP5)
        {
            isHalfDim = true;
            isSeventhChord = true;
            baseQuality = "m7(b5)";
            score += 34.0f;
        }
        else if (hasMaj3 && hasMaj7)
        {
            isMajor7 = true;
            isSeventhChord = true;
            baseQuality = "maj7";
            score += 35.0f;
        }
        else if (hasMaj3 && hasMin7)
        {
            isDominant = true;
            isSeventhChord = true;
            baseQuality = "7";
            score += 35.0f;
        }
        else if (hasMin3 && hasMin7)
        {
            isMinor = true;
            isSeventhChord = true;
            baseQuality = "m7";
            score += 34.0f;
        }
        else if (hasMin3 && hasMaj7)
        {
            isMinor = true;
            isSeventhChord = true;
            baseQuality = "mM7";
            score += 30.0f;
        }
        else if (!hasMaj3 && !hasMin3 && hasP4 && hasMin7)
        {
            isSus4 = true;
            isDominant = true;
            isSeventhChord = true;
            baseQuality = "7sus4";
            score += 28.0f;
        }
        else if (!hasMaj3 && !hasMin3 && hasP4)
        {
            isSus4 = true;
            baseQuality = "sus4";
            score += 22.0f;
        }
        else if (hasMaj3 && hasAug5 && !hasP5)
        {
            baseQuality = "aug";
            score += 24.0f;
        }
        else if (hasMin3 && hasDim5 && !hasP5)
        {
            baseQuality = "dim";
            score += 22.0f;
        }
        else if (hasMaj3)
        {
            baseQuality = ""; // Major Triad
            score += 20.0f;
        }
        else if (hasMin3)
        {
            isMinor = true;
            baseQuality = "m"; // Minor Triad
            score += 20.0f;
        }
        else
        {
            // 3度もsus4も無い場合はコードとしての妥当性が低い
            score -= 20.0f;
            baseQuality = "5";
        }

        // 2. テンション（9, 11, 13）の判定と表記統合
        juce::StringArray tensions;

        // 9th 系
        if (hasMaj2)
        {
            score += 6.0f;
            if (isMajor7)
                baseQuality = "maj9";
            else if (isDominant && !isSus4)
                baseQuality = "9";
            else if (isMinor && isSeventhChord && baseQuality == "m7")
                baseQuality = "m9";
            else if (!isSeventhChord && (baseQuality.isEmpty() || baseQuality == "m"))
                tensions.add("add9");
            else
                tensions.add("9");
        }
        else if (hasMin2)
        {
            score += isDominant ? 6.0f : -4.0f;
            tensions.add("b9");
        }

        // ドミナント時の #9 (Maj3 と Min3 が共存する場合)
        if (hasMaj3 && hasMin3 && isDominant)
        {
            score += 5.0f;
            tensions.add("#9");
        }

        // 11th 系
        if (hasP4 && !isSus4)
        {
            if (isMinor)
            {
                score += 6.0f;
                tensions.add("11");
            }
            else
            {
                // メジャー系でのナチュラル11thはアボイド傾向のためスコア微減
                score -= 2.0f;
                tensions.add("11");
            }
        }
        if (hasDim5 && !isHalfDim && !isDim7 && baseQuality != "dim")
        {
            score += 6.0f;
            tensions.add("#11");
        }

        // 13th / 6th 系
        if (hasMaj6 && !isDim7)
        {
            score += 6.0f;
            if (isSeventhChord)
            {
                if (isDominant && (baseQuality == "7" || baseQuality == "9") && tensions.isEmpty())
                    baseQuality = "13";
                else
                    tensions.add("13");
            }
            else if (!isSus4)
            {
                baseQuality += "6";
            }
        }
        else if (hasAug5 && baseQuality != "aug")
        {
            score += isDominant ? 5.0f : 1.0f;
            tensions.add("b13");
        }

        juce::String result = baseQuality;
        if (!tensions.isEmpty())
        {
            result += "(" + tensions.joinIntoString(",") + ")";
        }

        juce::ignoreUnused(matchedNotes);
        eval.score = score;
        eval.qualityAndTensions = result;
        return eval;
    }
} // anonymous namespace

juce::String ChordAnalyzer::pitchClassToName(int pitchClass)
{
    static constexpr const char* kNoteNames[12] = {
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
    };
    return kNoteNames[normalizePc(pitchClass)];
}

juce::String ChordAnalyzer::analyzeChordName(const std::vector<int>& pitches)
{
    if (pitches.empty())
        return "N.C.";

    std::vector<int> sorted = pitches;
    std::sort(sorted.begin(), sorted.end());

    const int bassPc = normalizePc(sorted.front());

    std::array<bool, 12> pcSet {};
    std::vector<int> uniquePcs;
    uniquePcs.reserve(12);

    for (int note : sorted)
    {
        const int pc = normalizePc(note);
        if (!pcSet[static_cast<size_t>(pc)])
        {
            pcSet[static_cast<size_t>(pc)] = true;
            uniquePcs.push_back(pc);
        }
    }

    if (uniquePcs.size() == 1)
        return pitchClassToName(bassPc);

    // 構成音に含まれる各ピッチクラスをルート候補として採点し、最高スコアの解釈を採用
    CandidateEvaluation bestEval = evaluateRootCandidate(bassPc, bassPc, pcSet);

    for (int candidatePc : uniquePcs)
    {
        if (candidatePc == bassPc)
            continue;

        const auto currentEval = evaluateRootCandidate(candidatePc, bassPc, pcSet);
        if (currentEval.score > bestEval.score)
        {
            bestEval = currentEval;
        }
    }

    juce::String chordSymbol = pitchClassToName(bestEval.rootPc) + bestEval.qualityAndTensions;

    // ベース音がルートと異なる場合はオンコード（Slash Chord: 例 "Bb/C"）として表記
    if (bestEval.rootPc != bassPc)
    {
        chordSymbol += "/" + pitchClassToName(bassPc);
    }

    return chordSymbol;
}

} // namespace reharm::engine
