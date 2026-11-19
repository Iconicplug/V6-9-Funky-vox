#pragma once
// ============================================================================
//  FunkyVox - motor DSP (C++ puro, sin dependencias de JUCE)
//  Vocoder de 24 bandas con portadora sierra/pulso, gate de entrada y
//  expansor por banda para eliminar el ruido de fondo.
// ============================================================================
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fx
{
constexpr double kPi = 3.14159265358979323846;

inline float onePoleCoef (double sr, double ms)      // coeficiente de suavizado (0..1)
{
    return float (1.0 - std::exp (-1.0 / (sr * ms * 0.001)));
}
inline float decayCoef (double sr, double ms)        // coeficiente de caida exponencial
{
    return float (std::exp (-1.0 / (sr * ms * 0.001)));
}

//==============================================================================
// Biquad (forma transpuesta II, en doble precision para bandas graves estrechas)
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline float process (float xf)
    {
        const double x = xf;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return float (y);
    }

    void setBandpass (double sr, double f, double q)           // ganancia 0 dB en el pico
    {
        const double w = 2.0 * kPi * f / sr, al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0;
        a1 = -2.0 * std::cos (w) / a0; a2 = (1.0 - al) / a0;
    }
    void setHighpass (double sr, double f, double q)
    {
        const double w = 2.0 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    void setPeaking (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kPi * f / sr, c = std::cos (w);
        const double al = std::sin (w) / (2.0 * q), a0 = 1.0 + al / A;
        b0 = (1.0 + al * A) / a0; b1 = -2.0 * c / a0; b2 = (1.0 - al * A) / a0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al / A) / a0;
    }
};

