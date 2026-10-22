/* NX Player — MIT License
 * Example DSP plug-in: headphone crossfeed (a low-passed, slightly delayed share of each channel is
 * fed to the other one, as with loudspeakers). Shows the whole C ABI in ~120 lines.
 * Build: CMake target nx_crossfeed (built with NX Player), or by hand:
 *   cc -O2 -shared -fPIC -I../../../Source/Ext crossfeed.c -o crossfeed.so -lm
 */
#include "nxplayer_dsp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DELAY 1024

typedef struct
{
    double rate;
    int channels;
    double level_db, cutoff_hz, delay_ms;
    double gain, alpha, norm;
    int delay;
    double lp[2];
    double ring[2][MAX_DELAY];
    int pos;
} crossfeed;

static const nx_dsp_param params[] = {
    { "level",  "Niveau de diaphonie", "dB", -15.0, -2.0, -6.0 },
    { "cutoff", "Fréquence de coupure", "Hz", 300.0, 1500.0, 700.0 },
    { "delay",  "Retard interaural",   "ms", 0.0, 0.6, 0.27 }
};

static void update (crossfeed* c)
{
    const double pi = 3.14159265358979323846;
    c->gain = pow (10.0, c->level_db / 20.0);
    c->alpha = 1.0 - exp (-2.0 * pi * c->cutoff_hz / c->rate);
    c->norm = 1.0 / (1.0 + c->gain);
    c->delay = (int) (c->delay_ms * 0.001 * c->rate + 0.5);
    if (c->delay >= MAX_DELAY) c->delay = MAX_DELAY - 1;
}

static void* create (double sample_rate, int num_channels, int max_block)
{
    crossfeed* c = (crossfeed*) calloc (1, sizeof (crossfeed));
    (void) max_block;
    if (c == NULL) return NULL;
    c->rate = sample_rate;
    c->channels = num_channels;
    c->level_db = params[0].default_value;
    c->cutoff_hz = params[1].default_value;
    c->delay_ms = params[2].default_value;
    update (c);
    return c;
}

static void destroy (void* p) { free (p); }

static void reset (void* p)
{
    crossfeed* c = (crossfeed*) p;
    c->lp[0] = c->lp[1] = 0.0;
    memset (c->ring, 0, sizeof (c->ring));
    c->pos = 0;
}

static void set_param (void* p, int index, double value)
{
    crossfeed* c = (crossfeed*) p;
    if (index == 0) c->level_db = value;
    else if (index == 1) c->cutoff_hz = value;
    else if (index == 2) c->delay_ms = value;
    update (c);
}

/* Real-time: in place, no allocation. Only the first two channels are crossfed. */
static void process (void* p, double* const* ch, int num_channels, int num_frames)
{
    crossfeed* c = (crossfeed*) p;
    int i;
    if (num_channels < 2) return;
    for (i = 0; i < num_frames; ++i)
    {
        const double l = ch[0][i], r = ch[1][i];
        int read = c->pos - c->delay;
        if (read < 0) read += MAX_DELAY;
        c->ring[0][c->pos] = l;
        c->ring[1][c->pos] = r;
        c->lp[0] += c->alpha * (c->ring[0][read] - c->lp[0]);   /* left, delayed and low-passed */
        c->lp[1] += c->alpha * (c->ring[1][read] - c->lp[1]);
        ch[0][i] = (l + c->gain * c->lp[1]) * c->norm;
        ch[1][i] = (r + c->gain * c->lp[0]) * c->norm;
        if (++c->pos == MAX_DELAY) c->pos = 0;
    }
}

static int latency (void* p) { (void) p; return 0; }

static const nx_dsp_descriptor descriptor = {
    NX_DSP_API_VERSION,
    "nx.crossfeed",
    "Crossfeed casque",
    "NX Audio Suite",
    "Réduit l'effet « dans la tête » au casque : une part filtrée et retardée de chaque voie passe dans l'autre.",
    3, params,
    create, destroy, process, set_param, reset, latency
};

NX_DSP_EXPORT const nx_dsp_descriptor* nx_dsp_get_descriptor (void) { return &descriptor; }
