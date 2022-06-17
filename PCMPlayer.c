
//#define NDEBUG

#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <pulse/pulseaudio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>

#include "Headers/LowSeikoSQ50.h"
#include "Headers/HighSeikoSQ50.h"


#define FORMAT PA_SAMPLE_S16LE
#define RATE 44100

struct userdata {
    int ud_iPos;
    unsigned char *ud_pcm_ptr;
    unsigned int ud_pcm_len;
};

void context_state_cb(pa_context* context, void* mainloop);
void stream_state_cb(pa_stream *s, void *mainloop);
void stream_success_cb(pa_stream *stream, int success, void *userdata);
void stream_write_cb(pa_stream *stream, size_t requested_bytes, void *userdata);

void wait_for_stream_drain(pa_threaded_mainloop *mainloop, pa_stream *stream);

/****************************************************************************
 * GLOBALs
 ****************************************************************************/
extern int GiDebug;

int PCMPlayFile(int instrument_index)
{
    pa_threaded_mainloop *mainloop;
    pa_mainloop_api *mainloop_api;
    pa_context *context;
    pa_stream *stream;
    int iRet = 0;
    
    struct userdata u = {};
    u.ud_iPos = 0;
    
    
    switch (instrument_index) {
        case 0: u.ud_pcm_ptr = LowSeikoSQ50; u.ud_pcm_len = LowSeikoSQ50_len; break;
        case 1: u.ud_pcm_ptr = HighSeikoSQ50; u.ud_pcm_len = HighSeikoSQ50_len; break;
        default : break;
    }
    
    if (GiDebug == 1) printf("PCMPlayer.c: START, instrument_index=%d\n", instrument_index);
    

    // Get a mainloop and its context
    mainloop = pa_threaded_mainloop_new();
    assert(mainloop);
    mainloop_api = pa_threaded_mainloop_get_api(mainloop);
    context = pa_context_new(mainloop_api, "pcm-playback");
    assert(context);

    // Set a callback so we can wait for the context to be ready
    pa_context_set_state_callback(context, &context_state_cb, mainloop);

    // Lock the mainloop so that it does not run and crash before the context is ready
    pa_threaded_mainloop_lock(mainloop);

    // Start the mainloop
    pa_threaded_mainloop_start(mainloop);
    pa_context_connect(context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL);

    // Wait for the context to be ready
    int cLoops=0;
    for (;;) {
        pa_context_state_t context_state = pa_context_get_state(context);
        if (GiDebug == 1) printf("PCMPlayer.c: X1.0, cLoops=%d, context_state=%d\n", cLoops, context_state);
        
        if (context_state == PA_CONTEXT_FAILED) {
            iRet = -1;
            fprintf(stderr, "PCMPlayer.c: X1.1, Connection failure: %s\n", pa_strerror(pa_context_errno(context)));
            break;
        } else {
            assert(PA_CONTEXT_IS_GOOD(context_state));
        
            if (context_state == PA_CONTEXT_READY) break;
            pa_threaded_mainloop_wait(mainloop);
            if (GiDebug == 1) printf("PCMPlayer.c: X1.2, cLoops=%d\n", cLoops);
        }
        
        ++cLoops;
    }
    
    if (iRet == 0) {        
        // Create a playback stream
        pa_sample_spec sample_specifications;
        sample_specifications.format = FORMAT;
        sample_specifications.rate = RATE;
        sample_specifications.channels = 2;

        pa_channel_map map;
        pa_channel_map_init_stereo(&map);

        stream = pa_stream_new(context, "Playback", &sample_specifications, &map);
        pa_stream_set_state_callback(stream, stream_state_cb, mainloop);
        pa_stream_set_write_callback(stream, stream_write_cb, &u);

        // recommended settings, i.e. server uses sensible values
        pa_buffer_attr buffer_attr; 
        buffer_attr.maxlength = (uint32_t) -1;
        buffer_attr.tlength = (uint32_t) -1;
        buffer_attr.prebuf = (uint32_t) -1;
        buffer_attr.minreq = (uint32_t) -1;

        // Settings copied as per the chromium browser source
        pa_stream_flags_t stream_flags;
        stream_flags = PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING | 
            PA_STREAM_NOT_MONOTONIC | PA_STREAM_AUTO_TIMING_UPDATE |
            PA_STREAM_ADJUST_LATENCY;

        // Connect stream to the default audio output sink
        pa_stream_connect_playback(stream, NULL, &buffer_attr, stream_flags, NULL, NULL);

        // Wait for the stream to be ready
        for (;;) {
            pa_stream_state_t stream_state = pa_stream_get_state(stream);
            assert(PA_STREAM_IS_GOOD(stream_state));
            if (stream_state == PA_STREAM_READY) break;
            pa_threaded_mainloop_wait(mainloop);
        }

        // Uncork the stream so it will start playing
        pa_stream_cork(stream, 0, stream_success_cb, mainloop);
        
        pa_threaded_mainloop_unlock(mainloop);

        // We could be doing other stuff here.

        // Wait for the stream to drain, then we know playback is finished.
        wait_for_stream_drain(mainloop, stream);
        
        pa_threaded_mainloop_stop(mainloop);
        
        pa_context_disconnect(context);
        pa_context_unref(context);
        
        pa_threaded_mainloop_free(mainloop);
        
        if (GiDebug == 1) printf("Done\n");
    } else {
        pa_threaded_mainloop_unlock(mainloop);
        pa_threaded_mainloop_stop(mainloop);
        pa_threaded_mainloop_free(mainloop);
    }
    
    return iRet;
}

