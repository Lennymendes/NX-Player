/* NX Player — MIT License
 *
 * DSP plug-in ABI (plain C, stable across compilers). A DSP plug-in is a shared library
 * (.dll / .dylib / .so) placed in   Plugins/DSP/<name>/   next to a manifest.json, exporting:
 *
 *     NX_DSP_EXPORT const nx_dsp_descriptor* nx_dsp_get_descriptor (void);
 *
 * Contract
 *  - create() / destroy() / set_param() are called on the message thread, never on the audio thread.
 *  - process() runs on the real-time audio thread: no allocation, no lock, no I/O, no syscalls.
 *    Buffers are planar 64-bit floats, processed in place, full scale = ±1.0.
 *  - Any active DSP takes the stream out of bit-perfect mode; NX Player shows it in the Signal Path.
 *  - DSP plug-ins only run on PCM. DSD (native or DoP) bypasses them.
 */
#ifndef NXPLAYER_DSP_H
#define NXPLAYER_DSP_H

#ifdef __cplusplus
extern "C" {
#endif

#define NX_DSP_API_VERSION 1

#if defined(_WIN32)
  #define NX_DSP_EXPORT __declspec(dllexport)
#else
  #define NX_DSP_EXPORT __attribute__((visibility("default")))
#endif

typedef struct nx_dsp_param
{
    const char* id;          /* stable identifier, ASCII */
    const char* name;        /* UTF-8 display name */
    const char* unit;        /* "dB", "Hz", "%", "" */
    double min_value, max_value, default_value;
} nx_dsp_param;

typedef struct nx_dsp_descriptor
{
    int api_version;                         /* NX_DSP_API_VERSION */
    const char* id;
    const char* name;
    const char* author;
    const char* description;
    int num_params;
    const nx_dsp_param* params;

    void* (*create) (double sample_rate, int num_channels, int max_block_frames);
    void  (*destroy) (void* instance);
    void  (*process) (void* instance, double* const* channels, int num_channels, int num_frames);
    void  (*set_param) (void* instance, int index, double value);
    void  (*reset) (void* instance);
    int   (*latency_frames) (void* instance);  /* may be NULL */
} nx_dsp_descriptor;

typedef const nx_dsp_descriptor* (*nx_dsp_get_descriptor_fn) (void);

#ifdef __cplusplus
}
#endif

#endif /* NXPLAYER_DSP_H */