//==============================================================================
// Oscilador sierra + pulso con PolyBLEP (sin aliasing audible)
inline float polyBlep (double t, double dt)
{
    if (t < dt)       { t /= dt;            return float (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return float (t * t + t + t + 1.0); }
    return 0.0f;
}
inline float sawAt (double p, double inc) { return float (2.0 * p - 1.0) - polyBlep (p, inc); }

struct Osc
{
    double ph = 0.0;

    // devuelve sierra y pulso (ancho d), ambos con media cero y pico ~1
    void next (double inc, double d, float& saw, float& pulse)
    {
        double p2 = ph + d; if (p2 >= 1.0) p2 -= 1.0;
        saw   = sawAt (ph, inc);
        pulse = (saw - sawAt (p2, inc)) * float (0.5 / std::max (d, 1.0 - d));
        ph += inc; if (ph >= 1.0) ph -= 1.0;
    }
};

//==============================================================================
// Detector de tono YIN (70 Hz - 800 Hz) - solo analiza cuando se necesita
struct PitchDetector
{
    void prepare (double sampleRate)
    {
        sr     = sampleRate;
        minTau = int (sr / 800.0);
        maxTau = int (sr / 70.0);
        N = 1; while (N < W + maxTau + 8) N <<= 1;
        buf.assign (size_t (N), 0.0f);
        tmp.assign (size_t (W + maxTau), 0.0f);
        cm.assign (size_t (maxTau + 2), 1.0f);
        w = 0; count = 0; valid = false; freq = 0; fresh = false;
    }

    void setActive (bool a) { active = a; }

    void push (float x)
    {
        buf[size_t (w)] = x;
        w = (w + 1) & (N - 1);
        if (++count >= hop) { count = 0; if (active) analyse(); else { fresh = false; valid = false; } }
    }

    bool consume (float& f)       // true una vez por cada analisis nuevo y valido
    {
        if (! fresh) return false;
        fresh = false;
        if (! valid) return false;
        f = freq;
        return true;
    }

private:
    void analyse()
    {
        const int L = W + maxTau;
        for (int i = 0; i < L; ++i)
            tmp[size_t (i)] = buf[size_t ((w - L + i + 2 * N) & (N - 1))];

        float e = 0;
        for (int i = L - W; i < L; ++i) e += tmp[size_t (i)] * tmp[size_t (i)];
        e /= float (W);
        fresh = true;
        if (e < 1.0e-5f) { valid = false; return; }

        float running = 0;
        cm[0] = 1.0f;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            float d = 0;
            for (int j = 0; j < W; ++j)
            {
                const float df = tmp[size_t (j)] - tmp[size_t (j + tau)];
                d += df * df;
            }
            running += d;
            cm[size_t (tau)] = running > 0 ? d * float (tau) / running : 1.0f;
        }

        int t = -1;
        for (int tau = minTau; tau < maxTau; ++tau)
            if (cm[size_t (tau)] < 0.15f)
            {
                while (tau + 1 < maxTau && cm[size_t (tau + 1)] < cm[size_t (tau)]) ++tau;
                t = tau; break;
            }
        if (t < 2) { valid = false; return; }

        const float a = cm[size_t (t - 1)], b = cm[size_t (t)], c = cm[size_t (t + 1)];
        const float den = a - 2 * b + c;
        const float shift = std::abs (den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
        freq  = float (sr) / (float (t) + shift);
        valid = true;
    }

    static constexpr int W = 1024, hop = 512;
    double sr = 44100;
    int minTau = 50, maxTau = 600, N = 2048, w = 0, count = 0;
    std::vector<float> buf, tmp, cm;
    float freq = 0;
    bool valid = false, fresh = false, active = true;
};

//==============================================================================
//  Motor completo
//
//  Portadora (por prioridad):
//    1) audio del sidechain (sintetizador de otra pista de Ableton), si suena
//    2) notas MIDI sostenidas (sintetizador interno de hasta 8 voces)
//    3) seguimiento del tono de la propia voz (sin correccion de escala)
//
class Engine
{
public:
    static constexpr int kVoices = 8;
    static constexpr int kBands  = 24;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        pitch.prepare (sr);

        // ---- banco de bandas (logaritmico 90 Hz - 7.8 kHz)
        const double lo = 90.0, hi = std::min (7800.0, sr * 0.42);
        for (int i = 0; i < kBands; ++i)
        {
            const double f = lo * std::pow (hi / lo, (i + 0.5) / kBands);
            const double q = 4.2;
            for (int k = 0; k < 2; ++k)
            {
                modF[size_t (i)][size_t (k)].setBandpass (sr, f, q);
                carF[size_t (i)][size_t (k)].setBandpass (sr, f, q);
            }
            // el filtro de portadora y modulador coinciden; compensa la caida de la sierra
            weight[size_t (i)] = float (std::clamp (std::pow (f / 250.0, tiltExp), 0.55, 6.0));
            const double relMs = std::clamp (40.0 * std::sqrt (200.0 / f), 12.0, 38.0);
            relC[size_t (i)] = decayCoef (sr, relMs);
        }
        atkC = decayCoef (sr, 1.5);

        modHP.setHighpass (sr, 80.0, 0.7071);
        sibHP[0].setHighpass (sr, 5200.0, 0.7071);
        sibHP[1].setHighpass (sr, 5200.0, 0.7071);
        outHP.setHighpass (sr, 90.0, 0.7071);
        outPeak.setPeaking (sr, 2300.0, 0.8, 3.0);     // timbre nasal tipo talkbox
        outAir.setPeaking (sr, 900.0, 0.9, 1.5);

        glideSet = -1.0f;
        gateAtk = onePoleCoef (sr, 2.0);  gateRelC = onePoleCoef (sr, 45.0);
        gateEnvAtk = onePoleCoef (sr, 1.5); gateEnvRel = onePoleCoef (sr, 30.0);
        voiceGainAtk = onePoleCoef (sr, 3.0); voiceGainRel = onePoleCoef (sr, 22.0);
        paramC = onePoleCoef (sr, 15.0);  octC = onePoleCoef (sr, 8.0);
        extC = onePoleCoef (sr, 6.0);
        agcAtk = decayCoef (sr, 0.4); agcRel = decayCoef (sr, 120.0);
        agcDown = onePoleCoef (sr, 3.0); agcUp = onePoleCoef (sr, 700.0);
        sibAtk = decayCoef (sr, 2.0); sibRel = decayCoef (sr, 18.0);
        totRel = decayCoef (sr, 18.0);

        for (size_t v = 0; v < size_t (kVoices); ++v)
            for (size_t o = 0; o < 3; ++o)
                voices[v].osc[o].ph = std::fmod (0.137 * double (v * 3 + o + 1), 1.0);
        follow.osc[0].ph = 0.1; follow.osc[1].ph = 0.43; follow.osc[2].ph = 0.77;
        reset();
    }

    void reset()
    {
        for (auto& b : modF) for (auto& f : b) f.reset();
        for (auto& b : carF) for (auto& f : b) f.reset();
        env.fill (0.0f);
        modHP.reset(); sibHP[0].reset(); sibHP[1].reset(); outHP.reset(); outPeak.reset(); outAir.reset();
        gateEnv = gateGain = 0; gateOpen = false; gateHold = 0;
        for (auto& v : voices) { v.gain = 0; v.gate = false; v.key = -1; }
        follow = Voice(); follow.osc[1].ph = 0.43; follow.osc[2].ph = 0.77;
        held.clear();
        extHold = 0; extMix = 0; agcPeak = 0; agcGain = 1.0f;
        sibEnvV = totEnv = 0;
        followHold = 0; med = { 60, 60, 60 };
        lastNote = 60; haveLast = false;
        ageCounter = 0;
        pulseSm = pulseTgt; mixSm = mixTgt; octSm = 12.0f * float (octave);
    }

    //----------------------------------------------------------------------
    void setParams (int octaveIn, float pulse, float glideMs, float mix)
    {
        octave   = std::clamp (octaveIn, -2, 2);
        pulseTgt = std::clamp (pulse, 0.0f, 1.0f);
        mixTgt   = std::clamp (mix, 0.0f, 1.0f);
        if (glideMs != glideSet)
        {
            glideSet  = glideMs;
            glideCoef = glideMs < 0.5f ? 1.0f : onePoleCoef (sr, glideMs);
        }
    }

    void noteOn (int note)
    {
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        held.push_back (note);

        Voice* v = nullptr;
        for (auto& x : voices) if (x.key == note) { v = &x; break; }
        if (v == nullptr)
            for (auto& x : voices) if (! x.gate && x.gain < 0.002f) { v = &x; break; }
        if (v == nullptr)                                      // robar la mas antigua
        {
            v = &voices[0];
            for (auto& x : voices) if (x.age < v->age) v = &x;
        }
        v->key = note; v->gate = true; v->age = ++ageCounter;
        v->target = float (note);
        v->note = (glideCoef < 1.0f && haveLast) ? lastNote : float (note);
        lastNote = float (note); haveLast = true;
    }

    void noteOff (int note)
    {
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        for (auto& v : voices) if (v.key == note) { v.gate = false; v.key = -1; }
    }

    void allNotesOff()
    {
        held.clear();
        for (auto& v : voices) { v.gate = false; v.key = -1; }
    }

    //----------------------------------------------------------------------
    // mod: voz (mono). sc: sidechain mono (puede ser nullptr). out: salida mono.
    void process (const float* mod, const float* sc, float* out, int n)
    {
        const double pwid = 0.36;                             // ancho de pulso nasal
        const float  det  = float (std::exp2 (8.0 / 1200.0));  // +-8 cents

        for (int i = 0; i < n; ++i)
        {
            pulseSm += (pulseTgt - pulseSm) * paramC;
            mixSm   += (mixTgt   - mixSm)   * paramC;
            octSm   += (12.0f * float (octave) - octSm) * octC;

            const float dryIn = mod[i];
            const float m = modHP.process (dryIn);

            // ---------------- puerta de ruido (gate con histeresis y hold)
            const float am = std::abs (m);
            gateEnv += (am - gateEnv) * (am > gateEnv ? gateEnvAtk : gateEnvRel);
            if (! gateOpen && gateEnv > kGateOpen)  { gateOpen = true; gateHold = int (0.09 * sr); }
            else if (gateOpen)
            {
                if (gateEnv > kGateClose) gateHold = int (0.09 * sr);
                else if (--gateHold <= 0) gateOpen = false;
            }
            gateGain += ((gateOpen ? 1.0f : 0.0f) - gateGain) * (gateOpen ? gateAtk : gateRelC);
            const float mg = m * gateGain;

            // ---------------- sidechain presente?
            float s = 0.0f;
            if (sc != nullptr)
            {
                s = sc[i];
                if (std::abs (s) > 0.003f) extHold = int (0.5 * sr);
                else if (extHold > 0) --extHold;
            }
            else if (extHold > 0) --extHold;
            const bool extOn = extHold > 0;
            extMix += ((extOn ? 1.0f : 0.0f) - extMix) * extC;

            // ---------------- seguimiento de tono (solo si hace falta)
            const bool needFollow = held.empty() && ! extOn;
            pitch.setActive (needFollow);
            pitch.push (m);
            float f0;
            if (pitch.consume (f0))
            {
                const float mn = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
                med[0] = med[1]; med[1] = med[2]; med[2] = mn;
                const float md = std::max (std::min (med[0], med[1]), std::min (std::max (med[0], med[1]), med[2]));
                if (md > 33.0f && md < 90.0f)
                {
                    follow.target = md;
                    followHold = int (0.14 * sr);
                }
            }
            else if (followHold > 0) --followHold;

            // ---------------- portadora interna
            float carrier = 0.0f;
            if (extMix < 0.999f)
            {
                int active = 0;
                for (auto& v : voices) if (v.gain > 0.01f) ++active;
                const bool followOn = needFollow && followHold > 0 && gateOpen;
                if (followOn && follow.gain < 0.002f) follow.note = follow.target;
                if (followOn || follow.gain > 0.01f) ++active;
                const float norm = 1.0f / std::sqrt (float (std::max (1, active)));

                for (auto& v : voices)
                {
                    v.gain += ((v.gate ? 1.0f : 0.0f) - v.gain) * (v.gate ? voiceGainAtk : voiceGainRel);
                    if (v.gain < 0.0005f && ! v.gate) { v.gain = 0; continue; }
                    v.note += (v.target - v.note) * glideCoef;
                    carrier += renderVoice (v, v.note + octSm, pwid, det) * v.gain * norm;
                }
                follow.gate = followOn;
                follow.gain += ((followOn ? 1.0f : 0.0f) - follow.gain) * (followOn ? voiceGainAtk : voiceGainRel);
                if (follow.gain > 0.0005f)
                {
                    follow.note += (follow.target - follow.note) * glideCoef;
                    carrier += renderVoice (follow, follow.note + octSm, pwid, det) * follow.gain * norm;
                }
            }

            // ---------------- portadora externa con nivelacion lenta (AGC)
            if (extMix > 0.001f)
            {
                const float as = std::abs (s);
                agcPeak = as > agcPeak ? agcPeak + (as - agcPeak) * (1.0f - agcAtk) : agcPeak * agcRel;
                if (agcPeak > 0.002f)
                {
                    const float gt = std::clamp (0.9f / agcPeak, 0.25f, 8.0f);
                    agcGain += (gt - agcGain) * (gt < agcGain ? agcDown : agcUp);
                }
                carrier = carrier * (1.0f - extMix) + s * agcGain * extMix;
            }

            // ---------------- banco de bandas del vocoder
            float sum = 0.0f;
            for (size_t b = 0; b < size_t (kBands); ++b)
            {
                const float mb = modF[b][1].process (modF[b][0].process (mg));
                const float cb = carF[b][1].process (carF[b][0].process (carrier));
                const float r  = std::abs (mb);
                float e = env[b];
                e = r > e ? atkC * e + (1.0f - atkC) * r : relC[b] * e + (1.0f - relC[b]) * r;
                env[b] = e;
                float amp = e * 1.5708f;                        // promedio rectificado -> amplitud pico
                amp = amp * amp / (amp + kKnee);               // expansor descendente (mata el ruido residual)
                sum += cb * weight[b] * amp;
            }

            // ---------------- consonantes: paso natural de la voz solo en fricativas (s, t, ch)
            const float hf = sibHP[1].process (sibHP[0].process (mg));
            const float ah = std::abs (hf), at = std::abs (mg);
            sibEnvV = ah > sibEnvV ? sibEnvV + (ah - sibEnvV) * (1.0f - sibAtk) : sibEnvV * sibRel;
            totEnv  = at > totEnv ? at : totEnv * totRel;
            const float ratio = sibEnvV / (totEnv + 1.0e-6f);
            const float sibAmt = smooth01 ((ratio - 0.30f) / 0.35f);
            float wet = sum * kWetGain + hf * sibAmt * kSibGain;

            // ---------------- acabado
            wet = outHP.process (wet);
            wet = outAir.process (outPeak.process (wet));
            wet = softClip (wet);

            const float a = float (kPi * 0.5) * mixSm;
            out[i] = std::cos (a) * dryIn + std::sin (a) * wet;     // mezcla de potencia constante
        }
    }

private:
    struct Voice
    {
        Osc osc[3];
        float note = 60.0f, target = 60.0f, gain = 0.0f;
        bool gate = false;
        int key = -1;
        uint32_t age = 0;
    };

    static float smooth01 (float x) { x = std::clamp (x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }

    static float softClip (float x)
    {
        const float ax = std::abs (x), k = 0.6f;
        if (ax <= k) return x;
        const float y = k + (1.0f - k) * std::tanh ((ax - k) / (1.0f - k));
        return x < 0 ? -y : y;
    }

    float renderVoice (Voice& v, float noteWithOct, double pw, float det)
    {
        const double inc = std::min (0.45, 440.0 * std::exp2 ((double (noteWithOct) - 69.0) / 12.0) / sr);
        const double incs[3] = { inc, inc * det, inc / det };
        static const float wts[3] = { 1.0f, 0.5f, 0.5f };
        float acc = 0.0f;
        for (int o = 0; o < 3; ++o)
        {
            float s, p;
            v.osc[o].next (incs[o], pw, s, p);
            acc += wts[o] * (s * (1.0f - pulseSm) + p * pulseSm);
        }
        return acc * 0.5f;
    }

    // ----- constantes de ajuste
    static constexpr float kGateOpen  = 0.0035f;   // ~ -49 dBFS
    static constexpr float kGateClose = 0.0018f;   // ~ -55 dBFS
    static constexpr float kKnee      = 0.0008f;   // expansor por banda
    static constexpr float  kWetGain = 15.0f;   // nivela la salida del vocoder con la voz de entrada
    static constexpr float  kSibGain = 0.7f;    // consonantes (s, t, ch) naturales
    static constexpr double tiltExp  = 0.5;     // compensa la caida de la sierra hacia los agudos

    double sr = 44100.0;
    int octave = 0;
    float pulseTgt = 0.25f, mixTgt = 1.0f, pulseSm = 0.25f, mixSm = 1.0f, octSm = 0.0f;
    float glideSet = -1.0f, glideCoef = 1.0f;

    std::array<std::array<Biquad, 2>, kBands> modF, carF;
    std::array<float, kBands> env {}, weight {}, relC {};
    float atkC = 0.9f;
    Biquad modHP, sibHP[2], outHP, outPeak, outAir;

    std::array<Voice, kVoices> voices;
    Voice follow;
    std::vector<int> held;
    float lastNote = 60.0f; bool haveLast = false; uint32_t ageCounter = 0;

    PitchDetector pitch;
    std::array<float, 3> med { 60, 60, 60 };
    int followHold = 0;

    float gateEnv = 0, gateGain = 0, gateAtk = 0.1f, gateRelC = 0.01f, gateEnvAtk = 0.1f, gateEnvRel = 0.01f;
    bool gateOpen = false; int gateHold = 0;
    float voiceGainAtk = 0.1f, voiceGainRel = 0.01f, paramC = 0.01f, octC = 0.01f, extC = 0.01f;

    int extHold = 0; float extMix = 0.0f;
    float agcPeak = 0, agcGain = 1.0f, agcAtk = 0.9f, agcRel = 0.99f, agcDown = 0.01f, agcUp = 0.0001f;

    float sibEnvV = 0, totEnv = 0, sibAtk = 0.9f, sibRel = 0.99f, totRel = 0.99f;
};
} // namespace fx
