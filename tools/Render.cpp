// ===========================================================================
//  whysho SY1 — headless render / QA tool (dev only, never shipped).
//
//    Render --out file.wav [--preset "Bank/Preset"] [--set id=value ...]
//           [--notes 36,48,60] [--vel 100] [--len 1.0] [--tail 2.0]
//           [--seq "note:startSec:lenSec[:vel],..."] [--bpm 140] [--sr 48000]
//           [--block 512] [--json]
//    Render --list-presets
//    Render --all-presets <dir> [--sr ..] [--bpm ..] [--json]
//    Render --list-params
//    Render --bench            CPU numbers for typical / worst-case patches
//    Render --selftest         engine test-suite, exits non-zero on failure
//
//  --set takes raw parameter values (Hz, s, dB, 0..1 ...). Choice parameters
//  also accept the choice name: --set osc1_wave=SuperSaw --set flt_type="LP 24".
// ===========================================================================
#include "PluginProcessor.h"
#include "dsp/SynthEngine.h"
#include "dsp/Diagnostics.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <iomanip>

using namespace wsho;

namespace
{
// ---------------------------------------------------------------------------
//  Phrase description + rendered audio
// ---------------------------------------------------------------------------
struct NoteEv { int note = 60; double start = 0.0, len = 1.0; int vel = 100; };

struct Phrase
{
    std::vector<NoteEv> notes;
    std::vector<std::pair<double, juce::MidiMessage>> extra;      // CCs, bends ... at absolute seconds
    double tail = 2.0;

    double notesEnd() const
    {
        double e = 0.0;
        for (auto& n : notes) e = juce::jmax (e, n.start + n.len);
        return e;
    }
    double length() const { return notesEnd() + tail; }
};

struct Audio
{
    std::vector<float> l, r;
    double sr = 48000.0, cpuSeconds = 0.0;
    int maxVoices = 0, endVoices = 0;
    int size() const { return (int) l.size(); }
    double seconds() const { return (double) l.size() / sr; }
    std::vector<float> mono() const
    {
        std::vector<float> m (l.size());
        for (size_t i = 0; i < l.size(); ++i) m[i] = 0.5f * (l[i] + r[i]);
        return m;
    }
};

struct TimedMidi { int pos; int order; juce::MidiMessage msg; };

std::vector<TimedMidi> buildMidi (const Phrase& ph, double sr)
{
    std::vector<TimedMidi> ev;
    for (auto& n : ph.notes)
    {
        ev.push_back ({ (int) std::llround (n.start * sr), 2, juce::MidiMessage::noteOn (1, n.note, (juce::uint8) juce::jlimit (1, 127, n.vel)) });
        ev.push_back ({ (int) std::llround ((n.start + n.len) * sr), 0, juce::MidiMessage::noteOff (1, n.note) });
    }
    for (auto& e : ph.extra) ev.push_back ({ (int) std::llround (e.first * sr), 1, e.second });
    std::stable_sort (ev.begin(), ev.end(), [] (const TimedMidi& a, const TimedMidi& b)
                      { return a.pos != b.pos ? a.pos < b.pos : a.order < b.order; });
    return ev;
}

// Generic block loop shared by the engine-direct and the processor renders.
template <typename ProcessFn>
Audio renderLoop (const Phrase& ph, double sr, int block, ProcessFn&& processBlock)
{
    Audio out;
    out.sr = sr;
    const int total = (int) std::ceil (ph.length() * sr);
    out.l.resize ((size_t) total); out.r.resize ((size_t) total);
    const auto events = buildMidi (ph, sr);
    size_t next = 0;
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true);
        buf.clear();
        midi.clear();
        while (next < events.size() && events[next].pos < pos + n)
        {
            midi.addEvent (events[next].msg, juce::jmax (0, events[next].pos - pos));
            ++next;
        }
        const auto t0 = juce::Time::getHighResolutionTicks();
        const int voices = processBlock (buf, midi, pos);
        out.cpuSeconds += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        out.maxVoices = juce::jmax (out.maxVoices, voices);
        out.endVoices = voices;
        std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + n, out.l.begin() + pos);
        std::copy (buf.getReadPointer (1), buf.getReadPointer (1) + n, out.r.begin() + pos);
    }
    return out;
}

// ---- raw parameter set for engine-direct rendering -------------------------
struct Patch
{
    std::array<float, (size_t) kNumParams> v {};
    Patch() { for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = paramDef (i).def; }
    float& operator[] (P p) { return v[(size_t) (int) p]; }
    Patch& set (P p, float x) { v[(size_t) (int) p] = x; return *this; }
};

using BlockHook = std::function<void (Patch&, int samplePos)>;

Audio renderEngine (dsp::SynthEngine& eng, Patch patch, const Phrase& ph, double sr = 48000.0, int block = 512,
                    double bpm = 120.0, const BlockHook& hook = {}, bool prepare = true)
{
    if (prepare) eng.prepare (sr, block);
    return renderLoop (ph, sr, block, [&] (juce::AudioBuffer<float>& b, juce::MidiBuffer& m, int pos)
    {
        if (hook) hook (patch, pos);
        eng.process (b, m, patch.v.data(), bpm);
        return eng.activeVoices();
    });
}

Audio renderEngine (Patch patch, const Phrase& ph, double sr = 48000.0, int block = 512, double bpm = 120.0,
                    const BlockHook& hook = {})
{
    dsp::SynthEngine eng;
    return renderEngine (eng, patch, ph, sr, block, bpm, hook);
}

struct FixedTempoHead : juce::AudioPlayHead
{
    double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setBpm (bpm);
        i.setIsPlaying (true);
        return i;
    }
};

Audio renderProcessor (WhyshoSynthAudioProcessor& proc, const Phrase& ph, double sr, int block, double bpm)
{
    FixedTempoHead head;
    head.bpm = bpm;
    proc.setPlayHead (&head);
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);
    auto a = renderLoop (ph, sr, block, [&] (juce::AudioBuffer<float>& b, juce::MidiBuffer& m, int)
    {
        proc.processBlock (b, m);
        return proc.activeVoices();
    });
    proc.setPlayHead (nullptr);
    return a;
}

// ---------------------------------------------------------------------------
//  Analysis
// ---------------------------------------------------------------------------
double toDb (double x) { return x > 1.0e-12 ? 20.0 * std::log10 (x) : -240.0; }

// seconds → sample index, clamped (t may be "infinity")
int sampleAt (const Audio& a, double t, int lo = 0)
{
    return (int) juce::jlimit ((double) lo, (double) a.size(), t * a.sr);
}

double peakOf (const float* x, int n)
{
    float p = 0.0f;
    for (int i = 0; i < n; ++i) p = juce::jmax (p, std::abs (x[i]));
    return p;
}
double peakOf (const Audio& a, double t0 = 0.0, double t1 = 1.0e9)
{
    const int i0 = sampleAt (a, t0), i1 = sampleAt (a, t1, i0);
    return juce::jmax (peakOf (a.l.data() + i0, i1 - i0), peakOf (a.r.data() + i0, i1 - i0));
}
double rmsOf (const Audio& a, double t0 = 0.0, double t1 = 1.0e9)
{
    const int i0 = sampleAt (a, t0), i1 = sampleAt (a, t1, i0);
    double s = 0.0;
    for (int i = i0; i < i1; ++i) s += (double) a.l[(size_t) i] * a.l[(size_t) i] + (double) a.r[(size_t) i] * a.r[(size_t) i];
    return i1 > i0 ? std::sqrt (s / (2.0 * (i1 - i0))) : 0.0;
}
int countNonFinite (const Audio& a)
{
    int c = 0;
    for (size_t i = 0; i < a.l.size(); ++i) c += (std::isfinite (a.l[i]) && std::isfinite (a.r[i])) ? 0 : 1;
    return c;
}

// YIN pitch detector (coarse). Returns 0 when nothing periodic is found.
double yin (const float* x, int n, double sr, double fmin = 20.0, double fmax = 6000.0)
{
    const int tauMax = juce::jmin (n / 2, (int) (sr / fmin));
    const int tauMin = juce::jmax (2, (int) (sr / fmax));
    const int w = n - tauMax;
    if (w < 16 || tauMax <= tauMin + 2) return 0.0;

    std::vector<double> d ((size_t) tauMax + 1, 0.0);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double s = 0.0;
        for (int i = 0; i < w; ++i) { const double e = (double) x[i] - (double) x[i + tau]; s += e * e; }
        d[(size_t) tau] = s;
    }
    double running = 0.0;
    d[0] = 1.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += d[(size_t) tau];
        d[(size_t) tau] = running > 0.0 ? d[(size_t) tau] * tau / running : 1.0;
    }
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
        if (d[(size_t) tau] < 0.12)
        {
            while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        best = tauMin;
        for (int tau = tauMin; tau < tauMax; ++tau) if (d[(size_t) tau] < d[(size_t) best]) best = tau;
        if (d[(size_t) best] > 0.45) return 0.0;
    }
    double t = best;
    if (best > 1 && best < tauMax)
    {
        const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const double den = a - 2.0 * b + c;
        if (std::abs (den) > 1.0e-12) t += 0.5 * (a - c) / den;
    }
    return sr / t;
}

// Hann-windowed single-bin DFT magnitude at an arbitrary frequency.
double dftMag (const float* x, int n, double sr, double f)
{
    const double w = juce::MathConstants<double>::twoPi * f / sr;
    const double cw = std::cos (w), sw = std::sin (w);
    double c = 1.0, s = 0.0, re = 0.0, im = 0.0;
    const double hw = juce::MathConstants<double>::twoPi / (double) (n - 1);
    for (int i = 0; i < n; ++i)
    {
        const double v = x[i] * (0.5 - 0.5 * std::cos (hw * i));
        re += v * c; im -= v * s;
        const double c2 = c * cw - s * sw;
        s = s * cw + c * sw; c = c2;
    }
    return std::sqrt (re * re + im * im);
}

// Fundamental in Hz: YIN for the octave, then a spectral peak search for precision.
double estimateF0 (const float* x, int n, double sr, double hint = 0.0)
{
    double f = hint > 0.0 ? hint : yin (x, juce::jmin (n, 16384), sr);
    if (f <= 0.0) return 0.0;
    const double span = juce::jmin (0.03 * f, 1.2 * sr / (double) n);
    double lo = f - span, hi = f + span;
    const double gr = 0.6180339887498949;
    double a = hi - gr * (hi - lo), b = lo + gr * (hi - lo);
    double fa = dftMag (x, n, sr, a), fb = dftMag (x, n, sr, b);
    for (int it = 0; it < 40; ++it)
    {
        if (fa < fb) { lo = a; a = b; fa = fb; b = lo + gr * (hi - lo); fb = dftMag (x, n, sr, b); }
        else         { hi = b; b = a; fb = fa; a = hi - gr * (hi - lo); fa = dftMag (x, n, sr, a); }
    }
    return 0.5 * (lo + hi);
}

double f0At (const Audio& a, double t0, double t1, double hint = 0.0)
{
    const auto m = a.mono();
    const int i0 = sampleAt (a, t0), i1 = sampleAt (a, t1, i0);
    if (i1 - i0 < 256) return 0.0;
    return estimateF0 (m.data() + i0, i1 - i0, a.sr, hint);
}
double yinAt (const Audio& a, double t0, double t1)
{
    const auto m = a.mono();
    const int i0 = sampleAt (a, t0), i1 = sampleAt (a, t1, i0);
    return i1 - i0 < 256 ? 0.0 : yin (m.data() + i0, i1 - i0, a.sr);
}
double cents (double f, double ref) { return (f > 0.0 && ref > 0.0) ? 1200.0 * std::log2 (f / ref) : 1.0e9; }

