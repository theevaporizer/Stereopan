// ============================================================================
//  Stereo Tool  --  Music Thing Modular Workshop System "Computer" program card
//  Built on Chris Johnson's header-only ComputerCard.h (TomWhitwell/Workshop_Computer)
//
//  ALWAYS ON : Euclidean generator  (Pulse In 1 = clock, Pulse In 2 = reset)
//                Knob X         = length 1..16
//                Knob Y  (+CV2) = hits   1..length
//                Pulse Out 1    = Euclidean triggers (hits)
//                Pulse Out 2    = complement (fires on the rests)
//              No cable in Pulse In 1 -> internal clock, 120 BPM.
//  CV In 1 is added to the MAIN knob (pan / crossfade / rotation).
//
//  SWITCH UP    : Panner.  Audio In 1 only (or only In 2) -> mono equal-power pan.
//                          Both cables -> stereo balance (In1->Out1, In2->Out2).
//                 Main knob: full CCW = Out 1 only, full CW = Out 2 only.
//  SWITCH MIDDLE: Equal-power crossfade In1 <-> In2 (Main knob), sent to Out 1 & 2.
//  SWITCH DOWN  : Main knob ROTATES the Euclidean pattern (0..length-1 steps).
//                 Audio outputs are silent in this position.
//                 The rotation is remembered when you leave this position.
//
//  DSP rules: ProcessSample() uses ONLY 32-bit integer maths (no float/double,
//  no division, no heap). The sine table is built once in the constructor.
// ============================================================================
#include "ComputerCard.h"
#include <cmath>
#include <cstdint>

class StereoTool : public ComputerCard
{
    // ---- constants ---------------------------------------------------------
    static constexpr int32_t  STEPS_PER_BEAT = 1;            // internal clock: steps per beat
    static constexpr int32_t  INT_PERIOD     = (48000 * 60 / 120) / STEPS_PER_BEAT; // 120 BPM
    static constexpr int32_t  TRIG_LEN       = 480;          // 10 ms trigger
    static constexpr int32_t  LED_HOLD       = 4800;         // 100 ms trigger-LED flash
    static constexpr uint32_t CTL_MASK       = 31;           // control rate = 48k/32 = 1.5 kHz

    // ---- state -------------------------------------------------------------
    int16_t  sinLut[65];            // quarter sine, Q15, 64 segments

    // control-rate values (smoothed knobs held <<4 for precision)
    int32_t  smX = 0, smY = 0, smRot = 0;
    int32_t  panTarget = 2048;
    int32_t  smPan = 2048 << 4;     // per-sample smoothed pan
    Switch   sw = Switch::Middle;
    bool     extClock = false, in1Conn = false, in2Conn = false;
    uint32_t ctlCnt = 0;

    // euclid
    int32_t  len = 16, hits = 4, rot = 0;
    uint32_t pattern = 0;
    int32_t  pos = 0;
    bool     resetPending = true;
    int32_t  intCnt = 0;
    int32_t  trigA = 0, trigB = 0, ledA = 0, ledB = 0;

    // meters
    int32_t  pkIn1 = 0, pkIn2 = 0, pkOut1 = 0, pkOut2 = 0;
    uint32_t sampleCnt = 0;

    // ---- helpers -----------------------------------------------------------
    // x: 0..4095 -> sin(x/4095 * pi/2) in Q15 (0..32767), linear interpolation
    inline int32_t QSin(int32_t x) const
    {
        int32_t i = x >> 6, f = x & 63;
        int32_t a = sinLut[i], b = sinLut[i + 1];
        return a + (((b - a) * f) >> 6);
    }
    static inline int32_t Clip(int32_t v) { return v > 2047 ? 2047 : (v < -2048 ? -2048 : v); }
    static inline int32_t Clamp12(int32_t v) { return v < 0 ? 0 : (v > 4095 ? 4095 : v); }
    static inline void Peak(int32_t &pk, int32_t v, bool decay)
    {
        if (v < 0) v = -v;
        if (v >= pk) pk = v; else if (decay && pk > 0) pk--;
    }

    // Euclidean pattern, division-free: step i is a hit when (i*hits mod len) < hits
    void BuildPattern()
    {
        uint32_t p = 0;
        int32_t acc = 0;
        for (int32_t i = 0; i < len; i++)
        {
            if (acc < hits) p |= (1u << i);
            acc += hits;
            if (acc >= len) acc -= len;
        }
        pattern = p;
    }

