#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <span>
#include <vector>

#include "VoicingDataTypes.h"

namespace reharm::engine
{

/**
 * @brief オフライン解析・再生用のタイムスタンプ付きMIDIイベント構造体
 *        DAWのPPQ（四分音符単位のミュージカルタイム）とサンプル位置の両方を保持します。
 */
struct TimedMidiEvent
{
    double ppqPosition { 0.0 };
    int64_t samplePosition { 0 };
    juce::MidiMessage message;
};

/**
 * @brief エンジンの動作モード
 */
enum class OperatingMode : int
{
    Bypass = 0,          ///< 入力MIDIをそのままスルー出力
    Capture = 1,         ///< DAWからの入力MIDIを内部バッファへ蓄積（スルー出力も維持）
    PlaybackReharm = 2   ///< 解析・再構築済みのMIDIデータをDAWタイムラインに同期して出力
};

/**
 * @brief MIDIリハーモナイズ処理のコアエンジン（UIおよびAudioProcessorから分離）
 *
 * 3層レイヤープロファイル（Genre / Artist / Environment）に基づき、
 * 和音候補の枝刈り生成・コスト採点・ビタビアルゴリズム（動的計画法）による
 * 最適ボイシング経路探索を実行します。
 */
class MidiReharmonizerEngine
{
public:
    MidiReharmonizerEngine();
    ~MidiReharmonizerEngine() = default;

    // コピー禁止・ムーブ禁止
    MidiReharmonizerEngine(const MidiReharmonizerEngine&) = delete;
    MidiReharmonizerEngine& operator=(const MidiReharmonizerEngine&) = delete;

    //==============================================================================
    // ライフサイクル管理
    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    //==============================================================================
    // オーディオ／MIDIスレッド処理
    void processMidiBlock(juce::MidiBuffer& midiMessages,
                          const juce::Optional<juce::AudioPlayHead::PositionInfo>& posInfo,
                          int numSamples);

    /**
     * @brief Phase 12: Audition（内部プレビューシンセ）用に、現在のブロック範囲に対応する
     *        リハーモナイズ済みMIDIイベントを4小節ループ（16.0 PPQ周期）で抽出します。
     */
    void extractAuditionMidiForBlock(juce::MidiBuffer& outAuditionMidi,
                                     double loopStartPpq,
                                     double blockDurationPpq,
                                     double ppqPerSample,
                                     int numSamples) const noexcept;

    //==============================================================================
    // オフライン操作・モード制御API
    void setOperatingMode(OperatingMode newMode) noexcept;
    [[nodiscard]] OperatingMode getOperatingMode() const noexcept;

    void clearCapturedData();

    /**
     * @brief 外部（D&Dインポートされた .mid ファイル等）から読み込んだ `juce::MidiMessageSequence` を
     *        リハーモナイズの入力元データ（ベースコード進行）としてセットします。
     * @param sequence             マージ済みの入力MIDIシーケンス
     * @param ticksPerQuarterNote  入力MIDIファイルのタイムベース（TPQN）
     */
    void setSourceMidiSequence(const juce::MidiMessageSequence& sequence,
                               double ticksPerQuarterNote = 960.0);

    /**
     * @brief 3層レイヤーのルールを設定し、内部の `VoicingProfile` を更新します。
     */
    void updateRules(const GenreRules& genre,
                     const ArtistRules& artist,
                     const EnvironmentRules& env);

    /**
     * @brief 現在適用されている合成済み `VoicingProfile` を取得します。
     */
    [[nodiscard]] VoicingProfile getCurrentProfile() const noexcept;

    /**
     * @brief 3層レイヤーのプリセットインデックスからルールを構築して更新します。
     */
    void updateRulesFromPresets(int genreIndex, int artistIndex, int environmentIndex);

    /**
     * @brief Mutation（突然変異）量（0.0f 〜 100.0f）を設定し、コスト関数およびテンション許容度を変調します。
     */
    void setMutationAmount(float mutationPercent) noexcept;

    /**
     * @brief 現在の Mutation 量（0.0f 〜 100.0f）を取得します。
     */
    [[nodiscard]] float getMutationAmount() const noexcept;

    //==============================================================================
    // Phase 9: Audio Groove Sync (サイドチェイン・トランジェント抽出＆動的クオンタイズ)
    //==============================================================================

    /**
     * @brief Audio Groove Sync の有効／無効を切り替えます。
     */
    void setGrooveSyncEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isGrooveSyncEnabled() const noexcept;

