/*
 * seq.c - minimal 8 step sequencer, see seq.h
 */
#include "seq.h"

#define US_PER_MS           1000U
#define TICK_US_AT_1BPM     625000U     /* 60 s / 96 ticks */

static uint8_t   s_pitch[SEQ_STEPS];
static uint8_t   s_len[SEQ_STEPS];              /* 1/16 notes */
static uint16_t  s_remain[SEQ_STEPS];           /* ticks until the note off, 0 = silent */
static uint8_t   s_sounding[SEQ_STEPS];         /* note sent with the note on */
static SeqMode_t s_mode;
static uint32_t  s_bpm;
static uint32_t  s_pos;                         /* tick played next */
static uint32_t  s_acc_us;
static uint8_t   s_playing;

static int32_t Clamp(int32_t v, int32_t lo, int32_t hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void Seq_Init(void)
{
    static const uint8_t pitch[SEQ_STEPS] = {48U, 51U, 53U, 55U, 58U, 60U, 58U, 55U};
    uint32_t i;

    for (i = 0U; i < SEQ_STEPS; i++)
    {
        s_pitch[i] = pitch[i];
        s_len[i] = SEQ_LEN_MIN;
        s_remain[i] = 0U;
    }
    s_mode = SEQ_MODE_SHIFT;
    s_bpm = 120U;
    s_pos = 0U;
    s_acc_us = 0U;
    s_playing = 0U;
}

uint32_t Seq_GetStartTicks(uint32_t step)
{
    uint32_t i, units = 0U;

    if (s_mode == SEQ_MODE_OVERLAP)
    {
        return step * SEQ_TICKS_PER_16TH;
    }
    for (i = 0U; (i < step) && (i < SEQ_STEPS); i++)
    {
        units += s_len[i];
    }
    return units * SEQ_TICKS_PER_16TH;
}

uint32_t Seq_GetNoteTicks(uint32_t step)
{
    return ((uint32_t)s_len[step % SEQ_STEPS] * SEQ_TICKS_PER_16TH) - SEQ_GATE_GAP_TICKS;
}

uint32_t Seq_GetTotalTicks(void)
{
    return (s_mode == SEQ_MODE_OVERLAP) ? (SEQ_STEPS * SEQ_TICKS_PER_16TH) : Seq_GetStartTicks(SEQ_STEPS);
}

uint32_t Seq_GetPosTicks(void)
{
    return s_pos;
}

static void StepOff(uint32_t i)
{
    if (s_remain[i] != 0U)
    {
        s_remain[i] = 0U;
        SeqOut_NoteOff(s_sounding[i]);
    }
}

static void AllOff(void)
{
    uint32_t i;

    for (i = 0U; i < SEQ_STEPS; i++)
    {
        StepOff(i);
    }
}

/* One tick: note offs that are due, then the steps starting on this tick */
static void Advance(void)
{
    uint32_t i;

    for (i = 0U; i < SEQ_STEPS; i++)
    {
        if ((s_remain[i] != 0U) && (--s_remain[i] == 0U))
        {
            SeqOut_NoteOff(s_sounding[i]);
        }
    }
    if (s_pos >= Seq_GetTotalTicks())
    {
        s_pos = 0U;
    }
    for (i = 0U; i < SEQ_STEPS; i++)
    {
        if (Seq_GetStartTicks(i) == s_pos)
        {
            StepOff(i);
            s_sounding[i] = s_pitch[i];
            s_remain[i] = (uint16_t)Seq_GetNoteTicks(i);
            SeqOut_NoteOn(s_sounding[i]);
        }
    }
    s_pos++;
}

void Seq_Tick1ms(void)
{
    const uint32_t tick_us = TICK_US_AT_1BPM / s_bpm;

    if (s_playing == 0U)
    {
        return;
    }
    s_acc_us += US_PER_MS;
    while (s_acc_us >= tick_us)
    {
        s_acc_us -= tick_us;
        Advance();
    }
}

void Seq_SetPlaying(uint8_t on)
{
    if (on == s_playing)
    {
        return;
    }
    s_playing = on;
    s_pos = 0U;
    s_acc_us = 0U;
    if (on == 0U)
    {
        AllOff();
    }
}

uint8_t Seq_IsPlaying(void)
{
    return s_playing;
}

void Seq_SetMode(SeqMode_t mode)
{
    s_mode = mode;
}

SeqMode_t Seq_GetMode(void)
{
    return s_mode;
}

uint8_t Seq_AddPitch(uint32_t step, int32_t delta)
{
    step %= SEQ_STEPS;
    s_pitch[step] = (uint8_t)Clamp((int32_t)s_pitch[step] + delta, (int32_t)SEQ_PITCH_MIN, (int32_t)SEQ_PITCH_MAX);
    return s_pitch[step];
}

uint8_t Seq_AddLength(uint32_t step, int32_t delta)
{
    step %= SEQ_STEPS;
    s_len[step] = (uint8_t)Clamp((int32_t)s_len[step] + delta, (int32_t)SEQ_LEN_MIN, (int32_t)SEQ_LEN_MAX);
    return s_len[step];
}

uint32_t Seq_AddTempo(int32_t delta)
{
    s_bpm = (uint32_t)Clamp((int32_t)s_bpm + delta, (int32_t)SEQ_BPM_MIN, (int32_t)SEQ_BPM_MAX);
    return s_bpm;
}

uint8_t Seq_GetPitch(uint32_t step)
{
    return s_pitch[step % SEQ_STEPS];
}

uint8_t Seq_GetLength(uint32_t step)
{
    return s_len[step % SEQ_STEPS];
}

uint32_t Seq_GetTempo(void)
{
    return s_bpm;
}
