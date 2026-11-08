# TriLayerReharm v1.5.0 (by Universal Make Associates)
![TriLayerReharm UI Screenshot](docs/images/hero_image.png)

> **「白玉コードを、インスピレーションの暴風雨に変える。」**
> TriLayerReharmは、DAW上の退屈なMIDIリージョンを、ジャンル・アーティスト（巨匠シミュレーター）・環境の3層レイヤーで再解釈し、極上のボイシングとグルーヴを持つMIDIデータに再構築するVST3プラグインです。

---

## 🔥 What's New in v1.5.0 (Monster Commercial Upgrade)

v1.0.0 から和声エンジン・リズム生成・UI/UX・DAW連携のすべてを刷新したメジャーアップデートです：

1. **巨匠ボイシング・シミュレーター (Artist Layer 2.0):**
   * `1. Rootless Master (ex. Bill E.)` — ルート音を省略し、密集クラスターと絶妙なレイドバックを備えたジャズ・ピアノの極致。
   * `2. Quartal Power (ex. McCoy T.)` — 完全4度堆積（Quartal Voicing）にコストボーナスを与え、力強いモーダル・サウンドを生成。
   * `3. Melody & Bass (ex. Joe P.)` — Drop 2 ボイシングを強く選好し、トップノート（メロディ）とベースラインの滑らかな対位法的進行を最優先。
   * `4. Standard (Default)` — バランスの取れたオールラウンド設計。
2. **既存トラックと絶対にケンカしない「3つの和声制御」＋ 7スケール拡張:**
   * **Melody Lock:** 元のMIDIの最高音（トップノート）を絶対維持し、既存の歌メロやリードと衝突しない内声リハーモナイズを実行。
   * **Force Diatonic + 7 Scales/Modes (`KEY` & `SCALE / MODE`):** 12キー × 7スケール（`Major (Ionian)`, `Natural Minor (Aeolian)`, `Harmonic Minor`, `Melodic Minor`, `Dorian`, `Mixolydian`, `Lydian`）でスケール外音・アボイドノート（短9度衝突）を厳密に排除しつつ、`#11`（Lydian）やナチュラル `13th`（Dorian）などのモード特性音を解禁。
   * **Pedal Bass Mode:** フレーズ先頭のベース音（または主音）を全コードの最低音として固定し、映画音楽やネオソウル特有のペダルポイント（分数コード）を生成。
3. **「白玉」を「生きた演奏」に変えるリズム分割 (Comping Style):**
   * `Sustained(白玉)` / `Neo-Soul Pocket`（左手ベース維持＋右手裏拍レイドバック刻み） / `Bossa Bounce`（ボサノヴァ風ベースバウンス＋シンコペーション刻み）の3モードを搭載。
4. **ベース音と上声部のMIDIチャンネル分離出力 (`Split Bass/Chord Ch.`):**
   * ONにすると最低音（ベース声部）を **MIDI Ch.1**、上声部（コード・テンション）を **MIDI Ch.2** に分離出力。DAW側で別トラックのシンセベースとエレピ／パッドを同時に鳴らし分け可能。
5. **インタラクティブ・ピアノロール強化 (Pin Lock / Wheel Shift / Ghost Notes / Playhead):**
   * **コードブロック個別ロック（ピン留め）:** 気に入った小節・拍のボイシングを左クリック1発でピン留め固定（`🔒`）。
   * **マウスホイール／右クリックによる転回形・オクターブ手動シフト:** 特定のブロック上でホイール回転（または右クリックメニュー）するだけで展開形（転回形）やオクターブを即時シフトし、前後のボイスリーディングも自動再計算。
   * **元MIDIゴーストノート ＆ リアルタイム・プレイヘッド:** 変換前の元MIDIを半透明の破線ゴーストノートとして背面表示し、DAW再生位置（PPQ）に追従する発光プレイヘッドを描画。
6. **原曲完全再現バイパス ＆ `A/B Compare` ＆ Undo保護付き `Reset` / `Layer Safe`:**
   * **Mutation = 0.0% パススルー:** Mutation 0%時はViterbi計算をバイパスし、インポートした元MIDIの構成音を100%忠実に再現。
   * **`A/B: Reharm / Original` トグル:** 現在の設定やMutation値を維持したまま、ワンクリックで原曲とリハモ結果を瞬時に聴き比べ。
   * **`Layer Safe` ＆ `Reset` ボタン:** 既存トラックに馴染む黄金比（City Pop + Mutation 32%）へ一発設定する `Layer Safe` と、Undo保護付きで初期状態へ安全に戻す `Reset` を搭載。
7. **お気に入り状態のユーザープリセット保存 (`Save` / `Load`) ＆ DAW完全状態復元:**
   * パラメーター・インポートMIDI・ピン留め（Lock）状態を統合XML（`.xml` / `.tlrpreset`）として `<AppData>/TriLayerReharm/Presets` に保存・ワンクリック呼び出し。DAWプロジェクト保存・再読み込み時もヘッドレス再計算により完全復元。

---

## ⚡ Core Architecture

*   **The Tri-Layer Engine:** 「Genre(理論: 全8種) × Artist(巨匠の手癖: 全4種) × Environment(情景: 全3種)」を掛け合わせ、Viterbiアルゴリズムによる動的計画法で曲全体を通した最も美しいボイスリーディングを一瞬で計算します。
*   **Dynamic Harmonic Rhythm & Passing Chords:** 2拍を超える長い白玉コードブロックを自動検出し、直後のターゲットコードへ滑らかに解決する「裏コード（Tritone Substitution: bII7）」や「セカンダリードミナント（V7）」を逆算して自動挿入。
*   **32 Progression Presets & 8-Bar Expansion:** `Jazz`, `Neo-Soul`, `City Pop`, `Cinematic`, `Lo-Fi`, `Modern R&B`, `Gospel`, `8-Bit (Chiptune)` の8ジャンル×4種＝計32種類の定番進行プリセット（`4 Bars` / `8 Bars`）を搭載。
*   **Built-in 12-Voice Audition Synth & Full Pass-Through:** 12ボイスの軽量エレピ音源（`Audition (Preview)`）を内蔵し、DAW停止中でもプラグイン単体で即座にループ試聴可能。
*   **Audio Groove Sync (Sidechain):** サイドチェイン入力されたドラムトラックのトランジェントを解析し、バッキングMIDIの発音タイミングをドラムのポケットへ完全吸着。

---

## 🚀 Installation & Usage

**For Windows (Cubase Pro / Studio One / Ableton Live / FL Studio / Reaper, etc.)**

1. [Releases](https://github.com/music-traning/TriLayerReharm/releases) ページから最新の **`TriLayerReharm_Installer_v1.5.0.exe`** をダウンロード。
2. インストーラーを実行（自動的に `C:\Program Files\Common Files\VST3` に配置されます）。
3. DAWでVST3プラグインをスキャンし、インストゥルメントトラック（またはMIDIトラック）にインサートしてください。

---

## 🛠 Build Instructions (For Developers)

このプロジェクトは **JUCE 8** フレームワークと **CMake** (C++20) で構築されています。

```bash
# クローン
git clone https://github.com/music-traning/TriLayerReharm.git
cd TriLayerReharm

# CMakeでのビルド準備 (Visual Studio 2022を使用)
cmake -B build -G "Visual Studio 17 2022" -A x64

# ビルド実行（完了後、自動でVST3フォルダへコピーされます）
cmake --build build --config Release --target TriLayerReharm_VST3
```

---

## 📄 License & Copyright

**© 2026 Universal Make Associates (U.M.A)** — All Rights Reserved.
