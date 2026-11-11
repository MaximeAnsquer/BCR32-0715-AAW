/*
 * seq.h - minimal 8 step sequencer with two step length behaviours (test firmware)
 *
 *   SEQ_MODE_OVERLAP  every step starts on the 1/16 grid; a longer step overlaps the next ones
 *   SEQ_MODE_SHIFT    steps play back to back; a longer step pushes the following ones later
 *                     (the pattern gets longer)
 *
 * Hardware independent: time comes in by Seq_Tick1ms, notes go out by SeqOut_NoteOn / Off,
 * which the application provides. Main loop only.
 */
#ifndef SEQ_H
#define SEQ_H

#include <stdint.h>

#define SEQ_STEPS           8U
#define SEQ_TICKS_PER_16TH  24U     /* 96 ticks per quarter note */
#define SEQ_GATE_GAP_TICKS  6U      /* a note ends this much before the end of its length */
#define SEQ_LEN_MIN         1U      /* step length in 1/16 notes */
#define SEQ_LEN_MAX         8U
#define SEQ_PITCH_MIN       36U     /* MIDI note, C2 */
#define SEQ_PITCH_MAX       96U
#define SEQ_BPM_MIN         40U
#define SEQ_BPM_MAX         240U

typedef enum
{
    SEQ_MODE_OVERLAP = 0,
    SEQ_MODE_SHIFT
} SeqMode_t;

/* provided by the application */
void SeqOut_NoteOn(uint8_t note);
void SeqOut_NoteOff(uint8_t note);

void Seq_Init(void);

/* Call once per elapsed millisecond */
void Seq_Tick1ms(void);

/* Stop sends the pending note offs and returns to the start of the pattern */
void Seq_SetPlaying(uint8_t on);
uint8_t Seq_IsPlaying(void);

void Seq_SetMode(SeqMode_t mode);
SeqMode_t Seq_GetMode(void);

/* Edits, clamped; step 0..SEQ_STEPS-1. Each returns the new value. */
uint8_t Seq_AddPitch(uint32_t step, int32_t delta);
uint8_t Seq_AddLength(uint32_t step, int32_t delta);
uint32_t Seq_AddTempo(int32_t delta);

uint8_t Seq_GetPitch(uint32_t step);
uint8_t Seq_GetLength(uint32_t step);
uint32_t Seq_GetTempo(void);

/* Layout in ticks, for the current mode */
uint32_t Seq_GetStartTicks(uint32_t step);
uint32_t Seq_GetNoteTicks(uint32_t step);       /* sounding time of the step */
uint32_t Seq_GetTotalTicks(void);
uint32_t Seq_GetPosTicks(void);                 /* playhead */

#endif /* SEQ_H */