// Magnitude spectrum (Blackman-Harris, 2^order points) starting at sample i0.
std::vector<float> spectrum (const std::vector<float>& x, int i0, int order)
{
    const int n = 1 << order;
    std::vector<float> buf ((size_t) (2 * n), 0.0f);
    for (int i = 0; i < n && i0 + i < (int) x.size(); ++i)
    {
        const double p = juce::MathConstants<double>::twoPi * i / (double) (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos (p) + 0.14128 * std::cos (2.0 * p) - 0.01168 * std::cos (3.0 * p);
        buf[(size_t) i] = (float) (x[(size_t) (i0 + i)] * w);
    }
    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (buf.data());
    buf.resize ((size_t) (n / 2 + 1));
    return buf;
}

double spectralCentroid (const std::vector<float>& mono, int i0, int len, double sr)
{
    int order = 10;
    while ((1 << (order + 1)) <= len && order < 15) ++order;
    if ((1 << order) > len) return 0.0;
    const auto mag = spectrum (mono, i0, order);
    const double binHz = sr / (double) (1 << order);
    double num = 0.0, den = 0.0;
    for (size_t k = 1; k < mag.size(); ++k) { num += mag[k] * (double) k * binHz; den += mag[k]; }
    return den > 1.0e-9 ? num / den : 0.0;
}

// Power outside the harmonics of f0 relative to the total, in dB.
double inharmonicDb (const Audio& a, double t0, double f0)
{
    const int order = 16, n = 1 << order;
    const auto mono = a.mono();
    const int i0 = (int) (t0 * a.sr);
    if (i0 + n > (int) mono.size() || f0 <= 0.0) return 0.0;
    const auto mag = spectrum (mono, i0, order);
    const double binHz = a.sr / (double) n;
    double harm = 0.0, rest = 0.0;
    for (size_t k = 0; k < mag.size(); ++k)
    {
        const double f = (double) k * binHz, p = (double) mag[k] * mag[k];
        if (f < 15.0) continue;                               // DC / window skirt
        const double h = std::round (f / f0);
        const bool nearHarmonic = h >= 1.0 && std::abs (f - h * f0) <= 8.0 * binHz + h * f0 * 2.0e-5;
        (nearHarmonic ? harm : rest) += p;
    }
    return 10.0 * std::log10 ((rest + 1.0e-30) / (harm + 1.0e-30));
}

// Largest second difference — a hard discontinuity (click) shows up as a spike.
double maxSecondDiff (const Audio& a, double t0 = 0.0, double t1 = 1.0e9)
{
    const int i0 = sampleAt (a, t0, 2), i1 = sampleAt (a, t1, i0);
    double m = 0.0;
    for (int i = i0; i < i1; ++i)
    {
        m = juce::jmax (m, (double) std::abs (a.l[(size_t) i] - 2.0f * a.l[(size_t) i - 1] + a.l[(size_t) i - 2]));
        m = juce::jmax (m, (double) std::abs (a.r[(size_t) i] - 2.0f * a.r[(size_t) i - 1] + a.r[(size_t) i - 2]));
    }
    return m;
}

// Seconds after `from` at which the output last exceeds `level` (-1 if it is still above at the end).
double timeToFallBelow (const Audio& a, double from, double level)
{
    const int i0 = sampleAt (a, from);
    int last = i0 - 1;
    for (int i = a.size() - 1; i >= i0; --i)
        if (std::abs (a.l[(size_t) i]) > level || std::abs (a.r[(size_t) i]) > level) { last = i; break; }
    if (last >= a.size() - 2) return -1.0;
    return (double) (last + 1 - i0) / a.sr;
}

struct Stats
{
    double peakDb = -240, rmsDb = -240, dc = 0, f0 = 0, centroid = 0, tail = 0, speed = 0, seconds = 0;
    int nonFinite = 0, maxVoices = 0;
    bool tailCut = false;
};

Stats analyse (const Audio& a, const Phrase& ph)
{
    Stats s;
    s.seconds = a.seconds();
    s.nonFinite = countNonFinite (a);
    s.peakDb = toDb (peakOf (a));
    const double end = ph.notesEnd();
    s.rmsDb = toDb (rmsOf (a, 0.0, end > 0.0 ? end : 1.0e9));
    double dc = 0.0;
    for (size_t i = 0; i < a.l.size(); ++i) dc += 0.5 * ((double) a.l[i] + a.r[i]);
    s.dc = a.l.empty() ? 0.0 : dc / (double) a.l.size();
    s.maxVoices = a.maxVoices;
    s.speed = a.cpuSeconds > 0.0 ? a.seconds() / a.cpuSeconds : 0.0;

    if (! ph.notes.empty())
    {
        // sustain portion = the back half of the longest note that sounds alone
        // (a chord has no single fundamental); fall back to the longest note.
        auto overlaps = [&ph] (const NoteEv& n)
        {
            for (auto& o : ph.notes)
                if (&o != &n && o.start < n.start + n.len && n.start < o.start + o.len) return true;
            return false;
        };
        auto longest = ph.notes.front();
        bool haveSolo = false;
        for (auto& n : ph.notes)
        {
            const bool solo = ! overlaps (n);
            if ((solo && ! haveSolo) || (solo == haveSolo && n.len > longest.len)) { longest = n; haveSolo = solo; }
        }
        const double t0 = longest.start + 0.4 * longest.len, t1 = longest.start + 0.95 * longest.len;
        const auto mono = a.mono();
        const int i0 = sampleAt (a, t0), i1 = sampleAt (a, t1, i0);
        if (i1 - i0 >= 1024)
        {
            s.f0 = estimateF0 (mono.data() + i0, juce::jmin (i1 - i0, 32768), a.sr);
            s.centroid = spectralCentroid (mono, i0, i1 - i0, a.sr);
        }
        const double t = timeToFallBelow (a, end, 0.001);
        s.tailCut = t < 0.0;
        s.tail = t < 0.0 ? a.seconds() - end : t;
    }
    return s;
}

juce::String statsLine (const Stats& s)
{
    juce::String t;
    t << "peak " << juce::String (s.peakDb, 2) << " dBFS | rms " << juce::String (s.rmsDb, 2) << " dBFS | dc "
      << juce::String (s.dc, 5) << " | f0 " << juce::String (s.f0, 2) << " Hz | centroid " << juce::String (juce::roundToInt (s.centroid))
      << " Hz | tail(-60dB) " << (s.tailCut ? ">" : "") << juce::String (s.tail, 3) << " s | voices " << s.maxVoices
      << " | nan/inf " << s.nonFinite << " | " << juce::String (s.speed, 1) << "x realtime";
    return t;
}

juce::var statsJson (const Stats& s, const juce::String& name, const juce::String& file)
{
    auto* o = new juce::DynamicObject();
    if (name.isNotEmpty()) o->setProperty ("name", name);
    if (file.isNotEmpty()) o->setProperty ("file", file);
    o->setProperty ("peak_dbfs", s.peakDb);
    o->setProperty ("rms_dbfs", s.rmsDb);
    o->setProperty ("dc_offset", s.dc);
    o->setProperty ("f0_hz", s.f0);
    o->setProperty ("centroid_hz", s.centroid);
    o->setProperty ("tail_s", s.tail);
    o->setProperty ("tail_truncated", s.tailCut);
    o->setProperty ("non_finite", s.nonFinite);
    o->setProperty ("max_voices", s.maxVoices);
    o->setProperty ("realtime_factor", s.speed);
    o->setProperty ("seconds", s.seconds);
    return juce::var (o);
}

bool writeWav (const juce::File& f, const Audio& a)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream> (f);
    if (! stream->openedOk()) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), a.sr, 2, 24, {}, 0));
    if (writer == nullptr) return false;
    stream.release();
    const float* ch[2] = { a.l.data(), a.r.data() };
    return writer->writeFromFloatArrays (ch, 2, a.size());
}

// ---------------------------------------------------------------------------
//  Command-line helpers
// ---------------------------------------------------------------------------
bool setParam (WhyshoSynthAudioProcessor& proc, const juce::String& id, const juce::String& value, juce::String& error)
{
    const int idx = paramIndex (id);
    auto* param = proc.state().getParameter (id);
    if (idx < 0 || param == nullptr) { error = "unknown parameter '" + id + "'"; return false; }

    float raw = 0.0f;
    const auto v = value.trim();
    if (v.containsOnly ("0123456789.-+eE") && v.isNotEmpty()) raw = v.getFloatValue();
    else
    {
        const auto choices = paramChoices ((P) idx);
        int found = -1;
        for (int i = 0; i < choices.size(); ++i) if (choices[i].equalsIgnoreCase (v)) found = i;
        if (found < 0 && (v.equalsIgnoreCase ("on") || v.equalsIgnoreCase ("true"))) found = 1;
        if (found < 0 && (v.equalsIgnoreCase ("off") || v.equalsIgnoreCase ("false"))) found = 0;
        if (found < 0) { error = "bad value '" + value + "' for " + id; return false; }
        raw = (float) found;
    }
    param->setValueNotifyingHost (param->convertTo0to1 (raw));
    return true;
}

bool parseSeq (const juce::String& text, int defaultVel, std::vector<NoteEv>& out)
{
    for (auto& tok : juce::StringArray::fromTokens (text, ",", ""))
    {
        const auto f = juce::StringArray::fromTokens (tok.trim(), ":", "");
        if (f.size() < 3) return false;
        NoteEv n;
        n.note = f[0].getIntValue(); n.start = f[1].getDoubleValue(); n.len = f[2].getDoubleValue();
        n.vel = f.size() > 3 ? f[3].getIntValue() : defaultVel;
        out.push_back (n);
    }
    return ! out.empty();
}

// A phrase that shows off what a preset of this category is for.
Phrase phraseForCategory (const juce::String& categoryIn, double bpm)
{
    const auto c = categoryIn.toLowerCase();
    const double beat = 60.0 / bpm;
    Phrase ph;
    auto add = [&] (int note, double startBeats, double lenBeats, int vel = 100)
    { ph.notes.push_back ({ note, startBeats * beat, lenBeats * beat, vel }); };

    if (c.contains ("808") || c.contains ("sub"))
    {
        // long note, short hit, then an overlapping pair → slide if the patch glides
        add (36, 0.0, 2.0); add (36, 2.5, 0.5); add (36, 3.0, 2.2); add (43, 4.5, 1.5); add (31, 6.0, 2.0);
        ph.tail = 2.0;
    }
    else if (c.contains ("bass"))
    {
        add (36, 0.0, 0.75); add (36, 1.0, 0.5); add (48, 1.5, 0.5); add (39, 2.0, 0.75); add (36, 3.0, 0.9, 120); add (34, 4.0, 2.0);
        ph.tail = 1.5;
    }
    else if (c.contains ("pad") || c.contains ("string") || c.contains ("atmos") || c.contains ("choir"))
    {
        for (int n : { 48, 55, 60, 63, 67 }) add (n, 0.0, 6.0, 90);
        ph.tail = 4.0;
    }
    else if (c.contains ("pluck") || c.contains ("bell") || c.contains ("key") || c.contains ("piano") || c.contains ("mallet"))
    {
        const int seq[] = { 60, 63, 67, 72, 70, 67, 63, 67 };
        for (int i = 0; i < 8; ++i) add (seq[i], i * 0.5, 0.4, i % 2 ? 85 : 110);
        for (int n : { 60, 67, 75 }) add (n, 4.5, 1.5);
        ph.tail = 3.0;
    }
    else if (c.contains ("drum") || c.contains ("perc") || c.contains ("kick") || c.contains ("hit"))
    {
        add (36, 0.0, 0.25, 120); add (36, 1.0, 0.25, 100); add (48, 2.0, 0.25, 110); add (60, 3.0, 0.25, 110);
        ph.tail = 2.0;
    }
    else if (c.contains ("synth") || c.contains ("brass") || c.contains ("pulse") || c.contains ("stab"))
    {
        for (int n : { 55, 62, 67 }) add (n, 0.0, 0.5, 110);
        for (int n : { 53, 60, 65 }) add (n, 1.0, 0.5, 100);
        add (67, 2.0, 1.4); add (70, 3.5, 0.5); add (72, 4.0, 2.0);
        ph.tail = 3.0;
    }
    else if (c.contains ("fx") || c.contains ("sfx") || c.contains ("riser"))
    {
        add (60, 0.0, 6.0); ph.tail = 4.0;
    }
    else      // leads and anything unknown: a short line with a legato overlap and a held note
    {
        add (60, 0.0, 0.9); add (63, 1.0, 0.5); add (67, 1.5, 0.7); add (72, 2.0, 1.2); add (70, 3.0, 0.5); add (67, 3.5, 2.0);
        ph.tail = 3.0;
    }
    return ph;
}

