/*
 * main.c - BCR32 test firmware: minimal 8 step sequencer with "shift" step lengths
 *
 * Built on the bare-metal demo (no RTOS, image at 0x08010000 started by the BCR32
 * bootloader, 1 ms SysTick main loop). Purpose: try a step length that pushes the
 * following steps later instead of overlapping them (seq.c).
 *
 * Controls
 *   Knobs 1..8      pitch of step 1..8 (semitones)
 *   Knobs 9..16     length of step 1..8 (1..8 sixteenth notes)
 *   SELECT button   mode: SHIFT (steps back to back) / OVERLAP (steps on the 1/16 grid)
 *   SELECT encoder  tempo
 *   T1              play / stop (plays at power up)
 *   T1+T2+T3+T4     bootloader (firmware update), as the demo; SysEx 0x77 as well
 *
 * Outputs
 *   MIDI notes, channel 1: MIDI OUT A, MIDI OUT B, USB port 1 ("BCR-32 CTRL")
 *   CV1 1 V / octave (0 V = note 36), GATE1 5 V while a note sounds
 *
 * OLED: mode, tempo and the last edit on the first line; below it the pattern as a
 * piano roll (x = time over one pattern, y = pitch) with the playhead.
 *
 * Not used here (demo only): encoder ring and key LEDs, SYNC, MIDI IN, flash tests.
 */
#include <stdio.h>
#include <string.h>

#include "gd32f4xx.h"

#include "input.h"
#include "seq.h"
#include "cv_out.h"
#include "activity_led.h"
#include "boot_jump.h"
#include "sysex.h"

#include "bsp_cv.h"
#include "bsp_gpio.h"
#include "bsp_led.h"
#include "bsp_midi_uart.h"
#include "bsp_spi1.h"
#include "bsp_usb.h"
#include "ssd1306.h"
#include "usbd_midi.h"

#define LED_BRIGHTNESS      0x0FU   /* HT16K33 duty 0..15 */
#define MIDI_CHANNEL        0U      /* channel 1 */
#define MIDI_VELOCITY       100U
#define OLED_PERIOD_MS      40U
#define ROLL_TOP            10U     /* piano roll area: y ROLL_TOP..SSD1306_HEIGHT-1 */
#define BAR_HEIGHT          3U
#define QUEUE_SIZE          256U    /* power of two, bytes */
#define CHUNK_MAX           64U

#define KNOB_PITCH_FIRST    1U
#define KNOB_LEN_FIRST      (KNOB_PITCH_FIRST + SEQ_STEPS)

typedef struct
{
    uint8_t  data[QUEUE_SIZE];
    uint32_t head, tail;
} Queue_t;

static volatile uint32_t s_tick_ms;

static Queue_t s_din;           /* MIDI bytes for OUT A and OUT B */
static Queue_t s_usb;           /* USB-MIDI event packets, 4 bytes each */
static uint32_t s_gate_notes;   /* notes sounding now */
static char s_edit[12];         /* last edit, shown top right */
static uint8_t s_oled_dirty = 1U;

void SysTick_Handler(void)
{
    s_tick_ms++;
}

/* all or nothing; a full queue drops the message */
static void QueuePut(Queue_t *q, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if ((QUEUE_SIZE - (q->head - q->tail)) < len)
    {
        return;
    }
    for (i = 0U; i < len; i++)
    {
        q->data[q->head & (QUEUE_SIZE - 1U)] = data[i];
        q->head++;
    }
}

static uint32_t QueueGet(Queue_t *q, uint8_t *data, uint32_t max)
{
    uint32_t n = 0U;

    while ((n < max) && (q->tail != q->head))
    {
        data[n++] = q->data[q->tail & (QUEUE_SIZE - 1U)];
        q->tail++;
    }
    return n;
}

static void MidiNote(uint8_t status, uint8_t note, uint8_t velocity)
{
    const uint8_t din[3] = {(uint8_t)(status | MIDI_CHANNEL), note, velocity};
    const uint8_t usb[4] = {(uint8_t)(status >> 4), din[0], note, velocity};    /* cable 0 */

    QueuePut(&s_din, din, sizeof(din));
    if (IsUsbConnected() != 0U)
    {
        QueuePut(&s_usb, usb, sizeof(usb));
    }
}

/* seq.c outputs: MIDI, CV1 follows the newest note, GATE1 high while any note sounds */
void SeqOut_NoteOn(uint8_t note)
{
    MidiNote(0x90U, note, MIDI_VELOCITY);
    CvOut_SetMv(0U, (((uint32_t)note - SEQ_PITCH_MIN) * 1000U) / 12U);
    s_gate_notes++;
    BspGate_Set(0U, BSP_GATE_5V);
}

