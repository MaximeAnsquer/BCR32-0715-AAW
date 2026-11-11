/*
 * cv_out.c - CV1..4 outputs, see cv_out.h
 */
#include "cv_out.h"
#include "bsp_cv.h"

#define DAC_FULL_SCALE      65535U

static uint32_t s_mv[CV_OUT_NUM];       /* value in mV */
static uint32_t s_next;                 /* channel refreshed next */
static uint32_t s_last_ms;

static uint16_t MvToDac(uint32_t mv)
{
    return (uint16_t)((mv * DAC_FULL_SCALE) / CV_OUT_MAX_MV);
}

void CvOut_Init(void)
{
    BspCv_Init();
}

void CvOut_Poll(uint32_t now_ms)
{
    if (now_ms == s_last_ms)
    {
        return;
    }
    s_last_ms = now_ms;
    BspCv_Write(s_next, MvToDac(s_mv[s_next]));
    s_next = (s_next + 1U) % CV_OUT_NUM;
}

void CvOut_SetMv(uint32_t channel, uint32_t mv)
{
    if (channel < CV_OUT_NUM)
    {
        s_mv[channel] = (mv > CV_OUT_MAX_MV) ? CV_OUT_MAX_MV : mv;
    }
}