bool ensureLicensed (WhyshoSynthAudioProcessor& proc)
{
    // Renders must not carry the unlicensed degradation (mutes + noise).
    proc.licenseManager().startOrCheckTrial();
    proc.refreshLicense();
    if (! proc.licenseInfo.isUsable())
    {
        std::cerr << "WARNING: licence/trial not usable (" << proc.licenseInfo.reason
                  << ") - renders will contain the unlicensed degradation gate.\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Benchmarks
// ---------------------------------------------------------------------------
Patch typicalPatch()
{
    Patch p;
    p.set (P::osc1Wave, wSaw).set (P::osc2On, 1).set (P::osc2Wave, wSaw).set (P::osc2Fine, 9).set (P::subLevel, 0.4f);
    p.set (P::fltCutoff, 1800).set (P::fltReso, 0.3f).set (P::fltEnv, 0.35f).set (P::ampR, 0.4f);
    p.set (P::mod1Src, msLfo1).set (P::mod1Dst, mdPitch).set (P::mod1Amt, 0.05f);
    p.set (P::choOn, 1).set (P::dlyOn, 1).set (P::revOn, 1);
    return p;
}

Phrase chordPhrase (int voices, double len, double tail, int vel = 100)
{
    Phrase ph;
    static const int steps[] = { 0, 3, 7, 10, 12, 15, 19, 22, 24, 27, 31, 34, 36, 39, 43, 46 };
    for (int i = 0; i < voices; ++i) ph.notes.push_back ({ 36 + steps[i % 16], 0.0, len, vel });
    ph.tail = tail;
    return ph;
}

double benchOne (const char* name, Patch patch, int voices, double sr = 48000.0, int block = 512)
{
    const auto a = renderEngine (patch, chordPhrase (voices, 6.0, 0.0), sr, block);
    const double load = 100.0 * a.cpuSeconds / a.seconds();
    std::cout << "  " << std::left << std::setw (58) << name << std::right << std::fixed << std::setprecision (2)
              << std::setw (7) << load << " % of one core  (" << std::setprecision (1) << a.seconds() / a.cpuSeconds
              << "x realtime, " << a.maxVoices << " voices)\n";
    return load;
}

void runBench()
{
    std::cout << "CPU (single core, " << "48 kHz / 512 unless noted):\n";
    benchOne ("default patch, 1 voice", Patch(), 1);
    benchOne ("typical: 2 saw + sub, LP24+env, cho+dly+rev, 8 voices", typicalPatch(), 8);
    benchOne ("typical, 16 voices", typicalPatch(), 16);
    benchOne ("typical, 8 voices, 96 kHz", typicalPatch(), 8, 96000.0);
    benchOne ("typical, 8 voices, block 32", typicalPatch(), 8, 48000.0, 32);
    {
        auto p = typicalPatch();
        p.set (P::unison, 4);
        benchOne ("typical + unison 4, 8 voices", p, 8);
    }
    {
        Patch p;
        p.set (P::osc1Wave, wSuperSaw).set (P::osc1Shape, 0.4f).set (P::fltCutoff, 6000).set (P::revOn, 1).set (P::choOn, 1);
        benchOne ("supersaw pad: 1 supersaw, 8 voices", p, 8);
    }
    {
        Patch p;
        for (int o = 0; o < kNumOscs; ++o)
        {
            p[oscParam (o, P::osc1On)] = 1; p[oscParam (o, P::osc1Wave)] = (float) wSuperSaw; p[oscParam (o, P::osc1Shape)] = 0.5f;
        }
        p.set (P::unison, 8).set (P::subLevel, 0.5f).set (P::noiseLevel, 0.2f).set (P::fltDrive, 0.5f);
        p.set (P::drvOn, 1).set (P::choOn, 1).set (P::dlyOn, 1).set (P::revOn, 1);
        benchOne ("WORST: 3 supersaw x unison 8, all FX, 16 voices", p, 16);
    }
    {
        Patch p;
        p.set (P::osc1Wave, wSine).set (P::pitchAmt, 24).set (P::ampD, 1.5f).set (P::ampS, 0).set (P::drvOn, 1);
        benchOne ("808: sine + pitch env + drive, 1 voice", p, 1);
    }
}

// ---------------------------------------------------------------------------
//  Self-test
// ---------------------------------------------------------------------------
struct TestRun
{
    int failures = 0, checks = 0;
    void section (const char* name) { std::cout << "\n[" << name << "]\n"; }
    bool check (bool ok, const juce::String& what, const juce::String& detail = {})
    {
        ++checks;
        if (! ok) ++failures;
        std::cout << (ok ? "  ok    " : "  FAIL  ") << what;
        if (detail.isNotEmpty()) std::cout << "   (" << detail << ")";
        std::cout << "\n" << std::flush;
        return ok;
    }
};

juce::String num (double v, int dp = 2) { return dp > 0 ? juce::String (v, dp) : juce::String (juce::roundToInt (v)); }

Phrase oneNote (int note, double len, double tail, int vel = 100)
{
    Phrase ph;
    ph.notes.push_back ({ note, 0.0, len, vel });
    ph.tail = tail;
    return ph;
}

Patch cleanPatch (int wave = wSaw)      // steady tone: full sustain, open filter, no velocity scaling
{
    Patch p;
    p.set (P::osc1Wave, (float) wave).set (P::osc1Level, 1.0f).set (P::ampS, 1.0f).set (P::velSens, 0.0f);
    p.set (P::fltCutoff, 20000).set (P::fltReso, 0.0f);
    return p;
}

void testSilenceAndLevels (TestRun& t)
{
    t.section ("silence / levels");
    {
        Patch p;
        p.set (P::drvOn, 1).set (P::choOn, 1).set (P::dlyOn, 1).set (P::revOn, 1).set (P::drvType, dTube);
        Phrase ph; ph.tail = 1.0;
        const auto a = renderEngine (p, ph);
        t.check (peakOf (a) <= 0.0 && a.maxVoices == 0, "no notes -> digital silence (all FX on)", "peak " + num (peakOf (a), 9));
    }
    {
        const auto a = renderEngine (Patch(), oneNote (60, 1.0, 0.5));
        const double pk = toDb (peakOf (a));
        t.check (pk > -14.0 && pk < -10.0, "default patch, 1 note @ vel 100 peaks near -12 dBFS", num (pk) + " dBFS");
        t.check (std::abs (cents (f0At (a, 0.3, 0.9), 261.6256)) < 1.0, "default patch plays C4", num (f0At (a, 0.3, 0.9), 3) + " Hz");
    }
    {
        const auto a = renderEngine (Patch(), chordPhrase (8, 1.5, 0.5));
        int hot = 0;
        for (int i = 0; i < a.size(); ++i) hot += (std::abs (a.l[(size_t) i]) >= 0.999f || std::abs (a.r[(size_t) i]) >= 0.999f) ? 1 : 0;
        t.check (peakOf (a) <= 1.0 && hot == 0, "8-note chord stays under 0 dBFS without flat-topping",
                 "peak " + num (toDb (peakOf (a))) + " dBFS, rms " + num (toDb (rmsOf (a, 0.2, 1.4))) + " dBFS");
    }
    {
        Patch p;
        for (int o = 0; o < kNumOscs; ++o) { p[oscParam (o, P::osc1On)] = 1; p[oscParam (o, P::osc1Level)] = 1; }
        p.set (P::unison, 8).set (P::subLevel, 1).set (P::masterGain, 12).set (P::fltReso, 1).set (P::fltCutoff, 400);
        const auto a = renderEngine (p, chordPhrase (16, 1.0, 0.5, 127));
        t.check (peakOf (a) <= 1.0 && countNonFinite (a) == 0, "everything maxed + 12 dB master never exceeds 0 dBFS",
                 "peak " + num (toDb (peakOf (a))) + " dBFS");
    }
    {
        Patch lo, hi;
        lo.set (P::masterGain, -6); hi.set (P::masterGain, 0);
        const double r = peakOf (renderEngine (hi, oneNote (60, 0.5, 0.2))) / peakOf (renderEngine (lo, oneNote (60, 0.5, 0.2)));
        t.check (std::abs (toDb (r) - 6.0) < 0.1, "master gain is in dB", num (toDb (r)) + " dB for a 6 dB step");
    }
    {
        Patch p; p.set (P::velSens, 1.0f);
        const double full = peakOf (renderEngine (p, oneNote (60, 0.3, 0.2, 127))), half = peakOf (renderEngine (p, oneNote (60, 0.3, 0.2, 64)));
        p.set (P::velSens, 0.0f);
        const double flatA = peakOf (renderEngine (p, oneNote (60, 0.3, 0.2, 127))), flatB = peakOf (renderEngine (p, oneNote (60, 0.3, 0.2, 20)));
        const double expect = (64.0 / 127.0) * (64.0 / 127.0);
        t.check (std::abs (half / full - expect) < 0.03 && std::abs (flatA / flatB - 1.0) < 0.01, "velocity sensitivity",
                 "sens 1: vel 64 = " + num (half / full, 3) + " (expect " + num (expect, 3) + "), sens 0: ratio " + num (flatA / flatB, 3));
    }
}

void testTuning (TestRun& t)
{
    t.section ("tuning (A4 = 440 Hz +/- 1 cent)");
    const char* names[] = { "saw", "square", "triangle", "sine", "supersaw" };
    for (int w = 0; w <= wSuperSaw; ++w)
    {
        auto p = cleanPatch (w);
        const auto a = renderEngine (p, oneNote (69, 4.6, 0.1));
        const double f = f0At (a, 0.4, 4.4, 440.0);       // 4 s window resolves the supersaw's centre partial
        t.check (std::abs (cents (f, 440.0)) < 1.0, juce::String (names[w]) + " A4", num (f, 4) + " Hz, " + num (cents (f, 440.0), 3) + " ct");
    }
    for (double sr : { 22050.0, 44100.0, 96000.0, 192000.0 })
    {
        const auto a = renderEngine (cleanPatch (wSaw), oneNote (69, 1.2, 0.1), sr);
        const double f = f0At (a, 0.2, 1.1);
        t.check (std::abs (cents (f, 440.0)) < 1.0, "saw A4 @ " + juce::String ((int) sr) + " Hz", num (f, 4) + " Hz");
    }
    {
        Patch p = cleanPatch (wSine);
        p.set (P::osc1On, 0).set (P::subLevel, 1.0f);
        const auto a = renderEngine (p, oneNote (69, 1.2, 0.1));
        p.set (P::subOct, 1);
        const auto b = renderEngine (p, oneNote (69, 1.2, 0.1));
        t.check (std::abs (cents (f0At (a, 0.2, 1.1), 220.0)) < 1.0 && std::abs (cents (f0At (b, 0.2, 1.1), 110.0)) < 1.0,
                 "sub oscillator -1 / -2 octaves", num (f0At (a, 0.2, 1.1), 3) + " / " + num (f0At (b, 0.2, 1.1), 3) + " Hz");
    }
    {
        Patch p = cleanPatch (wSine);
        p.set (P::osc1Oct, 1).set (P::osc1Semi, 7).set (P::osc1Fine, 50);
        const double f = f0At (renderEngine (p, oneNote (57, 1.2, 0.1)), 0.2, 1.1);
        const double expect = 220.0 * std::pow (2.0, (12.0 + 7.5) / 12.0);
        t.check (std::abs (cents (f, expect)) < 1.0, "octave / semi / fine offsets", num (f, 3) + " Hz, expect " + num (expect, 3));
    }
    {
        Patch p = cleanPatch (wSine);
        p.set (P::bendRange, 12);
        Phrase ph = oneNote (57, 1.2, 0.1);
        ph.extra.push_back ({ 0.0, juce::MidiMessage::pitchWheel (1, 16383) });
        const double f = f0At (renderEngine (p, ph), 0.3, 1.1);
        t.check (std::abs (cents (f, 440.0)) < 3.0, "pitch bend: full up with range 12 = +1 octave", num (f, 3) + " Hz");
    }
    {
        // documented matrix scaling: pitch = 24 * amt * |amt| semitones → 0.7071 = one octave
        Patch p = cleanPatch (wSine);
        p.set (P::mod1Src, msLfo1).set (P::mod1Dst, mdPitch).set (P::mod1Amt, 0.70711f).set (P::lfo1Wave, lSquare).set (P::lfo1Rate, 0.5f);
        const auto a = renderEngine (p, oneNote (57, 1.9, 0.1));
        const double hi = f0At (a, 0.2, 0.9), lo = f0At (a, 1.2, 1.9);
        t.check (std::abs (cents (hi, 440.0)) < 2.0 && std::abs (cents (lo, 110.0)) < 2.0, "mod matrix LFO->pitch 0.7071 = +/-12 st",
                 num (hi, 2) + " / " + num (lo, 2) + " Hz");
    }
    {
        Patch p = cleanPatch (wSine);
        p.set (P::pitchAmt, 24).set (P::pitchDec, 0.2f);
        const auto a = renderEngine (p, oneNote (45, 1.0, 0.1));
        const double early = yinAt (a, 0.0, 0.03), late = f0At (a, 0.5, 0.95);
        t.check (early > 200.0 && std::abs (cents (late, 110.0)) < 2.0, "pitch envelope sweeps down onto the note",
                 "first 30 ms " + num (early, 1) + " Hz -> " + num (late, 2) + " Hz");
    }
}

void testModulation (TestRun& t)
{
    t.section ("LFOs / mod matrix");
    Patch p = cleanPatch (wSine);
    p.set (P::ampA, 0.001f).set (P::ampR, 0.01f);
    p.set (P::mod1Src, msLfo1).set (P::mod1Dst, mdLevel).set (P::mod1Amt, 1.0f).set (P::lfo1Wave, lSquare);
    {
        // square tremolo, level x (1 +/- 1): on for half a cycle, silent for the other half
        p.set (P::lfo1Sync, 1).set (P::lfo1Div, 2);      // 1/4 note
        const auto a = renderEngine (p, oneNote (69, 1.4, 0.1), 48000.0, 512, 120.0);
        const auto b = renderEngine (p, oneNote (69, 1.4, 0.1), 48000.0, 512, 150.0);
        const double hi = peakOf (a, 0.05, 0.2), lo = peakOf (a, 0.3, 0.45), hi2 = peakOf (a, 0.55, 0.7);
        const double fast = peakOf (b, 0.25, 0.35);      // at 150 bpm the cycle is 0.4 s → low half is 0.2..0.4
        t.check (lo < 0.01 * hi && hi2 > 0.9 * hi && fast < 0.01 * hi, "tempo-synced LFO: 1/4 at 120 bpm = 2 Hz, follows bpm",
                 "levels " + num (hi, 3) + " / " + num (lo, 4) + " / " + num (hi2, 3));
    }
    {
        p.set (P::lfo1Sync, 0).set (P::lfo1Rate, 2.0f).set (P::lfo1Retrig, 1);
        Phrase ph;
        ph.notes = { { 69, 0.0, 0.2, 100 }, { 69, 0.3, 0.3, 100 } };
        ph.tail = 0.1;
        const auto retrig = renderEngine (p, ph);
        p.set (P::lfo1Retrig, 0);
        const auto freeRun = renderEngine (p, ph);
        // second note starts at 0.3 s: retriggered LFO is high again, the free-running one is in its low half (0.25..0.5 s)
        t.check (peakOf (retrig, 0.32, 0.45) > 0.1 && peakOf (freeRun, 0.32, 0.45) < 0.002 && peakOf (freeRun, 0.52, 0.58) > 0.1,
                 "LFO retrigger on = per-note phase, off = shared free-running phase");
    }
    {
        Patch q = cleanPatch (wSine);
        q.set (P::mod1Src, msModWheel).set (P::mod1Dst, mdLevel).set (P::mod1Amt, -1.0f);
        Phrase ph = oneNote (69, 1.0, 0.1);
        ph.extra.push_back ({ 0.5, juce::MidiMessage::controllerEvent (1, 1, 127) });
        const auto a = renderEngine (q, ph);
        t.check (peakOf (a, 0.2, 0.45) > 0.1 && peakOf (a, 0.7, 0.95) < 1.0e-4 && maxSecondDiff (a, 0.45, 0.6) < 0.01,
                 "mod wheel (CC1) drives the matrix, smoothed");
    }
    {
        Patch q = cleanPatch (wSine);
        q.set (P::mod1Src, msRandom).set (P::mod1Dst, mdPitch).set (P::mod1Amt, 0.5f);      // up to +/- 6 st per note
        Phrase ph;
        for (int i = 0; i < 6; ++i) ph.notes.push_back ({ 69, i * 0.3, 0.25, 100 });
        ph.tail = 0.1;
        const auto a = renderEngine (q, ph);
        double lo = 1.0e9, hi = 0.0;
        for (int i = 0; i < 6; ++i) { const double f = yinAt (a, i * 0.3 + 0.05, i * 0.3 + 0.2); lo = juce::jmin (lo, f); hi = juce::jmax (hi, f); }
        t.check (hi / lo > 1.05 && lo > 440.0 / 1.42 && hi < 440.0 * 1.42, "Random source: a new value per note, within +/- amt",
                 num (lo, 1) + " .. " + num (hi, 1) + " Hz over 6 notes");
    }
    {
        Patch q = cleanPatch (wSaw);
        q.set (P::fltCutoff, 300).set (P::fltType, fLP24).set (P::mod1Src, msVelocity).set (P::mod1Dst, mdCutoff).set (P::mod1Amt, 0.4f);
        const double soft = analyse (renderEngine (q, oneNote (48, 0.6, 0.1, 10)), oneNote (48, 0.6, 0.1)).centroid;
        const double hard = analyse (renderEngine (q, oneNote (48, 0.6, 0.1, 127)), oneNote (48, 0.6, 0.1)).centroid;
        t.check (hard > 2.0 * soft, "velocity -> cutoff opens the filter", "centroid " + num (soft, 0) + " -> " + num (hard, 0) + " Hz");
    }
    {
        // osc 2 → osc 1 FM adds sidebands; silent modulator (level 0) still modulates
        Patch q = cleanPatch (wSine);
        q.set (P::osc2On, 1).set (P::osc2Wave, wSine).set (P::osc2Level, 0.0f).set (P::osc2Semi, 12);
        const double plain = analyse (renderEngine (q, oneNote (57, 0.6, 0.1)), oneNote (57, 0.6, 0.1)).centroid;
        q.set (P::oscFm, 0.7f);
        const auto a = renderEngine (q, oneNote (57, 0.6, 0.1));
        const double fm = analyse (a, oneNote (57, 0.6, 0.1)).centroid;
        t.check (fm > 2.0 * plain && std::abs (cents (f0At (a, 0.2, 0.55, 220.0), 220.0)) < 2.0, "FM 2>1 adds harmonics and keeps the pitch",
                 "centroid " + num (plain, 0) + " -> " + num (fm, 0) + " Hz");
    }
    {
        // shape: pulse width. 50 % has no even harmonics; shape 1 is a ~5 % pulse (strong 2nd harmonic)
        auto second = [] (float shape)
        {
            Patch q = cleanPatch (wSquare);
            q.set (P::osc1Shape, shape);
            const auto a = renderEngine (q, oneNote (57, 0.8, 0.1));
            const auto m = a.mono();
            return dftMag (m.data() + 9600, 24000, 48000.0, 440.0) / dftMag (m.data() + 9600, 24000, 48000.0, 220.0);
        };
        const double h50 = second (0.0f), h5 = second (1.0f);
        t.check (h50 < 0.01 && h5 > 0.8, "square shape = pulse width (50 % -> 5 %)", "2nd harmonic " + num (h50, 4) + " -> " + num (h5, 3));
    }
}

void testBlockInvariance (TestRun& t)
{
    t.section ("block-size invariance");
    Patch p;
    p.set (P::osc2On, 1).set (P::osc2Wave, wSquare).set (P::osc2Shape, 0.4f).set (P::osc3On, 1).set (P::osc3Wave, wSuperSaw);
    p.set (P::subLevel, 0.3f).set (P::noiseLevel, 0.1f).set (P::oscFm, 0.3f).set (P::unison, 3);
    p.set (P::fltCutoff, 900).set (P::fltReso, 0.6f).set (P::fltEnv, 0.4f).set (P::fltDrive, 0.3f).set (P::pitchAmt, 12);
    p.set (P::mod1Src, msLfo1).set (P::mod1Dst, mdCutoff).set (P::mod1Amt, 0.3f);
    p.set (P::mod2Src, msLfo2).set (P::mod2Dst, mdPan).set (P::mod2Amt, 0.5f).set (P::lfo2Wave, lSampleHold).set (P::lfo2Rate, 7);
    p.set (P::mod3Src, msRandom).set (P::mod3Dst, mdShape).set (P::mod3Amt, 0.5f);
    p.set (P::glide, 0.1f).set (P::drvOn, 1).set (P::choOn, 1).set (P::dlyOn, 1).set (P::revOn, 1);
    Phrase ph;
    ph.notes = { { 48, 0.0131, 0.4, 100 }, { 55, 0.1007, 0.5, 80 }, { 60, 0.2503, 0.3, 120 }, { 48, 0.5011, 0.2, 90 }, { 67, 0.6113, 0.3, 127 } };
    ph.extra.push_back ({ 0.3301, juce::MidiMessage::controllerEvent (1, 64, 127) });
    ph.extra.push_back ({ 0.7207, juce::MidiMessage::controllerEvent (1, 64, 0) });
    ph.tail = 0.6;

    for (int mode = 0; mode < 2; ++mode)
    {
        p.set (P::voiceMode, mode == 0 ? (float) vmPoly : (float) vmLegato);
        const auto ref = renderEngine (p, ph, 48000.0, 512, 140.0);
        double worst = 0.0;
        for (int block : { 1, 64, 333 })
        {
            const auto a = renderEngine (p, ph, 48000.0, block, 140.0);
            for (int i = 0; i < ref.size(); ++i)
                worst = juce::jmax (worst, (double) std::abs (a.l[(size_t) i] - ref.l[(size_t) i]), (double) std::abs (a.r[(size_t) i] - ref.r[(size_t) i]));
        }
        t.check (worst < 1.0e-5 && peakOf (ref) > 0.05, juce::String (mode == 0 ? "poly" : "legato") + ": block 1 / 64 / 333 match block 512",
                 "max diff " + juce::String (worst, 9) + ", peak " + num (peakOf (ref), 3));
    }
}

void testAliasing (TestRun& t)
{
    t.section ("aliasing (non-harmonic energy, dB relative to the harmonics)");
    const char* names[] = { "saw", "square", "triangle" };
    for (double sr : { 44100.0, 48000.0 })
        for (int w = 0; w <= wTriangle; ++w)
        {
            double worst = -300.0; int worstNote = 0;
            for (int note : { 84, 91, 96, 101, 105, 108, 113 })
            {
                auto p = cleanPatch (w);
                if (w == wSquare) p.set (P::osc1Shape, 0.3f);
                const auto a = renderEngine (p, oneNote (note, 2.0, 0.05), sr);
                const double f0 = f0At (a, 0.3, 1.8, 440.0 * std::pow (2.0, (note - 69) / 12.0));
                const double d = inharmonicDb (a, 0.3, f0);
                if (d > worst) { worst = d; worstNote = note; }
            }
            t.check (worst < -60.0, juce::String (names[w]) + " @ " + juce::String ((int) sr) + " Hz, notes C6..C#8",
                     "worst " + num (worst, 1) + " dB at note " + juce::String (worstNote));
        }
    {
        // gliding pitch crosses wavetable boundaries — must not click
        auto p = cleanPatch (wSaw);
        p.set (P::voiceMode, vmLegato).set (P::glide, 1.0f).set (P::fltCutoff, 3000);
        Phrase ph;
        ph.notes = { { 36, 0.0, 1.5, 100 }, { 84, 0.2, 1.3, 100 } };
        ph.tail = 0.1;
        auto a = renderEngine (p, ph);
        p.set (P::osc1Wave, wSine);
        const auto ref = renderEngine (p, ph);
        t.check (countNonFinite (a) == 0 && peakOf (a, 0.3, 1.2) > 0.1 && peakOf (ref) > 0.1, "4-octave glide across mip tables renders",
                 "peak " + num (peakOf (a), 3));
    }
}

void testFilter (TestRun& t)
{
    t.section ("filter");
    const char* names[] = { "LP24", "LP12", "BP12", "HP12", "HP24", "Notch" };
    for (int type = 0; type <= fNotch; ++type)
    {
        Patch p;
        p.set (P::fltType, (float) type).set (P::fltReso, 1.0f).set (P::noiseLevel, 0.5f).set (P::fltDrive, 0.5f);
        p.set (P::osc2On, 1).set (P::osc2Wave, wSquare).set (P::masterGain, -24).set (P::fltEnv, 1.0f).set (P::fltA, 0.0005f).set (P::fltD, 0.01f);
        p.set (P::mod1Src, msLfo1).set (P::mod1Dst, mdCutoff).set (P::mod1Amt, 1.0f).set (P::lfo1Wave, lSquare).set (P::lfo1Rate, 30);
        p.set (P::mod2Src, msLfo2).set (P::mod2Dst, mdCutoff).set (P::mod2Amt, -1.0f).set (P::lfo2Wave, lSampleHold).set (P::lfo2Rate, 30);
        p.set (P::mod3Src, msLfo1).set (P::mod3Dst, mdReso).set (P::mod3Amt, 1.0f);
        juce::Random rng (99 + type);
        Phrase ph = chordPhrase (6, 2.0, 0.6);
        // cutoff slammed between extremes every block on top of the audio-rate-ish modulation
        const auto a = renderEngine (p, ph, 48000.0, 32, 120.0, [&] (Patch& q, int)
        {
            const float r = rng.nextFloat();
            q.set (P::fltCutoff, r < 0.33f ? 20.0f : (r < 0.66f ? 20000.0f : 20.0f * std::pow (1000.0f, rng.nextFloat())));
        });
        const double pk = peakOf (a) * 15.85;      // undo the -24 dB master → pre-master level
        t.check (countNonFinite (a) == 0 && pk < 12.0 && a.endVoices == 0 && peakOf (a, 2.5, 2.6) < 1.0e-4,
                 juce::String (names[type]) + ": max resonance + violent cutoff modulation stays bounded",
                 "pre-master peak " + num (toDb (pk), 1) + " dBFS (6 voices)");
    }
    {
        // near self-oscillation: resonance 1.0 rings at the cutoff frequency
        Patch p = cleanPatch (wSaw);
        p.set (P::fltType, fLP12).set (P::fltCutoff, 1000).set (P::fltReso, 1.0f);
        const auto a = renderEngine (p, oneNote (33, 1.0, 0.2));
        const auto mono = a.mono();
        const auto mag = spectrum (mono, 9600, 15);
        size_t best = 1;
        for (size_t k = 1; k < mag.size(); ++k) if (mag[k] > mag[best]) best = k;
        const double fpk = (double) best * 48000.0 / 32768.0;
        t.check (std::abs (cents (fpk, 1000.0)) < 60.0, "resonance peak sits on the cutoff", num (fpk, 1) + " Hz for a 1 kHz cutoff");
    }
    {
        // LP24 slope: one octave above cutoff ≈ -24 dB, measured with a sine through a static filter
        Patch p = cleanPatch (wSine);
        p.set (P::fltType, fLP24).set (P::fltCutoff, 220).set (P::fltReso, 0.0f);
        const double pass = rmsOf (renderEngine (p, oneNote (33, 0.8, 0.1)), 0.3, 0.7);     // 55 Hz
        const double oct  = rmsOf (renderEngine (p, oneNote (69, 0.8, 0.1)), 0.3, 0.7);     // 440 Hz
        const double oct2 = rmsOf (renderEngine (p, oneNote (81, 0.8, 0.1)), 0.3, 0.7);     // 880 Hz
        p.set (P::fltType, fLP12);
        const double o12 = rmsOf (renderEngine (p, oneNote (81, 0.8, 0.1)), 0.3, 0.7) / rmsOf (renderEngine (p, oneNote (69, 0.8, 0.1)), 0.3, 0.7);
        t.check (std::abs (toDb (oct2 / oct) + 24.0) < 2.0 && std::abs (toDb (o12) + 12.0) < 1.5 && toDb (oct / pass) < -20.0,
                 "LP24 / LP12 slopes", num (toDb (oct2 / oct), 1) + " and " + num (toDb (o12), 1) + " dB per octave");
    }
    {
        // key tracking 100 %: cutoff follows the note, so the filtered level ratio is note-independent
        Patch p = cleanPatch (wSaw);
        p.set (P::fltCutoff, 400).set (P::fltKey, 1.0f).set (P::fltType, fLP24);
        const double cLow = analyse (renderEngine (p, oneNote (48, 0.8, 0.1)), oneNote (48, 0.8, 0.1)).centroid;
        const double cHigh = analyse (renderEngine (p, oneNote (72, 0.8, 0.1)), oneNote (72, 0.8, 0.1)).centroid;
        t.check (std::abs (cHigh / cLow - 4.0) < 0.5, "key tracking 100 % moves the cutoff 1 oct / oct", "centroid ratio " + num (cHigh / cLow, 2) + " over 2 octaves");
    }
    {
        // filter envelope amount: ±1 = ±10 octaves → +0.2 on a 200 Hz cutoff opens to 800 Hz at the peak
        Patch p = cleanPatch (wSaw);
        p.set (P::fltCutoff, 200).set (P::fltEnv, 0.2f).set (P::fltS, 1.0f).set (P::fltType, fLP24);
        Patch q = cleanPatch (wSaw);
        q.set (P::fltCutoff, 800).set (P::fltType, fLP24);
        const double a = rmsOf (renderEngine (p, oneNote (36, 0.8, 0.1)), 0.3, 0.7), b = rmsOf (renderEngine (q, oneNote (36, 0.8, 0.1)), 0.3, 0.7);
        t.check (std::abs (toDb (a / b)) < 0.3, "filter env amount 0.2 = +2 octaves", num (toDb (a / b), 2) + " dB vs static 800 Hz");
    }
}

// Time at which the short-window peak envelope first reaches `frac` of `ref`.
double timeToReach (const Audio& a, double from, double ref, double frac, bool falling = false)
{
    const int win = juce::jmax (8, (int) (a.sr * 0.002));
    for (int i = (int) (from * a.sr); i + win < a.size(); i += win / 4)
    {
        const double pk = juce::jmax (peakOf (a.l.data() + i, win), peakOf (a.r.data() + i, win));
        if (falling ? pk <= ref * frac : pk >= ref * frac) return (double) i / a.sr - from;
    }
    return -1.0;
}

void testEnvelopes (TestRun& t)
{
    t.section ("envelopes");
    Patch p = cleanPatch (wSine);
    p.set (P::ampA, 0.1f).set (P::ampD, 0.3f).set (P::ampS, 0.5f).set (P::ampR, 0.4f);
    const auto a = renderEngine (p, oneNote (81, 1.5, 1.0));
    const double pk = peakOf (a);
    const double tAtk = timeToReach (a, 0.0, pk, 0.985);
    const double sus = peakOf (a, 1.3, 1.5);
    const double tRel = timeToReach (a, 1.5, sus, 0.001, true);
    const double tDec = timeToReach (a, 0.1, pk * 0.5, 1.0 + 0.0316, true);      // decay 30 dB of the way = half the time
    t.check (std::abs (tAtk - 0.1) < 0.012, "attack time", num (tAtk * 1000.0, 1) + " ms for 100 ms");
    t.check (std::abs (sus / pk - 0.5) < 0.01, "sustain level", num (sus / pk, 3) + " for 0.5");
    t.check (std::abs (tDec - 0.15) < 0.02, "decay rate (-60 dB over the decay time)", num (tDec * 1000.0, 1) + " ms to -30 dB for a 300 ms decay");
    t.check (std::abs (tRel - 0.4) < 0.03, "release time (-60 dB)", num (tRel * 1000.0, 1) + " ms for 400 ms");

    Patch f = cleanPatch (wSine);
    f.set (P::ampA, 0.0005f);
    const auto b = renderEngine (f, oneNote (93, 0.2, 0.1));
    int idx90 = -1;
    const double bp = peakOf (b, 0.05, 0.15);
    for (int i = 0; i < b.size() && idx90 < 0; ++i) if (std::abs (b.l[(size_t) i]) >= 0.9 * bp) idx90 = i;
    t.check (idx90 >= 4 && idx90 <= 40, "minimum attack is 0.5 ms and not a step", juce::String (idx90) + " samples to 90 %");
    t.check (maxSecondDiff (b, 0.0, 0.01) < 0.12, "fastest attack starts without a click", "max 2nd diff " + num (maxSecondDiff (b, 0.0, 0.01), 4));

    Patch z = cleanPatch (wSine);
    z.set (P::ampD, 0.2f).set (P::ampS, 0.0f);
    const auto c = renderEngine (z, oneNote (60, 2.0, 0.1));
    t.check (peakOf (c, 0.5, 2.0) < 1.0e-4 && c.endVoices == 0 && peakOf (c, 0.0, 0.1) > 0.1, "sustain 0: held voice decays to silence and frees itself");
}

void testVoiceModes (TestRun& t)
{
    t.section ("mono / legato / glide");
    Patch p = cleanPatch (wSine);
    p.set (P::ampA, 0.002f).set (P::ampD, 0.05f).set (P::ampS, 0.3f).set (P::ampR, 0.05f).set (P::glide, 0.2f);
    Phrase overlap;
    overlap.notes = { { 48, 0.0, 0.6, 100 }, { 60, 0.4, 0.6, 100 } };
    overlap.tail = 0.3;

    {
        p.set (P::voiceMode, vmLegato);
        const auto a = renderEngine (p, overlap);
        const double before = yinAt (a, 0.25, 0.39), mid = yinAt (a, 0.48, 0.52), after = yinAt (a, 0.75, 0.95);
        const double sus = peakOf (a, 0.3, 0.39), atNote2 = peakOf (a, 0.4, 0.5);
        t.check (std::abs (cents (before, 130.81)) < 5 && std::abs (cents (after, 261.63)) < 5, "legato: slides from the first to the second note",
                 num (before, 1) + " -> " + num (after, 1) + " Hz");
        t.check (std::abs (cents (mid, 185.0)) < 120.0, "legato: half-way through the glide the pitch is in between", num (mid, 1) + " Hz at glide midpoint (F#3 = 185)");
        t.check (atNote2 < sus * 1.15, "legato: overlapping note does not retrigger the envelope", "level " + num (atNote2 / sus, 2) + "x sustain");
        t.check (a.maxVoices == 1, "legato: one voice only", juce::String (a.maxVoices) + " voices");
    }
    {
        p.set (P::voiceMode, vmMono);
        const auto a = renderEngine (p, overlap);
        const double sus = peakOf (a, 0.3, 0.39), atNote2 = peakOf (a, 0.4, 0.5);
        t.check (atNote2 > sus * 2.0 && a.maxVoices == 1, "mono: overlapping note retriggers the envelope", "level " + num (atNote2 / sus, 2) + "x sustain");
        t.check (maxSecondDiff (a, 0.39, 0.45) < 0.01, "mono: retrigger is click-free", "max 2nd diff " + num (maxSecondDiff (a, 0.39, 0.45), 5));
    }
    {
        p.set (P::voiceMode, vmLegato).set (P::glide, 0.05f);
        Phrase ph;
        ph.notes = { { 48, 0.0, 1.2, 100 }, { 60, 0.3, 0.3, 100 } };
        ph.tail = 0.2;
        const auto a = renderEngine (p, ph);
        const double up = yinAt (a, 0.45, 0.58), back = yinAt (a, 0.8, 1.1);
        t.check (std::abs (cents (up, 261.63)) < 5 && std::abs (cents (back, 130.81)) < 5, "legato: releasing the top note returns to the held one",
                 num (up, 1) + " -> " + num (back, 1) + " Hz");
    }
    {
        // separate (non-overlapping) notes in legato start on pitch: 808 hits stay in tune
        p.set (P::voiceMode, vmLegato).set (P::glide, 0.3f);
        Phrase ph;
        ph.notes = { { 48, 0.0, 0.3, 100 }, { 60, 0.5, 0.4, 100 } };
        ph.tail = 0.2;
        const auto a = renderEngine (p, ph);
        t.check (std::abs (cents (yinAt (a, 0.51, 0.6), 261.63)) < 10, "legato: detached notes do not glide", num (yinAt (a, 0.51, 0.6), 1) + " Hz right after note 2");
    }
    {
        p.set (P::voiceMode, vmPoly).set (P::glide, 0.3f);
        Phrase ph;
        ph.notes = { { 48, 0.0, 0.3, 100 }, { 60, 0.5, 0.6, 100 } };
        ph.tail = 0.2;
        const auto a = renderEngine (p, ph);
        const double start = yinAt (a, 0.5, 0.56), end = yinAt (a, 0.9, 1.08);
        t.check (start < 180.0 && start > 125.0 && std::abs (cents (end, 261.63)) < 5, "poly: new note glides from the previous note",
                 num (start, 1) + " -> " + num (end, 1) + " Hz");
    }
    {
        p.set (P::voiceMode, vmPoly).set (P::glide, 0.0f);
        Phrase ph = oneNote (60, 0.2, 1.0);
        ph.extra.push_back ({ 0.1, juce::MidiMessage::controllerEvent (1, 64, 127) });
        ph.extra.push_back ({ 0.7, juce::MidiMessage::controllerEvent (1, 64, 0) });
        const auto a = renderEngine (p, ph);
        t.check (peakOf (a, 0.5, 0.69) > 0.02 && peakOf (a, 0.9, 1.2) < 1.0e-4, "sustain pedal holds the note until released");
        Phrase off = oneNote (60, 2.0, 0.2);
        off.extra.push_back ({ 0.3, juce::MidiMessage::allNotesOff (1) });
        const auto b = renderEngine (p, off);
        t.check (peakOf (b, 0.6, 2.0) < 1.0e-4 && peakOf (b, 0.1, 0.3) > 0.02, "CC123 all-notes-off releases held notes");
        Phrase snd = oneNote (60, 2.0, 0.2);
        snd.extra.push_back ({ 0.3, juce::MidiMessage::allSoundOff (1) });
        Patch longRel = p; longRel.set (P::ampR, 5.0f);
        const auto c = renderEngine (longRel, snd);
        t.check (peakOf (c, 0.32, 2.0) < 1.0e-4 && maxSecondDiff (c, 0.29, 0.32) < 0.01, "CC120 all-sound-off silences within a few ms, no click");
    }
}

void testVoiceStealing (TestRun& t)
{
    t.section ("polyphony / stealing / tails");
    {
        Patch p = cleanPatch (wSine);
        p.set (P::ampA, 0.005f).set (P::ampR, 3.0f).set (P::ampS, 0.8f);
        Phrase ph;
        for (int i = 0; i < 40; ++i) ph.notes.push_back ({ 36 + (i * 5) % 25, 0.02 * i + 0.0007 * (i % 3), 0.1, 100 });
        ph.tail = 0.3;
        const auto a = renderEngine (p, ph);
        Phrase burst;      // 40 notes in the same sample: more steals than spare slots
        for (int i = 0; i < 40; ++i) burst.notes.push_back ({ 30 + i, 0.1, 0.2, 100 });
        burst.tail = 0.2;
        const auto b = renderEngine (p, burst);
        t.check (a.maxVoices == kMaxVoices && b.maxVoices == kMaxVoices, "polyphony is capped at 16", juce::String (a.maxVoices) + " voices");
        t.check (maxSecondDiff (a) < 0.01, "40 overlapping notes: stolen voices fade without clicks", "max 2nd diff " + num (maxSecondDiff (a), 5) + " (a hard cut would be > 0.05)");
        t.check (countNonFinite (b) == 0 && peakOf (b) <= 1.0, "40 simultaneous notes handled");
    }
    {
        Patch p;
        p.set (P::ampR, 0.2f);
        dsp::SynthEngine eng;
        const auto a = renderEngine (eng, p, chordPhrase (8, 0.3, 1.5));
        t.check (a.maxVoices == 8 && a.endVoices == 0 && eng.activeVoices() == 0 && peakOf (a, 1.4, 1.8) <= 0.0,
                 "release tails end: voices free themselves, output returns to exact silence",
                 "tail " + num (timeToFallBelow (a, 0.3, 0.001), 3) + " s to -60 dBFS for a 0.2 s release");
    }
    {
        Patch p;
        p.set (P::unison, 8).set (P::uniDetune, 1.0f).set (P::uniSpread, 1.0f);
        const auto a = renderEngine (p, chordPhrase (16, 0.5, 0.3));
        Patch q;
        const double single = rmsOf (renderEngine (q, oneNote (60, 0.8, 0.1)), 0.3, 0.7);
        const double stacked = rmsOf (renderEngine (p, oneNote (60, 0.8, 0.1)), 0.3, 0.7);
        t.check (a.maxVoices == 16, "unison 8 does not consume polyphony", juce::String (a.maxVoices) + " voices for 16 notes");
        t.check (std::abs (toDb (stacked / single)) < 2.5, "unison is gain-compensated", num (toDb (stacked / single), 2) + " dB vs 1 voice");
        // width: side energy appears only with spread
        const auto wide = renderEngine (p, oneNote (60, 0.8, 0.1));
        double side = 0.0, mid = 0.0;
        for (int i = 9600; i < 33600; ++i) { side += std::pow (wide.l[(size_t) i] - wide.r[(size_t) i], 2.0); mid += std::pow (wide.l[(size_t) i] + wide.r[(size_t) i], 2.0); }
        const auto narrow = renderEngine (q, oneNote (60, 0.8, 0.1));
        double side0 = 0.0;
        for (int i = 9600; i < 33600; ++i) side0 += std::pow (narrow.l[(size_t) i] - narrow.r[(size_t) i], 2.0);
        t.check (side / mid > 0.1 && side0 < 1.0e-9, "unison spread widens, default patch is mono-centred", "side/mid " + num (side / mid, 3));
    }
}

void testEffects (TestRun& t)
{
    t.section ("effects");
    Patch base = cleanPatch (wSine);
    base.set (P::ampA, 0.01f).set (P::ampR, 0.05f);

    // toggling every effect on / off under a steady low sine must not click
    struct Fx { const char* name; P on; double limit; };
    for (auto fx : { Fx { "drive", P::drvOn, 0.004 }, Fx { "chorus", P::choOn, 0.004 }, Fx { "delay", P::dlyOn, 0.004 }, Fx { "reverb", P::revOn, 0.004 } })
    {
        const auto a = renderEngine (base, oneNote (45, 2.0, 0.3), 48000.0, 256, 120.0, [&] (Patch& q, int pos)
        {
            q.set (fx.on, ((pos / 9600) & 1) ? 1.0f : 0.0f);      // toggle every 200 ms
        });
        const double d = maxSecondDiff (a, 0.1, 2.0);
        t.check (d < fx.limit && countNonFinite (a) == 0, juce::String (fx.name) + ": click-free on/off", "max 2nd diff " + num (d, 5));
    }
    {
        const char* names[] = { "soft", "hard", "tube", "fold", "crush" };
        const double dry = rmsOf (renderEngine (Patch(), oneNote (48, 1.0, 0.1)), 0.3, 0.9);
        for (int type = 0; type <= dCrush; ++type)
        {
            juce::String detail;
            bool ok = true;
            for (float amt : { 0.0f, 0.3f, 0.7f, 1.0f })
            {
                Patch p;
                p.set (P::drvOn, 1).set (P::drvType, (float) type).set (P::drvAmt, amt);
                const auto a = renderEngine (p, oneNote (48, 1.0, 0.1));
                const double d = toDb (rmsOf (a, 0.3, 0.9) / dry);
                detail << num (d, 1) << " ";
                ok = ok && countNonFinite (a) == 0 && std::abs (d) < 4.0 && std::abs (analyse (a, oneNote (48, 1.0, 0.1)).dc) < 0.01;
            }
            t.check (ok, juce::String ("drive ") + names[type] + ": loudness roughly held, no DC", "dB vs dry at amt 0/0.3/0.7/1: " + detail.trim());
        }
    }
    {
        // delay: a short blip must come back after exactly the delay time, then ping-pong to the other side
        Patch p = cleanPatch (wSine);
        p.set (P::ampA, 0.0005f).set (P::ampD, 0.02f).set (P::ampS, 0).set (P::ampR, 0.01f);
        p.set (P::dlyOn, 1).set (P::dlySync, 0).set (P::dlyTime, 0.25f).set (P::dlyMix, 0.5f).set (P::dlyFb, 0.5f).set (P::dlyPing, 1);
        const auto a = renderEngine (p, oneNote (72, 0.03, 1.2));
        auto peakPos = [&] (const std::vector<float>& x, double t0, double t1)
        {
            int best = (int) (t0 * a.sr);
            for (int i = best; i < (int) (t1 * a.sr); ++i) if (std::abs (x[(size_t) i]) > std::abs (x[(size_t) best])) best = i;
            return best;
        };
        const int dryPk = peakPos (a.l, 0.0, 0.1), echo1 = peakPos (a.l, 0.2, 0.4), echo2 = peakPos (a.r, 0.45, 0.65);
        const double e1 = (echo1 - dryPk) / a.sr, e2 = (echo2 - dryPk) / a.sr;
        const double l1 = peakOf (a.l.data() + 9600, 9600), r1 = peakOf (a.r.data() + 9600, 9600);
        t.check (std::abs (e1 - 0.25) < 0.002 && std::abs (e2 - 0.5) < 0.003 && r1 < 0.05 * l1, "delay: free time + ping-pong",
                 "echoes at " + num (e1 * 1000, 1) + " ms (L), " + num (e2 * 1000, 1) + " ms (R)");

        p.set (P::dlySync, 1).set (P::dlyDiv, 3).set (P::dlyPing, 0);      // 1/8 at 120 bpm = 250 ms
        const auto b = renderEngine (p, oneNote (72, 0.03, 1.2), 48000.0, 512, 120.0);
        const double s1 = (peakPos (b.l, 0.2, 0.4) - peakPos (b.l, 0.0, 0.1)) / b.sr;
        const auto c = renderEngine (p, oneNote (72, 0.03, 1.2), 48000.0, 512, 150.0);
        const double s2 = (peakPos (c.l, 0.15, 0.4) - peakPos (c.l, 0.0, 0.1)) / c.sr;
        t.check (std::abs (s1 - 0.25) < 0.002 && std::abs (s2 - 0.2) < 0.002, "delay: tempo sync follows bpm",
                 "1/8 = " + num (s1 * 1000, 1) + " ms @120, " + num (s2 * 1000, 1) + " ms @150");

        // changing the time while it rings must glide, not click
        Patch q = base;
        q.set (P::dlyOn, 1).set (P::dlySync, 0).set (P::dlyMix, 0.5f).set (P::dlyFb, 0.6f);
        const auto d = renderEngine (q, oneNote (45, 2.0, 0.3), 48000.0, 256, 120.0, [&] (Patch& x, int pos)
        {
            x.set (P::dlyTime, ((pos / 14400) & 1) ? 0.11f : 0.43f);
        });
        t.check (maxSecondDiff (d, 0.1, 2.2) < 0.004, "delay: time changes are smooth", "max 2nd diff " + num (maxSecondDiff (d, 0.1, 2.2), 5));
    }
    {
        Patch p = cleanPatch (wSaw);
        p.set (P::ampR, 0.01f).set (P::revOn, 1).set (P::revMix, 0.5f).set (P::revSize, 0.3f);
        const auto small = renderEngine (p, oneNote (60, 0.2, 6.0));
        p.set (P::revSize, 0.7f);
        const auto big = renderEngine (p, oneNote (60, 0.2, 14.0));
        const double ref = peakOf (small, 0.1, 0.2);
        const double tSmall = timeToReach (small, 0.25, ref, 0.001, true), tBig = timeToReach (big, 0.25, ref, 0.001, true);
        double corr = 0.0, el = 0.0, er = 0.0;
        for (int i = 14400; i < 48000; ++i) { corr += (double) big.l[(size_t) i] * big.r[(size_t) i]; el += (double) big.l[(size_t) i] * big.l[(size_t) i]; er += (double) big.r[(size_t) i] * big.r[(size_t) i]; }
        corr /= std::sqrt (el * er) + 1.0e-20;
        t.check (tSmall > 0.2 && tSmall < 2.0 && tBig > 1.5 * tSmall && tBig < 12.0, "reverb: size sets the decay time",
                 "-60 dB after " + num (tSmall, 2) + " s (size 0.3), " + num (tBig, 2) + " s (size 0.7)");
        t.check (std::abs (corr) < 0.5 && peakOf (big, 13.5, 14.2) < 1.0e-4, "reverb: tail is stereo-decorrelated and dies away", "L/R correlation " + num (corr, 2));
        p.set (P::revDamp, 0.0f);
        const double bright = analyse (renderEngine (p, oneNote (60, 0.2, 2.0)), oneNote (60, 2.2, 0.0)).centroid;
        p.set (P::revDamp, 1.0f);
        const double dark = analyse (renderEngine (p, oneNote (60, 0.2, 2.0)), oneNote (60, 2.2, 0.0)).centroid;
        t.check (dark < 0.7 * bright, "reverb: damping darkens the tail", "centroid " + num (bright, 0) + " -> " + num (dark, 0) + " Hz");
    }
    {
        Patch p = cleanPatch (wSaw);
        p.set (P::choOn, 1).set (P::choMix, 0.5f).set (P::choDepth, 1.0f);
        const auto a = renderEngine (p, oneNote (60, 1.5, 0.2));
        double side = 0.0, mid = 0.0;
        for (int i = 9600; i < 67200; ++i) { side += std::pow (a.l[(size_t) i] - a.r[(size_t) i], 2.0); mid += std::pow (a.l[(size_t) i] + a.r[(size_t) i], 2.0); }
        t.check (side / mid > 0.02 && countNonFinite (a) == 0, "chorus: produces stereo width", "side/mid " + num (side / mid, 3));
    }
}

void testFuzz (TestRun& t, int iterations)
{
    t.section ("fuzz (random parameters incl. out-of-range / NaN, random MIDI)");
    const double rates[] = { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 };
    const int blocks[] = { 1, 7, 32, 64, 127, 333, 512, 1024 };
    std::unique_ptr<dsp::SynthEngine> engines[5];
    for (int i = 0; i < 5; ++i) { engines[i] = std::make_unique<dsp::SynthEngine>(); engines[i]->prepare (rates[i], 1024); }

    juce::Random rng (20240607);
    const int tripsBefore = dsp::guardTrips().load();
    juce::int64 samples = 0;
    int bad = 0, over = 0, firstBad = -1;
    double maxPeak = 0.0, cpu = 0.0, audio = 0.0;
    const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();

    auto randomise = [&] (Patch& p, bool wild)
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& d = paramDef (i);
            const float r = rng.nextFloat();
            float v;
            if (r < 0.62f)      v = d.min + (d.max - d.min) * rng.nextFloat();
            else if (r < 0.74f) v = d.min;
            else if (r < 0.86f) v = d.max;
            else if (r < 0.92f) v = d.def;
            else if (! wild)    v = d.min + (d.max - d.min) * rng.nextFloat();
            else if (r < 0.95f) v = (rng.nextBool() ? 1.0f : -1.0f) * 1.0e9f;
            else if (r < 0.97f) v = nan;
            else if (r < 0.985f) v = rng.nextBool() ? inf : -inf;
            else                v = (d.max - d.min) * (rng.nextFloat() * 6.0f - 3.0f);
            p.v[(size_t) i] = v;
        }
        // make sure most sets are audible so the test exercises real signal paths
        if (rng.nextFloat() < 0.8f) { p[P::osc1On] = 1; p[P::osc1Level] = juce::jmax (0.3f, std::abs (p[P::osc1Level])); }
        if (rng.nextFloat() < 0.5f) p[P::masterGain] = 12.0f;
    };

    for (int it = 0; it < iterations; ++it)
    {
        const int ri = rng.nextInt (5);
        auto& eng = *engines[ri];
        const int block = blocks[rng.nextInt (8)];
        const int total = block == 1 ? 600 + rng.nextInt (600) : 1500 + rng.nextInt (3000);
        if (rng.nextFloat() < 0.1f) eng.reset();
        if (rng.nextFloat() < 0.02f) eng.prepare (rates[ri], 1024);

        Patch p;
        randomise (p, true);
        const double bpm = rng.nextFloat() < 0.05 ? (rng.nextBool() ? 0.0 : 1.0e12) : 40.0 + 260.0 * rng.nextDouble();
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        const auto t0 = juce::Time::getHighResolutionTicks();

        for (int pos = 0; pos < total; pos += block)
        {
            midi.clear();
            const int events = pos == 0 ? 2 + rng.nextInt (20) : (rng.nextFloat() < 0.15f ? rng.nextInt (6) : 0);
            for (int e = 0; e < events; ++e)
            {
                const int at = rng.nextInt (block);
                const float r = rng.nextFloat();
                const int note = rng.nextFloat() < 0.1f ? rng.nextInt (128) : 24 + rng.nextInt (72);
                if (r < 0.55f)      midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (1 + rng.nextInt (127))), at);
                else if (r < 0.75f) midi.addEvent (juce::MidiMessage::noteOff (1, note), at);
                else if (r < 0.82f) midi.addEvent (juce::MidiMessage::pitchWheel (1, rng.nextInt (16384)), at);
                else if (r < 0.88f) midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, rng.nextInt (128)), at);
                else if (r < 0.94f) midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, rng.nextInt (128)), at);
                else if (r < 0.96f) midi.addEvent (juce::MidiMessage::allNotesOff (1), at);
                else if (r < 0.97f) midi.addEvent (juce::MidiMessage::allSoundOff (1), at);
                else                midi.addEvent (juce::MidiMessage::controllerEvent (1, rng.nextInt (128), rng.nextInt (128)), at);
            }
            if (rng.nextFloat() < 0.03f) randomise (p, true);                           // parameter jumps mid-note
            if (rng.nextFloat() < 0.2f)  p.v[(size_t) rng.nextInt (kNumParams)] = paramDef (rng.nextInt (kNumParams)).max;

            buf.setSize (2, block, false, false, true);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) buf.setSample (c, i, nan);     // output must be overwritten
            eng.process (buf, midi, p.v.data(), bpm);
            for (int c = 0; c < 2; ++c)
            {
                const float* d = buf.getReadPointer (c);
                for (int i = 0; i < block; ++i)
                {
                    if (! std::isfinite (d[i])) { ++bad; if (firstBad < 0) firstBad = it; }
                    else { const double a = std::abs (d[i]); if (a > 1.0) ++over; maxPeak = juce::jmax (maxPeak, a); }
                }
            }
            samples += block;
        }
        cpu += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        audio += total / rates[ri];
    }
    t.check (bad == 0, juce::String (iterations) + " random parameter sets x random MIDI, 5 sample rates, 8 block sizes: no NaN / Inf",
             juce::String (samples) + " samples, " + (bad ? "first failure in set " + juce::String (firstBad) : juce::String ("all finite")));
    t.check (dsp::guardTrips().load() == tripsBefore, "internal NaN safety nets never had to fire",
             juce::String (dsp::guardTrips().load() - tripsBefore) + " trips");
    t.check (over == 0, "output never exceeds 0 dBFS under fuzzing", "max peak " + num (maxPeak, 4) + ", avg speed " + num (audio / cpu, 1) + "x realtime");

    // every engine must still be healthy afterwards
    bool healthy = true;
    juce::String detail;
    for (int i = 0; i < 5; ++i)
    {
        engines[i]->reset();
        const auto a = renderEngine (*engines[i], cleanPatch (wSaw), oneNote (69, 0.6, 0.4), rates[i], 256, 120.0, {}, false);
        const double f = f0At (a, 0.2, 0.55);
        healthy = healthy && countNonFinite (a) == 0 && std::abs (cents (f, 440.0)) < 2.0 && a.endVoices == 0;
        detail << num (f, 1) << " ";
    }
    t.check (healthy, "engines recover to a clean A4 after fuzzing (all sample rates)", detail.trim() + " Hz");

    {
        // mono output bus + oversized block: no crash, sensible audio
        dsp::SynthEngine eng;
        eng.prepare (48000.0, 64);
        juce::AudioBuffer<float> mono (1, 4096);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 100);
        Patch p;
        eng.process (mono, midi, p.v.data(), 120.0);
        t.check (mono.getMagnitude (0, 4096) > 0.05f && mono.getMagnitude (0, 100) <= 0.0f, "mono bus and blocks larger than prepared are handled");
    }
}