    // ---- 1.5 kHz control update -------------------------------------------
    void UpdateControl()
    {
        extClock = Connected(Input::Pulse1);
        in1Conn  = Connected(Input::Audio1);
        in2Conn  = Connected(Input::Audio2);
        sw       = SwitchVal();

        int32_t cv1 = Connected(Input::CV1) ? CVIn1() : 0;
        int32_t cv2 = Connected(Input::CV2) ? CVIn2() : 0;

        int32_t kx = KnobVal(Knob::X), ky = KnobVal(Knob::Y);
        int32_t km = Clamp12(KnobVal(Knob::Main) + cv1);   // CV 1 adds to Main knob

        smX += ((kx << 4) - smX) >> 4;
        smY += ((ky << 4) - smY) >> 4;

        if (sw == Switch::Down) smRot += ((km << 4) - smRot) >> 4;   // rotate mode
        else                    panTarget = km;                      // pan / crossfade

        int32_t x = Clamp12(smX >> 4);
        int32_t y = Clamp12((smY >> 4) + cv2);

        int32_t nLen  = 1 + ((x * 16) >> 12);          // 1..16
        int32_t nHits = 1 + ((y * nLen) >> 12);        // 1..nLen
        int32_t nRot  = ((smRot >> 4) * nLen) >> 12;   // 0..nLen-1

        if (nLen != len || nHits != hits)
        {
            len = nLen; hits = nHits;
            BuildPattern();
        }
        rot = nRot;
    }

public:
    StereoTool()
    {
        // quarter-sine table (setup only; float is fine outside ProcessSample)
        for (int i = 0; i < 65; i++)
            sinLut[i] = (int16_t)lroundf(32767.0f * sinf((float)i * 1.57079633f / 64.0f));
        EnableNormalisationProbe();   // needed for Connected() jack detection
        BuildPattern();
    }

    void ProcessSample() override
    {
        sampleCnt++;
        if ((++ctlCnt & CTL_MASK) == 0) UpdateControl();

        // ===================== Euclidean sequencer ==========================
        if (PulseIn2RisingEdge())
        {
            resetPending = true;
            if (!extClock) intCnt = INT_PERIOD;   // restart internal clock immediately
        }

        bool tick = false;
        if (extClock) tick = PulseIn1RisingEdge();
        else if (++intCnt >= INT_PERIOD) { intCnt = 0; tick = true; }

        if (tick)
        {
            if (resetPending) { pos = 0; resetPending = false; }
            else if (++pos >= len) pos = 0;

            int32_t idx = pos + rot;
            if (idx >= len) idx -= len;
            if ((pattern >> idx) & 1u) { trigA = TRIG_LEN; ledA = LED_HOLD; }
            else                       { trigB = TRIG_LEN; ledB = LED_HOLD; }
        }
        PulseOut1(trigA > 0);
        PulseOut2(trigB > 0);
        if (trigA > 0) trigA--;
        if (trigB > 0) trigB--;
        if (ledA > 0) ledA--;
        if (ledB > 0) ledB--;

        // ============================ Audio =================================
        int32_t a1 = in1Conn ? AudioIn1() : 0;
        int32_t a2 = in2Conn ? AudioIn2() : 0;
        int32_t o1 = 0, o2 = 0;

        smPan += ((panTarget << 4) - smPan) >> 8;     // de-zipper (~5 ms)
        int32_t p = smPan >> 4;                       // 0..4095
        bool both = in1Conn && in2Conn;
        int32_t mono = in1Conn ? a1 : a2;

        switch (sw)
        {
        case Switch::Up:
            if (both)
            {   // stereo balance: centre = unity both sides, edges mute the far side
                int32_t gL = (p <= 2048) ? 32767 : QSin((4095 - p) * 2);
                int32_t gR = (p >= 2048) ? 32767 : QSin(p * 2);
                o1 = (a1 * gL) >> 15;
                o2 = (a2 * gR) >> 15;
            }
            else
            {   // mono equal-power pan
                o1 = (mono * QSin(4095 - p)) >> 15;
                o2 = (mono * QSin(p)) >> 15;
            }
            break;

        case Switch::Middle:
            o1 = o2 = (a1 * QSin(4095 - p) + a2 * QSin(p)) >> 15;   // equal-power crossfade
            break;

        default: // Down: audio outputs silent
            o1 = o2 = 0;
            break;
        }
        o1 = Clip(o1); o2 = Clip(o2);
        AudioOut1((int16_t)o1);
        AudioOut2((int16_t)o2);

        // ============================= LEDs =================================
        bool dec = (sampleCnt & 3) == 0;
        Peak(pkIn1, a1, dec);  Peak(pkIn2, a2, dec);
        Peak(pkOut1, o1, dec); Peak(pkOut2, o2, dec);
        LedBrightness(0, (uint16_t)(pkIn1 << 1));
        LedBrightness(1, (uint16_t)(pkIn2 << 1));
        LedBrightness(2, (uint16_t)(pkOut1 << 1));
        LedBrightness(3, (uint16_t)(pkOut2 << 1));
        LedBrightness(4, ledA > 0 ? 4095 : 0);
        LedBrightness(5, ledB > 0 ? 4095 : 0);
    }
};

int main()
{
    StereoTool card;
    card.Run();
}
