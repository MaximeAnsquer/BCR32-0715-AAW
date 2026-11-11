/*
 * cv_out.h - CV1..4 outputs
 *
 * 0 .. 10 V set in mV. Nominal scale of the
 * BCR32 factory defaults: 0 V = DAC 0, 10 V = DAC 65535; the per unit factory
 * calibration (settings in flash) is not applied. The sample/hold mux needs a
 * refresh: one channel per ms (CvOut_Poll), each channel every 4 ms.
 */
#ifndef CV_OUT_H
#define CV_OUT_H

#include <stdint.h>

#define CV_OUT_NUM          4U
#define CV_OUT_MAX_MV       10000U  /* 10 V */

/* DAC, mux, gates (0 V); all CV 0 V. After BspGpio_Init, SysTick running. */
void CvOut_Init(void);

/* Call from the main loop; refreshes one channel when a new ms started (~150 us) */
void CvOut_Poll(uint32_t now_ms);

/* channel 0..3: output voltage in mV, clamped to 10 V; output on the next refresh */
void CvOut_SetMv(uint32_t channel, uint32_t mv);

#endif /* CV_OUT_H */
