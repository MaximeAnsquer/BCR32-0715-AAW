/*
 * seq_test.c - host test of seq.c (no hardware):  cc -I../App seq_test.c ../App/seq.c -o seq_test && ./seq_test
 * Checks the note on times of one pattern in both modes after step 2 is made 3 sixteenths long.
 */
#include <stdio.h>
#include "seq.h"

static unsigned s_ms, s_on_ms[64], s_on_note[64], s_n, s_off, s_sounding, s_max_sounding;

void SeqOut_NoteOn(uint8_t note)
{
    if (s_n < 64U) { s_on_ms[s_n] = s_ms; s_on_note[s_n] = note; s_n++; }
    if (++s_sounding > s_max_sounding) { s_max_sounding = s_sounding; }
}

void SeqOut_NoteOff(uint8_t note)
{
    (void)note; s_off++; s_sounding--;
}

/* plays ms milliseconds at 120 BPM (1/16 = 125 ms); returns the number of failed checks */
static int Run(SeqMode_t mode, const unsigned *expect_16th, unsigned count, unsigned ms, unsigned expect_poly)
{
    unsigned i; int fail = 0;

    Seq_Init();
    Seq_SetMode(mode);
    (void)Seq_AddLength(1U, 2);     /* step 2: 3 sixteenths */
    s_n = 0U; s_off = 0U; s_sounding = 0U; s_max_sounding = 0U;
    Seq_SetPlaying(1U);
    for (s_ms = 1U; s_ms <= ms; s_ms++) { Seq_Tick1ms(); }
    Seq_SetPlaying(0U);

    printf("%s: %u note on, %u note off, max %u at once\n", (mode == SEQ_MODE_SHIFT) ? "SHIFT" : "OVERLAP", s_n, s_off, s_max_sounding);
    for (i = 0U; i < count; i++)
    {
        const unsigned want = expect_16th[i] * 125U;
        const int ok = (i < s_n) && (s_on_ms[i] + 6U >= want) && (s_on_ms[i] <= want + 6U);
        printf("  note %u (%u) at %u ms, expected ~%u ms %s\n", i + 1U, s_on_note[i], s_on_ms[i], want, ok ? "ok" : "FAIL");
        fail += ok ? 0 : 1;
    }
    if ((s_n != s_off) || (s_max_sounding != expect_poly)) { printf("  FAIL: on/off or polyphony\n"); fail++; }
    return fail;
}

int main(void)
{
    /* 8 steps then the first step of the next pass, in sixteenths */
    static const unsigned overlap[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    static const unsigned shift[9]   = {0, 1, 4, 5, 6, 7, 8, 9, 10};
    int fail = 0;

    fail += Run(SEQ_MODE_OVERLAP, overlap, 9U, 1100U, 2U);  /* step 2 sounds over steps 3 and 4 (one at a time) */
    fail += Run(SEQ_MODE_SHIFT, shift, 9U, 1350U, 1U);      /* never two notes at once */
    printf(fail ? "FAILED\n" : "PASSED\n");
    return fail;
}
