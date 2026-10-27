#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace reharm::ui
{

/**
 * @brief Piano Roll Viewer & MIDI Drag-and-Drop Importer / Exporter
 *
 * - 横軸: 時間（Tick）、縦軸: ピッチ（MIDIノート番号）でボイシングを描画
 * - 外部からの `.mid` / `.midi` ファイルのドラッグ＆ドロップを受け取り、親エディタへ転送
 * - 右上の「DRAG MIDI TO DAW」ハンドル（またはピアノロール領域のドラッグ）から
 *   OSの一時フォルダに `TriLayerReharm_Export.mid` を生成し、DAWのタイムラインへ直接D&D可能
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

private:
    struct NoteRectData
    {
        int noteNumber { 60 };
        uint8_t velocity { 90 };
        double startTick { 0.0 };
        double endTick { 960.0 };
    };

    /**
     * @brief ピアノロール上に描画するコードネーム解析ラベル情報
     */
    struct ChordLabelData
    {
        double startTick { 0.0 };
        double endTick { 960.0 };
        int highestPitch { 72 };
        juce::String chordName;
    };

    [[nodiscard]] static bool isBlackKey(int midiNoteNumber) noexcept;

    /**
     * @brief 右上の「DRAG MIDI TO DAW」バッジ領域を計算して返します。
     */
    [[nodiscard]] juce::Rectangle<float> getDragBadgeBounds() const noexcept;

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
    std::vector<ChordLabelData> chordLabels_;

    int minDisplayPitch_ { 36 };               ///< C2
    int maxDisplayPitch_ { 84 };               ///< C6
    double totalDisplayTicks_ { 960.0 * 16.0 }; ///< 4小節分 (960 TPQN基準)

    juce::Colour accentColour_ { 0xffff8c24 };

    bool isHoveringDragBadge_ { false };
    bool isDragExportInProgress_ { false };
    bool isFileDragHighlighted_ { false };
    juce::String sourceLabelText_ { "SOURCE: Preview Progression (Drop .mid here)" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};

} // namespace reharm::ui