void context_state_cb(pa_context* context, void* mainloop) {
    if (GiDebug == 1) printf("stream_context_cb\n");
    pa_threaded_mainloop_signal(mainloop, 0);
}

void stream_state_cb(pa_stream *s, void *mainloop) {
    if (GiDebug == 1) printf("stream_state_cb\n");
    pa_threaded_mainloop_signal(mainloop, 0);
}

void stream_write_cb(pa_stream *stream, size_t requested_bytes, void *userdata) {
    int bytes_remaining = requested_bytes;
    int iLoops = 0;
    
    struct userdata *u = (struct userdata*)userdata;
    
    //if (GiDebug == 1) printf("stream_write_cb: requested_bytes=%ld, GiPos=%d\n", requested_bytes, u->ud_iPos);
    
    if (u->ud_iPos >= u->ud_pcm_len) {
        //if (GiDebug == 1) printf("stream_write_cb: out of range\n");
        return;
    }
    
    
    while (bytes_remaining > 0) {
        uint8_t *buffer = NULL;
        size_t bytes_to_fill = 44100;
        size_t i;
        int newPos = u->ud_iPos;
        
        iLoops++;

        if (bytes_to_fill > bytes_remaining) bytes_to_fill = bytes_remaining;

        pa_stream_begin_write(stream, (void**) &buffer, &bytes_to_fill);

        for (i = 0; i < bytes_to_fill; i++) {
            buffer[i] = u->ud_pcm_ptr[newPos++];
            
            if (newPos >= u->ud_pcm_len) break;
        }

        pa_stream_write(stream, buffer, bytes_to_fill, NULL, 0LL, PA_SEEK_RELATIVE);

        bytes_remaining -= bytes_to_fill;
        
        u->ud_iPos = newPos;
    }
    
    //if (GiDebug == 1) printf("stream_write_cb: END, done in %d loops\n", iLoops);
}

void stream_success_cb(pa_stream *stream, int success, void *userdata) {
    return;
}


void my_drain_callback(pa_stream *s, int success, void *userdata) {
    pa_threaded_mainloop *m;
 
    m = userdata;
    assert(m);
 
    pa_threaded_mainloop_signal(m, 0);
}
 
void wait_for_stream_drain(pa_threaded_mainloop *m, pa_stream *s) {
    pa_operation *o;
    
    if (GiDebug == 1) printf("wait_for_stream_drain: START\n");
 
    pa_threaded_mainloop_lock(m);
 
    o = pa_stream_drain(s, my_drain_callback, m);
    assert(o);
 
    while (pa_operation_get_state(o) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(m);
 
    pa_operation_unref(o);
 
    pa_threaded_mainloop_unlock(m);
    
    if (GiDebug == 1) printf("wait_for_stream_drain: END\n");
}
