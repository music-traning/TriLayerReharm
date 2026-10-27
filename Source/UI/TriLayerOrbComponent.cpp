#include "TriLayerOrbComponent.h"
#include <algorithm>
#include <cmath>

namespace reharm::ui
{

TriLayerOrbComponent::TriLayerOrbComponent()
{
    // 約30Hz（30ms間隔）でアニメーションタイマーを開始
    startTimer(30);
}

TriLayerOrbComponent::~TriLayerOrbComponent()
{
    stopTimer();
}

void TriLayerOrbComponent::setLayerState(const juce::String& genreName,
                                         int artistIndex,
                                         const juce::String& environmentName)
{
    currentGenre_ = genreName;
    currentArtistIndex_ = artistIndex;
    currentEnvironment_ = environmentName;
    repaint();
}

void TriLayerOrbComponent::setMutationAmount(float mutationPercent)
{
    mutationAmount_ = std::clamp(mutationPercent, 0.0f, 100.0f);
    repaint();
}

void TriLayerOrbComponent::setTransientBounceLevel(float bounceLevel, bool grooveSyncActive) noexcept
{
    transientBounce_ = std::clamp(bounceLevel, 0.0f, 1.0f);
    isGrooveSyncActive_ = grooveSyncActive;
}

void TriLayerOrbComponent::timerCallback()
{
    const float normMutation = mutationAmount_ / 100.0f;

    // アーティストごとの基準角速度
    float baseAngularSpeed = 0.035f;
    switch (currentArtistIndex_)
    {
        case 0: baseAngularSpeed = 0.030f; break; // Artist A: Smooth
        case 1: baseAngularSpeed = 0.055f; break; // Artist B: Dynamic
        case 2: baseAngularSpeed = 0.080f; break; // Artist C: Fast / Drunk Groove
        case 3: baseAngularSpeed = 0.018f; break; // Artist D: Minimal / Slow
        default: break;
    }

    // 0% 付近では完全に一定の規則的な角速度、
    // 100% に近づくにつれて不規則な急加速・急減速（暴走状態）を発生させる
    const float chaosIntensity = normMutation * normMutation;
    const float erraticMultiplier = 1.0f + chaosIntensity * (juceRng_.nextFloat() * 4.8f - 1.1f);
    const float effectiveDeltaAngle = baseAngularSpeed * (0.65f + normMutation * 0.9f) * erraticMultiplier;

    rotationAngleRad_ = std::fmod(rotationAngleRad_ + effectiveDeltaAngle, juce::MathConstants<float>::twoPi);
    if (rotationAngleRad_ < 0.0f)
        rotationAngleRad_ += juce::MathConstants<float>::twoPi;

    const float pulseDelta = 0.04f + chaosIntensity * (0.14f * juceRng_.nextFloat());
    pulsePhaseRad_ = std::fmod(pulsePhaseRad_ + pulseDelta, juce::MathConstants<float>::twoPi);

    repaint();
}

juce::Colour TriLayerOrbComponent::resolveGenreColour() const noexcept
{
    juce::Colour baseColour(0xffff8c24); // Jazz: Warm Amber
    if (currentGenre_.containsIgnoreCase("Neo-Soul"))
        baseColour = juce::Colour(0xffa855f7); // Neo-Soul: Rich Violet
    else if (currentGenre_.containsIgnoreCase("City Pop"))
        baseColour = juce::Colour(0xffec4899); // City Pop: Neon Pink
    else if (currentGenre_.containsIgnoreCase("Cinematic"))
        baseColour = juce::Colour(0xff14b8a6); // Cinematic: Deep Emerald

    // Mutation が 50% を超えて 100% に近づくにつれ、彩度の高い蛍光警告色（ネオンマゼンタ／アシッドレッド）へフェード
    const float normMutation = mutationAmount_ / 100.0f;
    if (normMutation > 0.5f)
    {
        const float chaosBlend = (normMutation - 0.5f) * 2.0f; // 0.0 〜 1.0
        const float flicker = 0.5f + 0.5f * std::sin(pulsePhaseRad_ * 3.0f);
        const juce::Colour fluorescentWarning = juce::Colour(0xffff0055).interpolatedWith(
            juce::Colour(0xff00ffcc), flicker * 0.35f);
        baseColour = baseColour.interpolatedWith(fluorescentWarning, chaosBlend * 0.78f);
    }

    return baseColour;
}

void TriLayerOrbComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // 背景パネル
    g.setColour(juce::Colour(0xff11131a));
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(juce::Colour(0xff262936));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);

    const auto center = bounds.getCentre();
    // Phase 9: サイドチェインのトランジェント強度に応じてオーブ全体の半径を瞬間的にバウンス（拡大脈動）させる
    const float bounceScale = 1.0f + transientBounce_ * 0.16f;
    const float maxRadius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.42f * bounceScale;
    const float coreRadius = maxRadius * (0.33f + transientBounce_ * 0.05f);
    const juce::Colour coreColour = resolveGenreColour();

    // Layer 3 (外側): Atmosphere (Environment + Mutation)
    drawAtmosphereLayer(g, center, maxRadius, coreColour);

    // Layer 2 (中間): Orbiting Rings (Artist + Mutation Jagged Noise)
    drawArtistRingsLayer(g, center, maxRadius, coreColour);

    // Layer 1 (中心): Core (Genre + Fluorescent Mutation Glow)
    drawGenreCoreLayer(g, center, coreRadius, coreColour);

    // 左上のレイヤーステータスラベル
    g.setColour(juce::Colours::white.withAlpha(0.65f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(currentGenre_ + "  |  Artist " + juce::String(currentArtistIndex_ + 1) + "  |  " + currentEnvironment_,
               bounds.reduced(12.0f, 8.0f),
               juce::Justification::topLeft,
               false);

    // 右上に現在の Mutation モードインジケーターを表示
    juce::String mutState = "ADVANCED";
    if (mutationAmount_ < 20.0f)
        mutState = "BASIC (SAFE)";
    else if (mutationAmount_ > 75.0f)
        mutState = "CHAOS (MUTATING)";

    g.setColour(coreColour.brighter(0.3f).withAlpha(0.85f));
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.drawText("MUTATION: " + juce::String(static_cast<int>(std::round(mutationAmount_))) + "% [" + mutState + "]",
               bounds.reduced(12.0f, 8.0f),
               juce::Justification::topRight,
               false);

    // 左下に Audio Groove Sync LED インジケーターを描画
    const float ledX = bounds.getX() + 14.0f;
    const float ledY = bounds.getBottom() - 20.0f;
    const juce::Rectangle<float> ledRect(ledX, ledY, 9.0f, 9.0f);

    const juce::Colour ledBaseColour = isGrooveSyncActive_ ? juce::Colour(0xff10b981) : juce::Colour(0xff475569);
    const float ledGlowAlpha = isGrooveSyncActive_ ? (0.45f + transientBounce_ * 0.55f) : (0.20f + transientBounce_ * 0.65f);

    if (transientBounce_ > 0.05f)
    {
        g.setColour(juce::Colour(0xff34d399).withAlpha(transientBounce_ * 0.5f));
        g.fillEllipse(ledRect.expanded(4.0f * transientBounce_));
    }
    g.setColour(ledBaseColour.withAlpha(ledGlowAlpha));
    g.fillEllipse(ledRect);
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawEllipse(ledRect, 1.0f);

    g.setColour(juce::Colours::white.withAlpha(isGrooveSyncActive_ ? 0.85f : 0.45f));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(isGrooveSyncActive_ ? "GROOVE SYNC : LOCKED" : "GROOVE SYNC : STANDBY",
               juce::Rectangle<float>(ledX + 14.0f, ledY - 3.0f, 180.0f, 15.0f),
               juce::Justification::centredLeft,
               false);
}

void TriLayerOrbComponent::drawAtmosphereLayer(
    juce::Graphics& g,
    juce::Point<float> center,
    float baseRadius,
    juce::Colour coreColour)
{
    const float normMutation = mutationAmount_ / 100.0f;

    float envJitter = 0.8f;
    float baseAlpha = 0.25f;
    juce::Colour envTint = coreColour;

    if (currentEnvironment_.containsIgnoreCase("Rainy"))
    {
        envJitter = 1.5f;
        baseAlpha = 0.28f;
        envTint = coreColour.interpolatedWith(juce::Colour(0xff38bdf8), 0.45f);
    }
    else if (currentEnvironment_.containsIgnoreCase("VHS"))
    {
        envJitter = 4.5f;
        baseAlpha = 0.35f;
        envTint = coreColour.interpolatedWith(juce::Colour(0xfff43f5e), 0.35f);
    }
    else if (currentEnvironment_.containsIgnoreCase("Deep Space"))
    {
        envJitter = 0.6f;
        baseAlpha = 0.42f;
        envTint = coreColour.interpolatedWith(juce::Colour(0xff6366f1), 0.55f);
    }

    // 0% 付近ではブレをほぼ 0 に抑え、100% に向かって激しく揺らす
    const float effectiveJitter = envJitter * (normMutation * 2.2f);
    std::uniform_real_distribution<float> jitterDist(-effectiveJitter, effectiveJitter);

    const float breathingScale = 1.0f + (0.04f + 0.08f * normMutation) * std::sin(pulsePhaseRad_);
    const float outerRadius = baseRadius * breathingScale;

    for (int layer = 0; layer < 3; ++layer)
    {
        const juce::Point<float> jitteredCenter(
            center.x + (effectiveJitter > 0.01f ? jitterDist(rng_) : 0.0f),
            center.y + (effectiveJitter > 0.01f ? jitterDist(rng_) : 0.0f)
        );

        const float layerRadius = outerRadius * (1.0f - static_cast<float>(layer) * 0.14f);
        const float layerAlpha = baseAlpha * (1.0f - static_cast<float>(layer) * 0.22f);

        juce::ColourGradient grad(
            envTint.withAlpha(layerAlpha),
            jitteredCenter.x,
            jitteredCenter.y,
            envTint.withAlpha(0.0f),
            jitteredCenter.x + layerRadius,
            jitteredCenter.y,
            true
        );

        g.setGradientFill(grad);
        g.fillEllipse(jitteredCenter.x - layerRadius,
                      jitteredCenter.y - layerRadius,
                      layerRadius * 2.0f,
                      layerRadius * 2.0f);
    }
}

void TriLayerOrbComponent::drawArtistRingsLayer(
    juce::Graphics& g,
    juce::Point<float> center,
    float baseRadius,
    juce::Colour coreColour)
{
    const float normMutation = mutationAmount_ / 100.0f;
    // 二乗カーブで 0%付近は完全な真円、後半で急速にギザギザの不定形へ歪ませる
    const float distortionFactor = normMutation * normMutation;

    float radiusRatio1 = 0.56f;
    float radiusRatio2 = 0.76f;
    float strokeWidth1 = 2.5f;
    float strokeWidth2 = 1.5f;
    int numArcSegments = 2;

    switch (currentArtistIndex_)
    {
        case 0:
            radiusRatio1 = 0.55f;
            radiusRatio2 = 0.74f;
            strokeWidth1 = 2.6f;
            strokeWidth2 = 1.6f;
            numArcSegments = 2;
            break;
        case 1:
            radiusRatio1 = 0.62f;
            radiusRatio2 = 0.88f;
            strokeWidth1 = 4.2f;
            strokeWidth2 = 2.8f;
            numArcSegments = 3;
            break;
        case 2:
            radiusRatio1 = 0.48f;
            radiusRatio2 = 0.66f;
            strokeWidth1 = 2.0f;
            strokeWidth2 = 3.2f;
            numArcSegments = 4;
            break;
        case 3:
        default:
            radiusRatio1 = 0.60f;
            radiusRatio2 = 0.80f;
            strokeWidth1 = 1.2f;
            strokeWidth2 = 1.0f;
            numArcSegments = 2;
            break;
    }

    const float r1 = baseRadius * radiusRatio1;
    const float r2 = baseRadius * radiusRatio2;

    // ガイド用の背景サークル
    g.setColour(coreColour.withAlpha(0.14f));
    g.drawEllipse(center.x - r1, center.y - r1, r1 * 2.0f, r1 * 2.0f, 1.0f);
    g.drawEllipse(center.x - r2, center.y - r2, r2 * 2.0f, r2 * 2.0f, 1.0f);

    // 頂点ごとにランダムノイズ＋高調波変調を加えて「真円 -> ギザギザの不定形」を生成するラムダ
    auto buildMutatedArcPath = [&](float radius, float baseAngleOffset, float spanRatio) -> juce::Path
    {
        juce::Path path;
        const float stepAngle = juce::MathConstants<float>::twoPi / static_cast<float>(numArcSegments);
        const float arcSpan = stepAngle * spanRatio;
        constexpr int kVerticesPerArc = 28;

        for (int seg = 0; seg < numArcSegments; ++seg)
        {
            const float startA = baseAngleOffset + static_cast<float>(seg) * stepAngle;

            for (int v = 0; v <= kVerticesPerArc; ++v)
            {
                const float t = static_cast<float>(v) / static_cast<float>(kVerticesPerArc);
                const float angle = startA + t * arcSpan;

                // 0% では jitter = 0 (完全な真円軌道)、100% では激しいギザギザのスパイクノイズを加算
                const float harmonicWave = std::sin(angle * 9.0f + pulsePhaseRad_ * 2.0f) * (radius * 0.08f * distortionFactor);
                const float randomSpike  = (juceRng_.nextFloat() * 2.0f - 1.0f) * (radius * 0.24f * distortionFactor);
                const float mutatedR     = std::max(8.0f, radius + harmonicWave + randomSpike);

                const float px = center.x + std::cos(angle) * mutatedR;
                const float py = center.y + std::sin(angle) * mutatedR;

                if (v == 0)
                    path.startNewSubPath(px, py);
                else
                    path.lineTo(px, py);
            }
        }
        return path;
    };

    // 内側リング（順回転）
    const juce::Path innerArcs = buildMutatedArcPath(r1, rotationAngleRad_, 0.60f);
    g.setColour(coreColour.brighter(0.35f).withAlpha(0.90f));
    g.strokePath(innerArcs, juce::PathStrokeType(strokeWidth1, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 外側リング（逆回転）
    const juce::Path outerArcs = buildMutatedArcPath(r2, -rotationAngleRad_ * 0.75f + 0.4f, 0.46f);
    g.setColour(coreColour.brighter(0.65f).withAlpha(0.70f));
    g.strokePath(outerArcs, juce::PathStrokeType(strokeWidth2, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void TriLayerOrbComponent::drawGenreCoreLayer(
    juce::Graphics& g,
    juce::Point<float> center,
    float coreRadius,
    juce::Colour coreColour)
{
    const float normMutation = mutationAmount_ / 100.0f;
    const float chaosJitter = (normMutation > 0.6f)
                                  ? (normMutation - 0.6f) * 7.5f * (juceRng_.nextFloat() * 2.0f - 1.0f)
                                  : 0.0f;

    const juce::Point<float> c(center.x + chaosJitter, center.y - chaosJitter * 0.7f);
    const float pulseRadius = coreRadius * (1.0f + (0.03f + 0.07f * normMutation) * std::cos(pulsePhaseRad_ * 1.5f));

    juce::ColourGradient coreGrad(
        coreColour.brighter(0.75f),
        c.x - pulseRadius * 0.3f,
        c.y - pulseRadius * 0.3f,
        coreColour.darker(0.55f),
        c.x + pulseRadius,
        c.y + pulseRadius,
        true
    );

    g.setGradientFill(coreGrad);
    g.fillEllipse(c.x - pulseRadius,
                  c.y - pulseRadius,
                  pulseRadius * 2.0f,
                  pulseRadius * 2.0f);

    g.setColour(juce::Colours::white.withAlpha(0.45f + 0.35f * normMutation));
    g.drawEllipse(c.x - pulseRadius,
                  c.y - pulseRadius,
                  pulseRadius * 2.0f,
                  pulseRadius * 2.0f,
                  1.5f + normMutation * 1.2f);
}

} // namespace reharm::ui
