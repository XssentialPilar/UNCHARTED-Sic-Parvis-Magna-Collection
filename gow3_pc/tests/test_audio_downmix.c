/* 7.1 to stereo fold (src/runtime_audio.c). */
#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include "../src/runtime_audio.c"

uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}

static int near(float a, float b) { return fabsf(a-b)<1e-3f; }

/* One float frame in SDL order (FL FR C LFE BL BR SL SR) folded to stereo. */
static void fold_one(const float in[8], float out[2]) {
    unsigned char buf[8*4];
    memcpy(buf,in,sizeof(buf));
    assert(downmix_stereo(buf,1,1)==2*4);
    memcpy(out,buf,2*4);
}

int main(void) {
    float lr[2];
    /* Front channels pass at full level (SDL kept only ~0.21 of them). */
    fold_one((const float[8]){0.5f,0,0,0,0,0,0,0},lr); assert(near(lr[0],0.5f) && near(lr[1],0));
    fold_one((const float[8]){0,0.5f,0,0,0,0,0,0},lr); assert(near(lr[0],0) && near(lr[1],0.5f));
    /* Center and surrounds at -3 dB; LFE dropped. */
    fold_one((const float[8]){0,0,0.5f,0,0,0,0,0},lr); assert(near(lr[0],0.35355f) && near(lr[1],0.35355f));
    fold_one((const float[8]){0,0,0,1,0,0,0,0},lr); assert(near(lr[0],0) && near(lr[1],0));
    fold_one((const float[8]){0,0,0,0,0.5f,0,0.5f,0},lr); assert(near(lr[0],0.70711f) && near(lr[1],0));
    /* A loud mix is clamped instead of going past full scale. */
    fold_one((const float[8]){1,-1,1,0,1,-1,1,-1},lr); assert(lr[0]==1.0f && lr[1]==-1.0f);

    /* s16 frames, several in one buffer folded in place. */
    int16_t s[2*8]={16384,0,0,0,0,0,0,0, 32767,32767,32767,0,32767,32767,32767,32767};
    assert(downmix_stereo((unsigned char *)s,2,0)==2*2*2);
    assert(s[0]==16383 && s[1]==0);
    assert(s[2]==32767 && s[3]==32767);

    /* The endpoint stream is PCM, never a compressed bitstream: a float port keeps
       32-bit float samples and an integer port 16-bit, and the channel count is the
       port's own unless it is folded to the device's 2 or 6 channels. */
    assert(pcm_stream_format(1)==SDL_AUDIO_F32);
    assert(pcm_stream_format(0)==SDL_AUDIO_S16);
    assert(pcm_stream_channels(0,8)==8);
    assert(pcm_stream_channels(2,8)==2);
    assert(pcm_stream_channels(6,8)==6);
    assert(pcm_stream_channels(0,2)==2);
    puts("audio downmix ok");
    return 0;
}