void testProcessor (TestRun& t)
{
    t.section ("plugin processor / licence gate");
    WhyshoSynthAudioProcessor proc;
    const bool licensed = ensureLicensed (proc);
    t.check (licensed, "trial licence is active for rendering", proc.licenseInfo.reason);

    proc.presetManager().loadInit();
    for (int i = 0; i < kNumParams; ++i)      // loadInit may be a stub: force defaults explicitly
        if (auto* prm = proc.state().getParameter (pid (i)))
            prm->setValueNotifyingHost (prm->convertTo0to1 (paramDef (i).def));

    Phrase ph;
    for (int i = 0; i < 12; ++i) ph.notes.push_back ({ 48 + (i * 7) % 24, i * 1.0, 0.9, 100 });
    ph.tail = 0.5;
    const auto viaPlugin = renderProcessor (proc, ph, 48000.0, 512, 120.0);
    const auto direct = renderEngine (Patch(), ph, 48000.0, 512, 120.0);
    double worst = 0.0;
    for (int i = 0; i < direct.size(); ++i)
        worst = juce::jmax (worst, (double) std::abs (viaPlugin.l[(size_t) i] - direct.l[(size_t) i]), (double) std::abs (viaPlugin.r[(size_t) i] - direct.r[(size_t) i]));
    t.check (worst < 1.0e-6, "12.5 s through processBlock is identical to the bare engine (no degradation mutes / noise)",
             "max diff " + juce::String (worst, 9));

    juce::String err;
    const bool ok = setParam (proc, "osc1_wave", "Sine", err) && setParam (proc, "flt_cutoff", "1234.5", err) && ! setParam (proc, "nope", "1", err);
    t.check (ok && std::abs (proc.state().getRawParameterValue ("flt_cutoff")->load() - 1234.5f) < 0.01f
                 && (int) proc.state().getRawParameterValue ("osc1_wave")->load() == wSine, "--set reaches the parameters (raw values and choice names)");
}

