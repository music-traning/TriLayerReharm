# TriLayerReharm (by Universal Make Associates)
![TriLayerReharm UI Screenshot](docs/images/hero_image.png)

> **「白玉コードを、インスピレーションの暴風雨に変える。」**
> TriLayerReharmは、DAW上の退屈なMIDIリージョンを、ジャンル・アーティスト・環境の3層レイヤーで再解釈し、極上のボイシングとグルーヴを持つMIDIデータに再構築するオフラインVST3プラグインです。

---

## ⚡ Features (U.M.A Signature — Phase 14 Complete)

*   **The Tri-Layer Engine:** 「Genre(理論: 全8種) × Artist(手癖: 全4種) × Environment(情景: 全3種)」を掛け合わせ、Viterbiアルゴリズムによる動的計画法で曲全体を通した最も美しいボイスリーディング（Drop2 / Spread / Cluster等）を一瞬で計算します。
*   **Dynamic Harmonic Rhythm & Passing Chords (Phase 13):** 2拍を超える長い白玉コードブロックを自動検出し、直後のターゲットコードへ滑らかに解決する「裏コード（Tritone Substitution: bII7）」や「セカンダリードミナント（V7）」を逆算して自動挿入。高Mutation領域（80%〜100%）では1小節を3〜4拍に細分化し、「裏コードのさらに裏」を含むビバップ風の連続アプローチチェインを構築します。
*   **32 Progression Presets & 8-Bar Expansion (Phase 12 & 14):** `Jazz`, `Neo-Soul`, `City Pop`, `Cinematic`, `Lo-Fi`, `Modern R&B`, `Gospel`, `8-Bit (Chiptune)` の8ジャンル×4種＝計32種類の定番進行プリセットを搭載。さらに `4 Bars` と、後半（5〜8小節目）でサブドミナントマイナーやターンアラウンドへ音楽的に展開する `8 Bars` をワンクリックで切り替え可能です。
*   **Built-in 12-Voice Audition Synth & Full Pass-Through (Phase 12):** 12ボイスの軽量エレピ音源（`Audition (Preview)`）を内蔵し、DAW停止中でもプラグイン単体で即座にループ試聴可能。通常時はオーディオ・MIDIともに100%完全スルー（パススルー）し、後段の外部シンセ（Spire等）の演奏を一切妨げません。
*   **Audio Groove Sync (Sidechain):** DAWのドラムトラックをサイドチェインで流し込むだけで、トランジェントを解析し、バッキングMIDIの発音タイミングをその「ノリ（レイドバック/ドライブ）」に完全同期（吸着）させます。
*   **Mutation Slider & Undo/Redo History:** 0%（端正な基本ボイシング）から100%（オルタード＆連続パッシングコードのアバンギャルド）まで、リハモの過激さをリアルタイムにコントロール。`Ctrl+Z` / `Ctrl+Y` 対応の Undo / Redo 履歴管理も搭載。
*   **Drag & Drop Workflow & Realtime Chord Analyzer:** プラグイン画面への `.mid` ファイルD&Dインポートと、DAWタイムラインへの直接D&Dエクスポートに対応。生成されたボイシングやパッシングコードのコードネーム（例: `Db9(#11)`）をピアノロール上にリアルタイム描画します。

---

## 🎹 How it Works (Under the Hood)

単なる「コード置換辞書」ではありません。内部では以下の変態的な計算が回っています。

1.  **Combinatorics & Pruning:** 指定されたルートとクオリティから、指定音域内で取り得る構成音の全組み合わせを生成。非現実的な配置をC++レベルで高速に枝刈りします。
2.  **Scoring & Viterbi Search:** 各ボイシング候補に対し、プロファイル（半音ぶつかりの許容度など）に基づく静的コストと、前後の和音からの移動距離（指の移動の少なさ）に基づく動的コストを算出。トレリス構造を構築し、動的計画法で最短経路を一意に決定します。
3.  **Time Modulation:** 決定されたMIDIノートに対し、Environmentレイヤーの定義（ストラム、ヒューマナイズ）やサイドチェインのGroove Mapに従ってタイムスタンプ（Note On/Off）を再計算します。

---

## 🚀 Installation & Usage

**For Windows (Cubase Pro 13 / Studio One / Ableton Live, etc.)**

1. [Releases](https://github.com/music-traning/TriLayerReharm/releases) ページから最新の `TriLayerReharm_Installer_v1.0.0.exe` をダウンロード。
2. インストーラーを実行（自動的に `C:\Program Files\Common Files\VST3` に配置されます）。
3. DAWでVST3プラグインをスキャンし、MIDIトラック（またはインストゥルメントトラック）にインサートしてください。

---

## 🛠 Build Instructions (For Developers)

このプロジェクトは **JUCE** フレームワークと **CMake** (C++20) で構築されています。

```bash
# クローン
git clone https://github.com/music-traning/TriLayerReharm.git
cd TriLayerReharm

# CMakeでのビルド準備 (Visual Studio 2022を使用)
cmake -B build -G "Visual Studio 17 2022" -A x64

# ビルド実行（完了後、自動でVST3フォルダへコピーされます）
cmake --build build --config Release
```

---

## 📄 License & Copyright

**© 2026 Universal Make Associates (U.M.A)** — All Rights Reserved.
