#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace reharm::engine
{

//==============================================================================
/**
 * @brief 希望するボイシング配置スタイル
 */
enum class VoicingStyle : uint8_t
{
    Close = 0,   ///< クローズド・ボイシング（密集配置）
    Drop2,       ///< ドロップ2（上から2番目の声を1オクターブ下げる）
    Drop3,       ///< ドロップ3（上から3番目の声を1オクターブ下げる）
    Spread       ///< スプレッド（低音域は広く、中〜高音域は適度に配置）
};

/**
 * @brief コードクオリティ種別
 */
enum class ChordQuality : uint8_t
{
    MajorTriad = 0,
    MinorTriad,
    DiminishedTriad,
    AugmentedTriad,
    Major7,
    Minor7,
    Dominant7,
    Minor7b5,
    Diminished7,
    MinorMajor7,
    Sus4,
    Dominant7Sus4
};

//==============================================================================
// 1. 3層レイヤーのプロファイル構造体定義 (Genre / Artist / Environment)
//==============================================================================

/**
 * @brief Layer 1: ジャンル固有の音楽理論・和声ルール
 */
struct GenreRules
{
    bool allowTritoneSub { false };                 ///< 裏コード（トライトーン代理）の許容
    bool allowTensions { true };                    ///< テンションノート（9th等）の付加許容
    VoicingStyle preferredStyle { VoicingStyle::Drop2 }; ///< ジャンルで好まれる基本ボイシング

    float omitRootPenalty { 12.0f };                ///< ルート音を省略した際のペナルティ（ジャズのルートレスなら低く設定）
    float guideToneMissingPenalty { 35.0f };        ///< 3rd または 7th（ガイドトーン）が欠落した際のペナルティ
    float minorSecondPenalty { 50.0f };             ///< 隣接声部間の半音（短2度/短9度）ぶつかりペナルティ
    float styleMismatchPenalty { 10.0f };           ///< preferredStyle（Drop2など）から外れた際のペナルティ
};

/**
 * @brief Layer 2: アーティスト固有の演奏癖・ボイシング美学ルール
 */
struct ArtistRules
{
    float spreadTargetSemitones { 16.0f };           ///< ベース音〜トップ音までの理想的な広がり（半音単位、16 = 長10度）
    float spreadDeviationWeight { 1.5f };           ///< 理想の広がりからの乖離に対する重み
    float voiceLeadingWeight { 2.0f };              ///< 前のコードからの各声部の移動距離に対する重み
    float topNoteLeapWeight { 3.0f };               ///< トップノート（最高音）の跳躍に対する重み（メロディックな繋がり重視）

    float timingJitterMs { 4.0f };                  ///< 発音タイミングのランダムな揺らぎ（ミリ秒）
    float strumSpreadMs { 8.0f };                   ///< 下の声部から上の声部へアルペジオ状に発音するストラム遅延（ミリ秒）
    float velocityHumanizeRange { 8.0f };           ///< ベロシティのランダム変調幅
    float topVoiceVelocityBoost { 10.0f };          ///< トップノートを際立たせるベロシティ加算量
};

/**
 * @brief Layer 3: 再生環境・楽器の物理的制約ルール
 */
struct EnvironmentRules
{
    int minPitch { 40 };                            ///< 使用可能な最低MIDIノート番号（デフォルト: 40 = E2）
    int maxPitch { 84 };                            ///< 使用可能な最高MIDIノート番号（デフォルト: 84 = C6）
    int minVoices { 3 };                            ///< 最小構成音数（ハードリミット）
    int maxVoices { 5 };                            ///< 最大構成音数（ハードリミット）

    int maxAdjacentInterval { 14 };                 ///< 隣接する上声部間の最大インターバル（半音単位、14 = 長9度）
    int maxBassToTenorInterval { 19 };              ///< 最低音（Bass）と第2音（Tenor）間の最大インターバル（19 = 完全12度）
    int maxTotalSpan { 28 };                        ///< ボイシング全体の最大スパン（ハードリミット）

    int lowIntervalLimitPitch { 53 };               ///< Low Interval Limit の基準ピッチ（53 = F3。これ未満での密集を制限）
    int lowIntervalMinSemitones { 5 };              ///< 低音域で許容される最小インターバル（5 = 完全4度未満にペナルティ）
    float lowIntervalPenaltyWeight { 8.0f };        ///< Low Interval Limit 違反時のペナルティ係数
};

/**
 * @brief 3層レイヤー（Genre / Artist / Environment）を合成した最終評価・変調プロファイル
 */
struct VoicingProfile
{
    // ハードリミット（枝刈り用）
    int minPitch { 40 };
    int maxPitch { 84 };
    int minVoices { 3 };
    int maxVoices { 5 };
    int maxAdjacentInterval { 14 };
    int maxBassToTenorInterval { 19 };
    int maxTotalSpan { 28 };

    // 和声・スタイル設定
    bool allowTritoneSub { false };
    bool allowTensions { true };
    VoicingStyle preferredStyle { VoicingStyle::Drop2 };

    // 静的コスト重み
    float omitRootPenalty { 12.0f };
    float guideToneMissingPenalty { 35.0f };
    float minorSecondPenalty { 50.0f };
    float styleMismatchPenalty { 10.0f };
    float spreadTargetSemitones { 16.0f };
    float spreadDeviationWeight { 1.5f };

    // Low Interval Limit 重み
    int lowIntervalLimitPitch { 53 };
    int lowIntervalMinSemitones { 5 };
    float lowIntervalPenaltyWeight { 8.0f };

    // 動的（遷移）コスト重み
    float voiceLeadingWeight { 2.0f };
    float topNoteLeapWeight { 3.0f };

    // 発音タイミング・ベロシティ変調パラメーター
    float timingJitterMs { 4.0f };
    float strumSpreadMs { 8.0f };
    float velocityHumanizeRange { 8.0f };
    float topVoiceVelocityBoost { 10.0f };

    // Phase 8: Mutation（突然変異・カオス度）パラメーター (0.0f 〜 100.0f, Default: 50.0f)
    float mutationAmount { 50.0f };
};

/**
 * @brief 3層レイヤーの構造体を合成し、妥当な範囲にクランプした `VoicingProfile` を生成するファクトリ関数
 */
[[nodiscard]] inline VoicingProfile synthesizeVoicingProfile(
    const GenreRules& genre,
    const ArtistRules& artist,
    const EnvironmentRules& env) noexcept
{
    VoicingProfile profile;

    // 1. Environment (物理制約・ハードリミット: 安全のため3〜5音の範囲等を保証)
    profile.minPitch = std::clamp(env.minPitch, 12, 108);
    profile.maxPitch = std::clamp(env.maxPitch, profile.minPitch + 12, 120);
    profile.minVoices = std::clamp(env.minVoices, 3, 5);
    profile.maxVoices = std::clamp(env.maxVoices, profile.minVoices, 5);
    profile.maxAdjacentInterval = std::clamp(env.maxAdjacentInterval, 5, 24);
    profile.maxBassToTenorInterval = std::clamp(env.maxBassToTenorInterval, profile.maxAdjacentInterval, 28);
    profile.maxTotalSpan = std::clamp(env.maxTotalSpan, 12, 36);

    profile.lowIntervalLimitPitch = env.lowIntervalLimitPitch;
    profile.lowIntervalMinSemitones = std::max(1, env.lowIntervalMinSemitones);
    profile.lowIntervalPenaltyWeight = std::max(0.0f, env.lowIntervalPenaltyWeight);

    // 2. Genre (音楽理論・静的ペナルティ)
    profile.allowTritoneSub = genre.allowTritoneSub;
    profile.allowTensions = genre.allowTensions;
    profile.preferredStyle = genre.preferredStyle;
    profile.omitRootPenalty = std::max(0.0f, genre.omitRootPenalty);
    profile.guideToneMissingPenalty = std::max(0.0f, genre.guideToneMissingPenalty);
    profile.minorSecondPenalty = std::max(0.0f, genre.minorSecondPenalty);
    profile.styleMismatchPenalty = std::max(0.0f, genre.styleMismatchPenalty);

    // 3. Artist (美学・ボイスリーディング・ヒューマナイズ)
    profile.spreadTargetSemitones = std::clamp(
        artist.spreadTargetSemitones,
        6.0f,
        static_cast<float>(profile.maxTotalSpan)
    );
    profile.spreadDeviationWeight = std::max(0.0f, artist.spreadDeviationWeight);
    profile.voiceLeadingWeight = std::max(0.0f, artist.voiceLeadingWeight);
    profile.topNoteLeapWeight = std::max(0.0f, artist.topNoteLeapWeight);

    profile.timingJitterMs = std::max(0.0f, artist.timingJitterMs);
    profile.strumSpreadMs = artist.strumSpreadMs;
    profile.velocityHumanizeRange = std::clamp(artist.velocityHumanizeRange, 0.0f, 40.0f);
    profile.topVoiceVelocityBoost = std::clamp(artist.topVoiceVelocityBoost, -30.0f, 30.0f);

    return profile;
}

//==============================================================================
// 2. ボイシングおよびコード進行のデータ構造
//==============================================================================

/**
 * @brief 単一コードに対する具体的なボイシング（昇順ソートされたMIDIノート番号列）
 */
struct Voicing
{
    std::vector<int> midiNotes; ///< 昇順（Bass -> Top）で格納されたMIDIノート番号配列

    /**
     * @brief 構成音が空かどうかを返します
     */
    [[nodiscard]] bool isEmpty() const noexcept
    {
        return midiNotes.empty();
    }

    /**
     * @brief 声部（構成音）の数を返します
     */
    [[nodiscard]] size_t getNumVoices() const noexcept
    {
        return midiNotes.size();
    }

    /**
     * @brief ベースノート（最低音）のMIDIノート番号を取得します（空の場合は -1）
     */
    [[nodiscard]] int getBassNote() const noexcept
    {
        return midiNotes.empty() ? -1 : midiNotes.front();
    }

    /**
     * @brief トップノート（最高音）のMIDIノート番号を取得します（空の場合は -1）
     */
    [[nodiscard]] int getTopNote() const noexcept
    {
        return midiNotes.empty() ? -1 : midiNotes.back();
    }

    /**
     * @brief ボイシング全体の広がり（トップノート - ベースノート）を半音単位で返します
     */
    [[nodiscard]] int getSpread() const noexcept
    {
        if (midiNotes.size() < 2)
            return 0;
        return midiNotes.back() - midiNotes.front();
    }

    /**
     * @brief このボイシングが Drop 2 配置の特徴を満たしているか判定します
     *
     * Drop 2の典型特徴: 4声以上において、下から1番目と2番目の間隔（または2番目と3番目の間隔）が
     * 5半音（完全4度）以上開き、上声部が比較的密集している配置。
     */
    [[nodiscard]] bool isDrop2Like() const noexcept
    {
        if (midiNotes.size() < 4)
            return false;

        const size_t n = midiNotes.size();
        const int droppedGap = midiNotes[n - 3] - midiNotes[n - 4]; // Dropされた声部とその上の声部の間隔
        const int upperGap1  = midiNotes[n - 2] - midiNotes[n - 3];
        const int upperGap2  = midiNotes[n - 1] - midiNotes[n - 2];

        return (droppedGap >= 5 && droppedGap <= 10)
            && (upperGap1 >= 2 && upperGap1 <= 6)
            && (upperGap2 >= 2 && upperGap2 <= 6);
    }
};

/**
 * @brief タイムライン上の1つのコード区間を表す構造体（ビタビ探索の1時間ステップに対応）
 */
struct ChordBlock
{
    int rootPitchClass { 0 };                       ///< ルート音のピッチクラス (0 = C, 1 = C#, ..., 11 = B)
    ChordQuality quality { ChordQuality::Major7 };  ///< コードクオリティ
    double startPpq { 0.0 };                        ///< 開始位置（PPQ）
    double durationPpq { 4.0 };                     ///< 長さ（PPQ）
    int baseVelocity { 90 };                        ///< 基準ベロシティ (1 - 127)
    std::optional<int> targetTopNote;               ///< 元のトップノート（メロディ維持の参照用、任意）
};

} // namespace reharm::engine