int runSelfTest (bool quick)
{
    TestRun t;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    std::cout << "whysho SY1 engine self-test\n";
    testSilenceAndLevels (t);
    testTuning (t);
    testModulation (t);
    testBlockInvariance (t);
    testAliasing (t);
    testFilter (t);
    testEnvelopes (t);
    testVoiceModes (t);
    testVoiceStealing (t);
    testEffects (t);
    testFuzz (t, quick ? 400 : 4000);
    testProcessor (t);

    std::cout << "\n";
    runBench();
    std::cout << "\n" << t.checks - t.failures << " / " << t.checks << " checks passed in "
              << num ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) << " s — "
              << (t.failures == 0 ? "SELFTEST PASSED" : "SELFTEST FAILED") << "\n";
    return t.failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
void printUsage()
{
    std::cout <<
        "whysho SY1 render / QA tool\n"
        "  Render --out file.wav [--preset \"Bank/Preset\"] [--set id=value ...]\n"
        "         [--notes 36,48,60] [--vel 100] [--len 1.0] [--tail 2.0]\n"
        "         [--seq \"note:start:len[:vel],...\"] [--bpm 140] [--sr 48000] [--block 512] [--json]\n"
        "  Render --list-presets | --list-params\n"
        "  Render --all-presets <dir> [--sr 48000] [--bpm 140] [--block 512] [--json]\n"
        "  Render --bench\n"
        "  Render --selftest [--quick]\n";
}

