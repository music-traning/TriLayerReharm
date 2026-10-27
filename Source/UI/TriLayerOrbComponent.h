#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <random>

namespace reharm::ui
{

/**
 * @brief The Tri-Layer Orb: 3層レイヤー（Genre / Artist / Environment）および
 *        Mutation（突然変異・カオス度）の状態をリアルタイム描画するカスタムコンポーネント
 *
 * - Core (Genre): 中心の球体。ジャンルに応じた色彩＋高Mutation時に高彩度の蛍光警告色へフェード。
 * - Ring (Artist): コア周囲の軌道パス。0%付近では滑らかな真円軌道、100%に近づくにつれて
 *                  各頂点にノイズが乗り「ギザギザの不定形」へ歪み、回転速度も不規則に加減速（暴走）。
 * - Atmosphere (Environment): 外周を包む半透明オーラ。環境とMutation量に応じてブレとアルファが変化。
 */
class TriLayerOrbComponent final : public juce::Component,
                                   private juce::Timer
{
public:
    TriLayerOrbComponent();
    ~TriLayerOrbComponent() override;

    //==============================================================================
    /**
     * @brief 3層レイヤーの現在の選択状態を更新します。
     */
    void setLayerState(const juce::String& genreName,
                       int artistIndex,
                       const juce::String& environmentName);

    /**
     * @brief Mutation（突然変異）量（0.0f 〜 100.0f）を設定します。
     */
    void setMutationAmount(float mutationPercent);

    /**
     * @brief Phase 9: サイドチェイン音声のトランジェント検出時に呼び出し、
     *        オーブ全体をアタックに合わせてバウンス（脈動）させ、トランジェントLEDを点灯させます。
     */
    void setTransientBounceLevel(float bounceLevel, bool grooveSyncActive) noexcept;

    //==============================================================================
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    // 各レイヤーの描画ヘルパー
    void drawAtmosphereLayer(juce::Graphics& g, juce::Point<float> center, float baseRadius, juce::Colour coreColour);
    void drawArtistRingsLayer(juce::Graphics& g, juce::Point<float> center, float baseRadius, juce::Colour coreColour);
    void drawGenreCoreLayer(juce::Graphics& g, juce::Point<float> center, float coreRadius, juce::Colour coreColour);

    [[nodiscard]] juce::Colour resolveGenreColour() const noexcept;

    //==============================================================================
    juce::String currentGenre_ { "Jazz" };
    int currentArtistIndex_ { 0 };
    juce::String currentEnvironment_ { "Rainy" };
    float mutationAmount_ { 50.0f }; ///< 0.0f (Basic) 〜 100.0f (Chaos)

    float rotationAngleRad_ { 0.0f };
    float pulsePhaseRad_ { 0.0f };
    float transientBounce_ { 0.0f };
    bool isGrooveSyncActive_ { false };

    std::mt19937 rng_ { 1337u };
    juce::Random juceRng_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TriLayerOrbComponent)
};

} // namespace reharm::ui