    /**
     * @brief オーディオスレッド（processBlock）で検出されたトランジェントのPPQ位置を
     *        内部の GrooveMap リングバッファへ記録します。
     */
    void recordTransientPpq(double ppqPosition) noexcept;

    /**
     * @brief 外部から GrooveMap（PPQ位置の配列）を直接セットします。
     */
    void setGrooveMap(std::span<const double> groovePpqPositions);

    /**
     * @brief 蓄積された GrooveMap をクリアします。
     */
    void clearGrooveMap();

    /**
     * @brief 生成済みの `juce::MidiMessageSequence` の各ノート発音タイミング（Note On）を、
     *        `grooveMapPpq` 内の最も近いトランジェント位置へ吸着（シフト）させます。
     *        ※環境レイヤーのヒューマナイズ処理よりも後に適用され、オーディオのノリで最優先上書きします。
     */
    void applyGrooveMapToSequence(juce::MidiMessageSequence& sequence,
                                  std::span<const double> grooveMapPpq,
                                  double ticksPerQuarterNote = 960.0) const;

    /**
     * @brief 現在のリハーモナイズ結果（解析済みMIDIシーケンス）を
     *        PianoRoll描画等に適した `juce::MidiMessageSequence` 形式で取得します。
     *        （入力が空の場合はプレビュー用のデフォルトコード進行から生成可能）
     */
    [[nodiscard]] juce::MidiMessageSequence getReharmonizedMidiSequence(double ticksPerQuarterNote = 960.0) const;

    /**
     * @brief 指定された構成音（MIDIノート番号配列）からコードネーム文字列を逆解析して返します。
     */
    [[nodiscard]] static juce::String analyzeChordName(const std::vector<int>& pitches);

    /**
     * @brief 蓄積されたMIDIデータに対してオフライン・リハーモナイズ変換を実行し、
     *        ビタビアルゴリズムで最適ボイシング経路を探索して出力用バッファを構築します。
     * @return 変換に成功した場合は true
     */
    bool triggerOfflineReharmonize(bool usePreviewProgressionIfEmpty = true);

    //==============================================================================
    // Phase 3: ボイシング生成・採点・ビタビ経路探索コアアルゴリズム（単体テスト・外部呼び出し可能）
    //==============================================================================

    /**
     * @brief 1. 和音候補の生成と枝刈り (Candidate Generation & Pruning)
     *
     * 指定されたコードブロックとプロファイルの音域制限（minPitch〜maxPitch）に基づき、
     * 物理的に妥当な3〜5音のボイシング候補群をバックトラッキング＋枝刈りで生成します。
     */
    [[nodiscard]] std::vector<Voicing> generateCandidateVoicings(
        const ChordBlock& chord,
        const VoicingProfile& profile) const;

    /**
     * @brief 2. コスト関数 (Scoring / Cost Function)
     *
     * 前のボイシング（先頭コードの場合は nullptr）と現在のボイシング、対象コード、
     * および `VoicingProfile` から静的コスト＋動的（ボイスリーディング）コストの合計を返します。
     */
    [[nodiscard]] float evaluateVoicingCost(
        const Voicing* prevVoicing,
        const Voicing& currentVoicing,
        const ChordBlock& chord,
        const VoicingProfile& profile) const noexcept;

    /**
     * @brief 静的コスト単体の計算（半音のぶつかり、Low Interval Limit違反、ルート/ガイドトーン欠落など）
     */
    [[nodiscard]] float calculateStaticCost(
        const Voicing& currentVoicing,
        const ChordBlock& chord,
        const VoicingProfile& profile) const noexcept;

    /**
     * @brief 動的コスト単体の計算（前のボイシングからの各声部移動距離・トップノート跳躍ペナルティ）
     */
    [[nodiscard]] float calculateTransitionCost(
        const Voicing& prevVoicing,
        const Voicing& currentVoicing,
        const VoicingProfile& profile) const noexcept;

    /**
     * @brief 3. 経路探索 (Viterbi Algorithm)
     *
     * コード進行全体を通して、トレリス（格子）構造上の動的計画法により
     * 合計コストが最小となる最適ボイシングの系列を探索して返します。
     */
    [[nodiscard]] std::vector<Voicing> findOptimalVoicingPath(
        std::span<const ChordBlock> progression,
        const VoicingProfile& profile) const;

    //==============================================================================
    // Phase 13: Harmonic Rhythm（和声リズム）の動的分割とパッシングコード自動挿入
    //==============================================================================