void SeqOut_NoteOff(uint8_t note)
{
    MidiNote(0x80U, note, 0U);
    if ((s_gate_notes != 0U) && (--s_gate_notes == 0U))
    {
        BspGate_Set(0U, BSP_GATE_0V);
    }
}

/* Both DIN ports get the same bytes: send only when both are idle */
static void DinPoll(void)
{
    uint8_t buf[CHUNK_MAX];
    uint32_t n;

    if ((BspMidiUart_IsTxBusy(BSP_MIDI_PORT_A) != 0) || (BspMidiUart_IsTxBusy(BSP_MIDI_PORT_B) != 0))
    {
        return;
    }
    n = QueueGet(&s_din, buf, sizeof(buf));
    if (n == 0U)
    {
        return;
    }
    (void)BspMidiUart_Send(BSP_MIDI_PORT_A, buf, n);
    (void)BspMidiUart_Send(BSP_MIDI_PORT_B, buf, n);
    ActivityLed_Trigger(ACTIVITY_LED_MIDI_A, s_tick_ms);
    ActivityLed_Trigger(ACTIVITY_LED_MIDI_B, s_tick_ms);
}

/* Host packets go to the SysEx parser (mode query, bootloader request); its replies and
 * the queued notes go to the host */
static void UsbPoll(void)
{
    static uint8_t buf[CHUNK_MAX + 16U];
    uint32_t n;

    if (IsUsbConnected() == 0U)
    {
        s_usb.tail = s_usb.head;    /* no host: nothing kept for later */
        return;
    }
    n = UsbMidiRxRead(buf, CHUNK_MAX);
    if (n != 0U)
    {
        Sysex_UsbRx(buf, n);
    }
    if (UsbMidiTxBusy() != 0U)
    {
        return;
    }
    n = Sysex_UsbTx(buf, 16U);
    n += QueueGet(&s_usb, &buf[n], CHUNK_MAX);
    if (n != 0U)
    {
        USBTxNBytes(buf, n);
        ActivityLed_Trigger(ACTIVITY_LED_USB, s_tick_ms);
    }
}

