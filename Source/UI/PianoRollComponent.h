#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "../Engine/VoicingDataTypes.h"

namespace reharm::ui
{

/**
 * @brief Piano Roll Viewer & MIDI Drag-and-Drop Importer / Exporter
 *
 * - 横軸: 時間（Tick）、縦軸: ピッチ（MIDIノート番号）でボイシングを描画
 * - 外部からの `.mid` / `.midi` ファイルのドラッグ＆ドロップを受け取り、親エディタへ転送
 * - 右上の「DRAG MIDI TO DAW」ハンドル（またはピアノロール領域のドラッグ）から
 *   OSの一時フォルダに `TriLayerReharm_Export.mid` を生成し、DAWのタイムラインへ直接D&D可能
 * - v1.5 機能B & C: コードブロックごとのコードネーム描画（重なり防止付き）およびクリックによるLock（ピン留め）トグル
 */
class PianoRollComponent final : public juce::Component,
                                 public juce::FileDragAndDropTarget
{
public:
    PianoRollComponent();
    ~PianoRollComponent() override = default;

    /**
     * @brief 外部からMIDIファイル群がピアノロール上にドロップされた際に呼ばれるコールバック
     */
    std::function<void(const juce::StringArray&)> onFilesDroppedCallback;

    /**
     * @brief v1.5 機能B: ピアノロール上のコードブロックが左クリックされ、Lock（ピン留め）がトグルされた際のコールバック
     */
    std::function<void(size_t blockIndex)> onChordBlockLockToggled;

    /**
     * @brief v1.5 UI/UX: ピアノロール上のコードブロック上でマウスホイールまたは右クリックにより
     *        ボイシングの展開形（転回形）やオクターブが手動シフトされた際のコールバック
     */
    std::function<void(size_t blockIndex, int inversionSteps, int octaveShift)> onChordBlockVoicingShifted;

    //==============================================================================
    // juce::FileDragAndDropTarget オーバーライド
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    //==============================================================================
    /**
     * @brief `MidiReharmonizerEngine` が生成した最新の `MidiMessageSequence` をセットして再描画します。
     */
    void setMidiSequence(const juce::MidiMessageSequence& sequence, int ticksPerQuarterNote = 960);

    /**
     * @brief v1.5 UI/UX: 変換前の元MIDIシーケンス（`capturedSequence_`）をセットし、
     *        リハモ結果の背面に薄い半透明の枠（ゴーストノート）として描画します。
     */
    void setGhostMidiSequence(const juce::MidiMessageSequence& sequence, int ticksPerQuarterNote = 960);

    /**
     * @brief v1.5 UI/UX: DAWまたはAuditionの現在再生位置（PPQ）と再生中状態をセットし、
     *        リアルタイム・プレイヘッドと再生中コードブロックの発光エフェクトを更新します。
     */
    void setPlayheadPosition(double ppqPosition, bool isPlaying);

    /**
     * @brief v1.5 機能B & C: エンジン側の各コードブロック情報（ロック状態・構成音）をセットして
     *        コードネーム解析ラベルとロックUIを更新します。
     */
    void setChordBlockViews(const std::vector<engine::ChordBlockViewInfo>& blockViews);

    /**
     * @brief ノート矩形およびドラッグバッジのテーマカラーを設定します。
     */
    void setAccentColour(juce::Colour newColour) noexcept;

    /**
     * @brief 外部MIDIファイルがドラッグオーバーされている際の視覚ハイライト状態を設定します。
     */
    void setFileDragHighlight(bool isDraggingOver);

    /**
     * @brief 現在読み込まれているソースMIDIのラベル文字列（ファイル名など）を設定します。
     */
    void setSourceLabelText(const juce::String& newSourceLabel);

    //==============================================================================
    void paint(juce::Graphics& g) override;

    // マウス＆ドラッグ＆ドロップイベント
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    struct NoteRectData
    {
        int noteNumber { 60 };
        uint8_t velocity { 90 };
        double startTick { 0.0 };
        double endTick { 960.0 };
    };

    /**
     * @brief ピアノロール上に描画するコードネーム解析ラベル＆ロック状態情報
     */
    struct ChordLabelData
    {
        size_t blockIndex { 0 };
        double startTick { 0.0 };
        double endTick { 960.0 };
        int highestPitch { 72 };
        bool isLocked { false };
        bool isPassingChord { false };
        juce::String chordName;
        mutable juce::Rectangle<float> lastPillRect {};
    };

    [[nodiscard]] static bool isBlackKey(int midiNoteNumber) noexcept;

    /**
     * @brief 右上の「DRAG MIDI TO DAW」バッジ領域を計算して返します。
     */
    [[nodiscard]] juce::Rectangle<float> getDragBadgeBounds() const noexcept;

    /**
     * @brief 指定されたマウス座標に対応するコードブロックインデックスを検索します（該当なしは -1）。
     */
    [[nodiscard]] int findChordBlockIndexAtPosition(juce::Point<float> pos) const noexcept;

    /**
     * @brief 現在の `currentSequence_` を `juce::MidiFile` に変換し、
     *        OSの一時ディレクトリ（tempDirectory）へ `TriLayerReharm_Export.mid` として書き出します。
     * @return 書き出しに成功した場合は生成された `juce::File`、失敗時は空の `juce::File()`
     */
    [[nodiscard]] juce::File exportSequenceToTempMidiFile() const;

    //==============================================================================
    juce::MidiMessageSequence currentSequence_;
    int ticksPerQuarterNote_ { 960 };

    std::vector<NoteRectData> notes_;
    std::vector<NoteRectData> ghostNotes_;
    std::vector<ChordLabelData> chordLabels_;

    double playheadPpq_ { -1.0 };
    bool isPlaying_ { false };

    int minDisplayPitch_ { 36 };               ///< C2
    int maxDisplayPitch_ { 84 };               ///< C6
    double totalDisplayTicks_ { 960.0 * 16.0 }; ///< 4小節分 (960 TPQN基準)

    juce::Colour accentColour_ { 0xffff8c24 };

    bool isHoveringDragBadge_ { false };
    int hoveredBlockIndex_ { -1 };
    bool isDragExportInProgress_ { false };
    bool isFileDragHighlighted_ { false };
    juce::String sourceLabelText_ { "SOURCE: Preview Progression (Drop .mid here)" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};

} // namespace reharm::ui