    /**
     * @brief 長さが2拍（2.0 PPQ = ハーフノート）を超える長いコードブロックを検出し、
     *        `profile.mutationAmount`（0.0〜100.0）に応じて拍を分割してアプローチコード（裏コード・セカンダリードミナント等）を自動挿入します。
     *
     * - 0%〜30%: 分割を行わず、入力された和声リズムをそのまま維持
     * - 30%〜70% (50%付近): 4小節に1〜2回程度の頻度で、ドミナント解決前やターンアラウンド等に裏コードを控えめに挿入
     * - 70%〜100% (Chaos): 可能な限りすべての白玉コードを分割し、100%付近では連続アプローチコード（裏コードのさらに裏など）を挿入してバップ風に細分化
     */
    [[nodiscard]] std::vector<ChordBlock> subdivideAndInsertPassingChords(
        std::span<const ChordBlock> baseProgression,
        const VoicingProfile& profile) const;

    /**
     * @brief 直後に来るターゲットコードのルート音（`targetRootPitchClass`）へ向かうアプローチコード（裏コードまたはセカンダリードミナント）を生成します。
     * @param sourceBlock           分割元のコードブロック
     * @param targetRootPitchClass  解決先（直後のコード）のルートピッチクラス (0〜11)
     * @param startPpq              パッシングコードの開始PPQ位置
     * @param durationPpq           パッシングコードの長さ（PPQ）
     * @param useTritoneSubstitution true の場合は裏コード（半音上の7th）、false の場合はセカンダリードミナント（完全5度上の7th）
     * @param nextTargetTopNote     直後のコードのトップノート（滑らかなトップノート接続用、任意）
     */
    [[nodiscard]] static ChordBlock computeApproachChordForTarget(
        const ChordBlock& sourceBlock,
        int targetRootPitchClass,
        double startPpq,
        double durationPpq,
        bool useTritoneSubstitution,
        std::optional<int> nextTargetTopNote = std::nullopt) noexcept;

private:
    //==============================================================================
    // 内部ヘルパーメソッド
    void captureIncomingMidi(const juce::MidiBuffer& incomingBuffer,
                             const juce::Optional<juce::AudioPlayHead::PositionInfo>& posInfo);

    void injectReharmonizedMidi(juce::MidiBuffer& targetBuffer,
                                const juce::Optional<juce::AudioPlayHead::PositionInfo>& posInfo,
                                int numSamples);

    /**
     * @brief キャプチャされた生MIDIイベント列から和音区間（ChordBlock配列）を抽出します。
     */
    [[nodiscard]] std::vector<ChordBlock> extractChordProgressionFromCaptured() const;

    /**
     * @brief 決定したボイシング配列を、タイミング揺らぎ・ストラム・ベロシティ変調付きで
     *        `TimedMidiEvent` 配列に変換します。
     */
    [[nodiscard]] std::vector<TimedMidiEvent> renderVoicingPathToMidiEvents(
        std::span<const ChordBlock> progression,
        std::span<const Voicing> voicings,
        const VoicingProfile& profile) const;

    /**
     * @brief ヒューマナイズ適用後の `TimedMidiEvent` 配列に対し、GrooveMap への吸着処理を適用します。
     */
    void quantizeEventsToGrooveMap(std::vector<TimedMidiEvent>& events,
                                   std::span<const double> grooveMapPpq) const;

    //==============================================================================
    double currentSampleRate_ { 44100.0 };
    int currentMaxBlockSize_ { 512 };

    std::atomic<OperatingMode> mode_ { OperatingMode::Bypass };

    // 3層レイヤーから合成された現在のプロファイル
    VoicingProfile activeProfile_;
    std::atomic<float> mutationAmount_ { 50.0f };
    std::atomic<bool> grooveSyncEnabled_ { false };

    // Phase 9: 直近数小節分のトランジェント発音位置（PPQ）を保持する GrooveMap リングバッファ
    static constexpr size_t kMaxGrooveMapSize = 128;
    std::vector<double> grooveMapPpq_;
    size_t grooveWriteIndex_ { 0 };
    bool hasLiveAudioTransients_ { false };

    // 入力キャプチャ用バッファ（Captureモード時に蓄積）
    std::vector<TimedMidiEvent> capturedSequence_;

    // オフライン変換後の出力用シーケンス（PlaybackReharmモード時に参照）
    std::vector<TimedMidiEvent> reharmonizedSequence_;

    // オフライン変換とオーディオスレッドの競合を防ぐ軽量ロック
    mutable juce::SpinLock sequenceLock_;
};

} // namespace reharm::engine