static void NoteName(uint8_t note, char *text, uint32_t size)
{
    static const char *const name[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    (void)snprintf(text, size, "%s%d", name[note % 12U], ((int)note / 12) - 1);
}

/* One bar of the piano roll, from tick t0 for len ticks; wraps at the end of the pattern */
static void DrawBar(uint32_t t0, uint32_t len, uint32_t total, uint8_t y)
{
    uint32_t t1 = t0 + len;

    if (t1 > total)
    {
        DrawBar(0U, t1 - total, total, y);
        t1 = total;
    }
    {
        const uint32_t x0 = (t0 * SSD1306_WIDTH) / total;
        uint32_t x1 = (t1 * SSD1306_WIDTH) / total;

        x1 = (x1 > (x0 + 1U)) ? (x1 - 1U) : x0;
        ssd1306_FillRectangle((uint8_t)x0, y, (uint8_t)x1, (uint8_t)(y + BAR_HEIGHT - 1U), White);
    }
}

static void OledDraw(void)
{
    char line[24];
    const uint32_t total = Seq_GetTotalTicks();
    const uint32_t span_y = (SSD1306_HEIGHT - ROLL_TOP) - BAR_HEIGHT;
    uint32_t i, lo = SEQ_PITCH_MAX, hi = SEQ_PITCH_MIN, x;

    ssd1306_Fill(Black);

    (void)snprintf(line, sizeof(line), "%s %3u %c", (Seq_GetMode() == SEQ_MODE_SHIFT) ? "SHIFT" : "OVERL",
                   (unsigned int)Seq_GetTempo(), (Seq_IsPlaying() != 0U) ? '>' : '-');
    ssd1306_SetCursor(0U, 0U);
    (void)ssd1306_WriteString(line, Font_6x8, White);
    ssd1306_SetCursor((uint8_t)(SSD1306_WIDTH - (strlen(s_edit) * Font_6x8.FontWidth)), 0U);
    (void)ssd1306_WriteString(s_edit, Font_6x8, White);

    for (i = 0U; i < SEQ_STEPS; i++)
    {
        const uint32_t p = Seq_GetPitch(i);

        lo = (p < lo) ? p : lo;
        hi = (p > hi) ? p : hi;
    }
    for (i = 0U; i < SEQ_STEPS; i++)
    {
        const uint32_t up = (hi > lo) ? (((Seq_GetPitch(i) - lo) * span_y) / (hi - lo)) : (span_y / 2U);

        DrawBar(Seq_GetStartTicks(i), Seq_GetNoteTicks(i), total, (uint8_t)((ROLL_TOP + span_y) - up));
    }

    if (Seq_IsPlaying() != 0U)
    {
        x = ((Seq_GetPosTicks() % total) * SSD1306_WIDTH) / total;
        ssd1306_Line((uint8_t)x, (uint8_t)ROLL_TOP, (uint8_t)x, (uint8_t)(SSD1306_HEIGHT - 1U), White);
    }
    ssd1306_UpdateScreen();
}

static void OnInputEvent(const InputEvent_t *ev)
{
    char name[6];

    if (ev->type == INPUT_EV_BUTTON)
    {
        if (ev->id == INPUT_BTN_SELECT)
        {
            Seq_SetMode((Seq_GetMode() == SEQ_MODE_SHIFT) ? SEQ_MODE_OVERLAP : SEQ_MODE_SHIFT);
        }
        else if (ev->id == INPUT_BTN_T1)
        {
            Seq_SetPlaying((Seq_IsPlaying() != 0U) ? 0U : 1U);
        }
        else
        {
            (void)snprintf(s_edit, sizeof(s_edit), "BTN%u", ev->id);    /* shows the key numbering */
        }
        s_oled_dirty = 1U;
    }
    else if (ev->type == INPUT_EV_ENCODER)
    {
        if (ev->id == INPUT_ENC_SELECT)
        {
            (void)Seq_AddTempo(ev->delta);
            s_edit[0] = '\0';
        }
        else if ((ev->id >= KNOB_PITCH_FIRST) && (ev->id < KNOB_LEN_FIRST))
        {
            const uint32_t step = ev->id - KNOB_PITCH_FIRST;

            NoteName(Seq_AddPitch(step, ev->delta), name, sizeof(name));
            (void)snprintf(s_edit, sizeof(s_edit), "%u:%s", (unsigned int)(step + 1U), name);
        }
        else if ((ev->id >= KNOB_LEN_FIRST) && (ev->id < (KNOB_LEN_FIRST + SEQ_STEPS)))
        {
            const uint32_t step = ev->id - KNOB_LEN_FIRST;

            (void)snprintf(s_edit, sizeof(s_edit), "%u:LEN%u", (unsigned int)(step + 1U),
                           (unsigned int)Seq_AddLength(step, ev->delta));
        }
        else
        {
            (void)snprintf(s_edit, sizeof(s_edit), "KNOB%u", ev->id);   /* unused knob: its number */
        }
        s_oled_dirty = 1U;
    }
    else
    {
        /* button release: nothing */
    }
}

int main(void)
{
    uint32_t seq_ms, oled_ms = 0U;

    /* SystemInit already ran from Reset_Handler; IRQs are still masked (CPSID i) */
    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);
    (void)SysTick_Config(SystemCoreClock / 1000U);    /* also needed by BspDelay_Us */
    __enable_irq();

    BspClock_Init();
    BspGpio_Init();
    CvOut_Init();       /* ~1 ms busy wait */
    BspMidiUart_Init();
    BspSpi1_Init();
    Oled_Init();        /* reset, SSD1306 setup, blank screen */
    BspUsb_Init();
    BspLedI2c_Init();
    (void)BspLed_Init(LED_BRIGHTNESS);      /* all LEDs off */

    Seq_Init();
    Seq_SetPlaying(1U);
    seq_ms = s_tick_ms;

    for (;;)
    {
        InputEvent_t ev;
        const uint32_t now = s_tick_ms;

        while (seq_ms != now)
        {
            seq_ms++;
            Seq_Tick1ms();
        }
        DinPoll();
        UsbPoll();

        Input_Poll(now);
        while (Input_GetEvent(&ev) != 0)
        {
            OnInputEvent(&ev);
        }

        /* SysEx 0x77, or T1 + T2 + T3 + T4 held: bootloader for a firmware update */
        if ((Sysex_BootRequested() != 0U) ||
            ((Input_IsPressed(INPUT_BTN_T1) != 0U) && (Input_IsPressed(INPUT_BTN_T1 + 1U) != 0U) &&
             (Input_IsPressed(INPUT_BTN_T1 + 2U) != 0U) && (Input_IsPressed(INPUT_BTN_T4) != 0U)))
        {
            Seq_SetPlaying(0U);
            BootJump_Start();
        }

        CvOut_Poll(now);
        ActivityLed_Poll(now);

        /* playhead: redraw at 25 Hz while playing, otherwise only after an edit */
        if (((now - oled_ms) >= OLED_PERIOD_MS) && ((s_oled_dirty != 0U) || (Seq_IsPlaying() != 0U)))
        {
            oled_ms = now;
            s_oled_dirty = 0U;
            OledDraw();
        }
    }
}
