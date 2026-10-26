/* NX Player — MIT License
 * Catalogue DSP plug-in: bass / treble shelving filters (RBJ cookbook, 64-bit) and left / right balance.
 * Same C ABI as Plugins/DSP/crossfeed (see nxplayer_dsp.h). Built by CMake (target nx_tone).
 */
#include "nxplayer_dsp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { double b0, b1, b2, a1, a2; } biquad;
typedef struct { double z1, z2; } state;

typedef struct
{
    double rate;
    double bass_db, treble_db, balance;
    biquad low, high;
    state st[2][2];
    double gain[2];
} tone;

static const nx_dsp_param params[] = {
    { "bass",    "Graves (105 Hz)", "dB", -12.0, 12.0, 0.0 },
    { "treble",  "Aigus (8 kHz)",   "dB", -12.0, 12.0, 0.0 },
    { "balance", "Balance",         "%",  -100.0, 100.0, 0.0 }
};

static biquad shelf (double rate, double f0, double gain_db, int high)
{
    const double pi = 3.14159265358979323846;
    const double A = pow (10.0, gain_db / 40.0);
    const double w0 = 2.0 * pi * f0 / rate;
    const double cw = cos (w0), sw = sin (w0);
    const double alpha = sw / 2.0 * sqrt (2.0);   /* shelf slope S = 1 */
    const double sq = 2.0 * sqrt (A) * alpha;
    double b0, b1, b2, a0, a1, a2;
    biquad q;
    if (! high)
    {
        b0 = A * ((A + 1) - (A - 1) * cw + sq);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - sq);
        a0 = (A + 1) + (A - 1) * cw + sq;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - sq;
    }
    else
    {
        b0 = A * ((A + 1) + (A - 1) * cw + sq);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - sq);
        a0 = (A + 1) - (A - 1) * cw + sq;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - sq;
    }
    q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
    return q;
}

static void update (tone* t)
{
    const double b = t->balance / 100.0;
    double f = 8000.0;
    if (f > t->rate * 0.45) f = t->rate * 0.45;
    t->low = shelf (t->rate, 105.0, t->bass_db, 0);
    t->high = shelf (t->rate, f, t->treble_db, 1);
    /* constant level in the centre, the opposite side fades out */
    t->gain[0] = b > 0.0 ? 1.0 - b : 1.0;
    t->gain[1] = b < 0.0 ? 1.0 + b : 1.0;
}

static void* create (double sample_rate, int num_channels, int max_block)
{
    tone* t = (tone*) calloc (1, sizeof (tone));
    (void) num_channels; (void) max_block;
    if (t == NULL) return NULL;
    t->rate = sample_rate;
    t->bass_db = params[0].default_value;
    t->treble_db = params[1].default_value;
    t->balance = params[2].default_value;
    update (t);
    return t;
}

static void destroy (void* p) { free (p); }
static void reset (void* p) { tone* t = (tone*) p; memset (t->st, 0, sizeof (t->st)); }

static void set_param (void* p, int index, double value)
{
    tone* t = (tone*) p;
    if (index == 0) t->bass_db = value;
    else if (index == 1) t->treble_db = value;
    else if (index == 2) t->balance = value;
    update (t);
}

static double run (const biquad* q, state* s, double x)
{
    /* transposed direct form II */
    const double y = q->b0 * x + s->z1;
    s->z1 = q->b1 * x - q->a1 * y + s->z2;
    s->z2 = q->b2 * x - q->a2 * y;
    return y;
}

static void process (void* p, double* const* ch, int num_channels, int num_frames)
{
    tone* t = (tone*) p;
    int c, i;
    const int n = num_channels < 2 ? num_channels : 2;
    for (c = 0; c < n; ++c)
        for (i = 0; i < num_frames; ++i)
        {
            double x = ch[c][i];
            x = run (&t->low, &t->st[c][0], x);
            x = run (&t->high, &t->st[c][1], x);
            ch[c][i] = x * t->gain[c];
        }
}

static int latency (void* p) { (void) p; return 0; }

static const nx_dsp_descriptor descriptor = {
    NX_DSP_API_VERSION,
    "nx.tone",
    "Tonalité & balance",
    "NX Audio Suite",
    "Correcteur graves / aigus (plateaux à 105 Hz et 8 kHz, ±12 dB) et balance gauche / droite.",
    3, params,
    create, destroy, process, set_param, reset, latency
};

NX_DSP_EXPORT const nx_dsp_descriptor* nx_dsp_get_descriptor (void) { return &descriptor; }