bool findPreset (PresetManager& pm, const juce::String& path, int& bankOut, int& presetOut)
{
    const auto bankName = path.upToFirstOccurrenceOf ("/", false, false).trim();
    const auto presetName = path.fromFirstOccurrenceOf ("/", false, false).trim();
    for (int pass = 0; pass < 2; ++pass)      // exact "Bank/Preset" first, then a bare preset name in any bank
        for (int b = 0; b < pm.numBanks(); ++b)
        {
            const auto& bank = pm.bank (b);
            if (pass == 0 && ! bank.name.equalsIgnoreCase (bankName)) continue;
            const auto& want = pass == 0 ? presetName : path.trim();
            for (int i = 0; i < (int) bank.presets.size(); ++i)
                if (bank.presets[(size_t) i].name.equalsIgnoreCase (want)) { bankOut = b; presetOut = i; return true; }
        }
    return false;
}
} // namespace

// ---------------------------------------------------------------------------
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));

    auto has = [&] (const char* flag) { return args.contains (flag); };
    auto value = [&] (const char* flag, const juce::String& fallback = {})
    {
        const int i = args.indexOf (flag);
        return (i >= 0 && i + 1 < args.size()) ? args[i + 1] : fallback;
    };

    if (args.isEmpty() || has ("--help") || has ("-h")) { printUsage(); return args.isEmpty() ? 1 : 0; }
    if (has ("--selftest")) return runSelfTest (has ("--quick"));
    if (has ("--bench"))    { runBench(); return 0; }

    if (has ("--list-params"))
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& d = paramDef (i);
            std::cout << std::left << std::setw (14) << d.id << std::setw (20) << d.name << d.min << " .. " << d.max
                      << "  (default " << d.def << (d.unit[0] ? juce::String (" ") + d.unit : juce::String()) << ")";
            if (d.choices != nullptr) std::cout << "  [" << d.choices << "]";
            std::cout << "\n";
        }
        return 0;
    }

    const double sr  = juce::jlimit (8000.0, 384000.0, value ("--sr", "48000").getDoubleValue());
    const int block  = juce::jlimit (1, 16384, value ("--block", "512").getIntValue());
    const double bpm = juce::jlimit (20.0, 999.0, value ("--bpm", "140").getDoubleValue());
    const bool json  = has ("--json");

    WhyshoSynthAudioProcessor proc;
    auto& pm = proc.presetManager();

    if (has ("--list-presets"))
    {
        juce::Array<juce::var> list;
        for (int b = 0; b < pm.numBanks(); ++b)
        {
            const auto& bank = pm.bank (b);
            for (auto& p : bank.presets)
            {
                if (json)
                {
                    auto* o = new juce::DynamicObject();
                    o->setProperty ("bank", bank.name); o->setProperty ("preset", p.name); o->setProperty ("category", p.category);
                    list.add (juce::var (o));
                }
                else std::cout << bank.name << "/" << p.name << "   [" << p.category << "]\n";
            }
        }
        if (json) std::cout << juce::JSON::toString (juce::var (list)) << "\n";
        else if (pm.numBanks() == 0) std::cout << "(no banks)\n";
        return 0;
    }

    const bool licensed = ensureLicensed (proc);

    if (has ("--all-presets"))
    {
        const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (value ("--all-presets", "renders"));
        dir.createDirectory();
        juce::Array<juce::var> list;
        int problems = 0, count = 0;
        for (int b = 0; b < pm.numBanks(); ++b)
        {
            const auto bank = pm.bank (b);      // copy: load() may notify / rescan
            for (int i = 0; i < (int) bank.presets.size(); ++i)
            {
                const auto& preset = bank.presets[(size_t) i];
                pm.load (b, i);
                proc.reset();
                const auto ph = phraseForCategory (preset.category, bpm);
                const auto audio = renderProcessor (proc, ph, sr, block, bpm);
                const auto st = analyse (audio, ph);
                const auto file = dir.getChildFile (juce::File::createLegalFileName (bank.name + "__" + preset.name) + ".wav");
                const bool wrote = writeWav (file, audio);
                const bool bad = st.nonFinite > 0 || ! wrote || st.peakDb < -60.0;
                problems += bad ? 1 : 0;
                ++count;
                if (json) list.add (statsJson (st, bank.name + "/" + preset.name, file.getFullPathName()));
                else std::cout << std::left << std::setw (44) << (bank.name + "/" + preset.name) << " [" << std::setw (8) << preset.category << "] "
                               << statsLine (st) << (bad ? "   <-- CHECK" : "") << "\n" << std::flush;
            }
        }
        if (json) std::cout << juce::JSON::toString (juce::var (list)) << "\n";
        else std::cout << count << " presets rendered to " << dir.getFullPathName() << ", " << problems << " flagged"
                       << (licensed ? "" : " (UNLICENSED: output degraded)") << "\n";
        return problems == 0 ? 0 : 2;
    }

    // ---- single render
    if (! has ("--out")) { printUsage(); return 1; }

    juce::String presetLabel;
    if (has ("--preset"))
    {
        int b = -1, i = -1;
        if (! findPreset (pm, value ("--preset"), b, i))
        {
            std::cerr << "preset not found: " << value ("--preset") << " (try --list-presets)\n";
            return 1;
        }
        pm.load (b, i);
        presetLabel = pm.bank (b).name + "/" + pm.bank (b).presets[(size_t) i].name;
    }
    for (int i = 0; i + 1 < args.size(); ++i)
        if (args[i] == "--set")
        {
            juce::String err;
            const auto kv = args[i + 1];
            if (! kv.contains ("=") || ! setParam (proc, kv.upToFirstOccurrenceOf ("=", false, false).trim(), kv.fromFirstOccurrenceOf ("=", false, false), err))
            {
                std::cerr << "--set " << kv << ": " << (err.isEmpty() ? juce::String ("expected id=value") : err) << " (try --list-params)\n";
                return 1;
            }
        }

    Phrase ph;
    const int vel = juce::jlimit (1, 127, value ("--vel", "100").getIntValue());
    const double len = juce::jmax (0.001, value ("--len", "1.0").getDoubleValue());
    ph.tail = juce::jmax (0.0, value ("--tail", "2.0").getDoubleValue());
    if (has ("--seq"))
    {
        if (! parseSeq (value ("--seq"), vel, ph.notes)) { std::cerr << "bad --seq (expected note:start:len[:vel],...)\n"; return 1; }
    }
    else
    {
        for (auto& tok : juce::StringArray::fromTokens (value ("--notes", "60"), ",", ""))
            if (tok.trim().isNotEmpty()) ph.notes.push_back ({ juce::jlimit (0, 127, tok.getIntValue()), 0.0, len, vel });
    }

    const auto audio = renderProcessor (proc, ph, sr, block, bpm);
    const auto st = analyse (audio, ph);
    const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile (value ("--out"));
    if (! writeWav (file, audio)) { std::cerr << "could not write " << file.getFullPathName() << "\n"; return 1; }

    if (json)
    {
        auto v = statsJson (st, presetLabel, file.getFullPathName());
        v.getDynamicObject()->setProperty ("licensed", licensed);
        std::cout << juce::JSON::toString (v) << "\n";
    }
    else
    {
        std::cout << file.getFullPathName() << "  (" << num (audio.seconds(), 2) << " s, " << (int) sr << " Hz"
                  << (presetLabel.isNotEmpty() ? ", " + presetLabel : juce::String()) << ")\n" << statsLine (st) << "\n";
    }
    return st.nonFinite == 0 ? 0 : 2;
}
