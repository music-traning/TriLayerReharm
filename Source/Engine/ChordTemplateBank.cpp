#include "ChordTemplateBank.h"

namespace reharm::engine
{

namespace
{
    void addBlockChordToSequence(
        juce::MidiMessageSequence& seq,
        const std::vector<int>& midiNotes,
        double startBeatsPpq,
        double durationBeatsPpq,
        juce::uint8 velocity,
        double ticksPerQuarterNote)
    {
        const double startTick = startBeatsPpq * ticksPerQuarterNote;
        const double endTick   = (startBeatsPpq + durationBeatsPpq) * ticksPerQuarterNote;

        for (int note : midiNotes)
        {
            juce::MidiMessage onMsg = juce::MidiMessage::noteOn(1, note, velocity);
            onMsg.setTimeStamp(startTick);
            seq.addEvent(onMsg);

            juce::MidiMessage offMsg = juce::MidiMessage::noteOff(1, note);
            offMsg.setTimeStamp(endTick);
            seq.addEvent(offMsg);
        }
    }
} // anonymous namespace

juce::StringArray ChordTemplateBank::getTemplateNames()
{
    return {
        "Jazz : 2-5-1-6 (Dm7 - G7 - Cmaj7 - A7)",
        "City Pop : Just The Two of Us (Dbmaj7 - C7 - Fm7 - Ebm7 Ab7)",
        "Neo-Soul : 4-3-2-1 (Fmaj7 - Em7 - Dm7 - Cmaj7)",
        "Cinematic : Pedal Sus4 (Csus4 - C - Bbsus4/C - Bb/C)"
    };
}

juce::MidiMessageSequence ChordTemplateBank::createTemplateSequence(
    TemplateId templateId,
    double ticksPerQuarterNote)
{
    juce::MidiMessageSequence seq;

    switch (templateId)
    {
        case TemplateId::Jazz2516:
        {
            // Bar 1: Dm7 (D3, F3, A3, C4)
            addBlockChordToSequence(seq, { 50, 53, 57, 60 }, 0.0,  3.85, 88, ticksPerQuarterNote);
            // Bar 2: G7 (G2, F3, A3, B3)
            addBlockChordToSequence(seq, { 43, 53, 57, 59 }, 4.0,  3.85, 92, ticksPerQuarterNote);
            // Bar 3: Cmaj7 (C3, E3, G3, B3)
            addBlockChordToSequence(seq, { 48, 52, 55, 59 }, 8.0,  3.85, 86, ticksPerQuarterNote);
            // Bar 4: A7 (A2, G3, C#4, E4)
            addBlockChordToSequence(seq, { 45, 55, 61, 64 }, 12.0, 3.85, 90, ticksPerQuarterNote);
            break;
        }

        case TemplateId::CityPopJustTheTwoOfUs:
        {
            // Bar 1: Dbmaj7 (Db3, F3, Ab3, C4)
            addBlockChordToSequence(seq, { 49, 53, 56, 60 }, 0.0,  3.85, 90, ticksPerQuarterNote);
            // Bar 2: C7 (C3, E3, G3, Bb3)
            addBlockChordToSequence(seq, { 48, 52, 55, 58 }, 4.0,  3.85, 94, ticksPerQuarterNote);
            // Bar 3: Fm7 (F2, Eb3, Ab3, C4)
            addBlockChordToSequence(seq, { 41, 51, 56, 60 }, 8.0,  3.85, 88, ticksPerQuarterNote);
            // Bar 4 (前半2拍): Ebm7 (Eb3, Gb3, Bb3, Db4)
            addBlockChordToSequence(seq, { 51, 54, 58, 61 }, 12.0, 1.90, 90, ticksPerQuarterNote);
            // Bar 4 (後半2拍): Ab7 (Ab2, Gb3, C4, Eb4)
            addBlockChordToSequence(seq, { 44, 54, 60, 63 }, 14.0, 1.85, 95, ticksPerQuarterNote);
            break;
        }

        case TemplateId::NeoSoul4321:
        {
            // Bar 1: Fmaj7 (F2, C3, E3, A3)
            addBlockChordToSequence(seq, { 41, 48, 52, 57 }, 0.0,  3.85, 84, ticksPerQuarterNote);
            // Bar 2: Em7 (E2, B2, D3, G3)
            addBlockChordToSequence(seq, { 40, 47, 50, 55 }, 4.0,  3.85, 82, ticksPerQuarterNote);
            // Bar 3: Dm7 (D2, A2, C3, F3)
            addBlockChordToSequence(seq, { 38, 45, 48, 53 }, 8.0,  3.85, 86, ticksPerQuarterNote);
            // Bar 4: Cmaj7 (C2, G2, B2, E3)
            addBlockChordToSequence(seq, { 36, 43, 47, 52 }, 12.0, 3.85, 80, ticksPerQuarterNote);
            break;
        }

        case TemplateId::CinematicPedalSus4:
        {
            // Bar 1: Csus4 (C3, F3, G3, C4)
            addBlockChordToSequence(seq, { 48, 53, 55, 60 }, 0.0,  3.85, 88, ticksPerQuarterNote);
            // Bar 2: C (C3, E3, G3, C4)
            addBlockChordToSequence(seq, { 48, 52, 55, 60 }, 4.0,  3.85, 85, ticksPerQuarterNote);
            // Bar 3: Bbsus4/C (C3, F3, Bb3, Eb4) -> C7sus4系サウンド
            addBlockChordToSequence(seq, { 48, 53, 58, 63 }, 8.0,  3.85, 94, ticksPerQuarterNote);
            // Bar 4: Bb/C (C3, F3, Bb3, D4) -> C9sus4 / Bb over C
            addBlockChordToSequence(seq, { 48, 53, 58, 62 }, 12.0, 3.85, 90, ticksPerQuarterNote);
            break;
        }
    }

    seq.sort();
    seq.updateMatchedPairs();
    return seq;
}

} // namespace reharm::engine
