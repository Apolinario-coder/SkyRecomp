// Exercise the real voice decoder/mixer with deterministic PCM, independent of a device.
// clang -O1 -ffunction-sections -fdata-sections tools/test_audio_resample.c -Wl,--gc-sections -lSDL3 -o build/test_audio_resample.exe
#include <assert.h>
#include "../runtime/services/audio.c"

Settings g_settings = {.volume = 1};
pthread_mutex_t g_klock = PTHREAD_MUTEX_INITIALIZER;
Service *service_new(const char *name, ServiceHandler h, void *state) { abort(); }
void ipc_return_object(IpcResp *resp, Service *s) { abort(); }
static Renderer *pacing_renderer;
static uint64_t signal_time[80];
static int signal_count;
void kernel_signal(KObject *o) {
    assert(pacing_renderer);
    signal_time[signal_count++] = SDL_GetTicksNS();
    if (signal_count == 80) pacing_renderer->quit = true;
}
KEvent *kernel_create_event_pair(uint32_t *writable, uint32_t *readable) { abort(); }

static void check_rate(uint32_t source_rate, float pitch, uint16_t behaviour, int consumed) {
    enum { N = 160, COUNT = 8192 };
    int16_t pcm[COUNT];
    for (int i = 0; i < COUNT; i++) pcm[i] = (int16_t)(12000 * sin(2 * M_PI * i / 32));
    Voice voice = {.in_use = 1, .fmt = 2, .chans = 1, .rate = source_rate, .pitch = pitch,
                   .volume = 1, .prev_volume = 1, .mix_id = 0, .wb_count = 1, .behaviour = behaviour};
    voice.wb[0] = (WaveBuf){.addr = (uintptr_t)pcm, .size = sizeof pcm, .end = COUNT, .sent = 1};
    ChanState cs = {.valid = {1}};
    ChanRes resource = {.mix = {1, 1}, .prev = {1, 1}};
    float mix_buf[MIXBUF_MAX * N] = {0}, out[2 * N];
    Mix mix = {.used = 1, .volume = 1, .bufs = 2, .buf = mix_buf};
    Renderer r = {.sample_rate = 32000, .sample_count = N, .voices = 1, .voice = &voice,
                  .chst = &cs, .chres = &resource, .mix = &mix, .nmix = 1,
                  .sink_used = 1, .sink_inputs = 2, .sink_input = {0, 1}};
    render_frame(&r, out);
    if (behaviour & 2) for (int i = 0; i < N; i++)
        assert(fabsf(out[2 * i] - pcm[i] / 32768.0f) < 0.00001f);
    if (cs.played != (uint64_t)consumed)
        fprintf(stderr, "rate=%u pitch=%g flags=%u: consumed %llu, expected %d\n", source_rate, pitch, behaviour,
                (unsigned long long)cs.played, consumed);
    assert(cs.played == (uint64_t)consumed);
    // Accumulate over successive blocks: pitch/rate must determine source duration.
    for (int frame = 1; frame < 4; frame++) render_frame(&r, out);
    assert(cs.played == (uint64_t)consumed * 4);
    double energy = 0;
    for (int i = 0; i < 2 * N; i++) { assert(isfinite(out[i])); energy += out[i] * out[i]; }
    assert(energy > 1);
}

static void check_pacing(void) {
    // A 10 ms output-device period must not batch the guest's 5 ms audio events.
    // Sequence players use those events as their clock, even though PCM playback
    // can sound correct when several renderer blocks are generated in a burst.
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "441");
    Renderer r = {.sample_rate = 32000, .sample_count = 160};
    pthread_mutex_init(&r.lock, NULL);
    renderer_open_output(&r);
    assert(r.stream);
    pacing_renderer = &r;
    renderer_tick(&r);
    int bursts = 0;
    for (int i = 1; i < signal_count; i++)
        if (signal_time[i] - signal_time[i - 1] < 1000000) bursts++;
    fprintf(stderr, "audio clock: %d signals, %d gaps below 1 ms, %.1f ms elapsed\n",
            signal_count, bursts, (signal_time[79] - signal_time[0]) / 1e6);
    SDL_DestroyAudioStream(r.stream);
    pthread_mutex_destroy(&r.lock);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    pacing_renderer = NULL;
    assert(bursts < 8);
}

static void check_splitters(void) {
    // The title menu sends four 40-byte state records before destinations.
    // Unused bytes retain an SNDD header at offset 144. The old stride-based
    // parser treated the following SNDI magic (float 804069) as a left gain.
    uint8_t data[0x20 + 4 * 40 + 4 * 0x70] = {0};
    uint32_t magic = 0x48444E53, count = 4;
    memcpy(data, &magic, 4);
    memcpy(data + 4, &count, 4);
    memcpy(data + 8, &count, 4);
    for (int i = 0; i < 4; i++) {
        uint8_t *p = data + 0x20 + i * 40;
        uint32_t words[] = {0x49444E53, (uint32_t)i, 32000, 3, 0, 1, 2};
        memcpy(p, words, sizeof words);
    }
    uint32_t stale[] = {0x44444E53, 3};
    memcpy(data + 144, stale, sizeof stale);
    for (int i = 0; i < 4; i++) {
        uint8_t *p = data + 192 + i * 0x70;
        uint32_t words[] = {0x44444E53, (uint32_t)i * 3};
        float gains[] = {.25f, .75f};
        memcpy(p, words, sizeof words);
        memcpy(p + 8, gains, sizeof gains);
    }
    SplitterDest destinations[16] = {0};
    Renderer r = {.nspl_dest = 16, .spl_dest = destinations};
    update_splitters(&r, data, sizeof data);
    for (int i = 0; i < 4; i++) {
        assert(destinations[i * 3].mix[0] == .25f);
        assert(destinations[i * 3].mix[1] == .75f);
    }
    // Empty delta updates preserve existing routes. Truncated state records
    // and oversized ID counts must never be interpreted as destinations.
    memset(data + 4, 0, 8);
    update_splitters(&r, data, 0x20);
    assert(destinations[3].mix[0] == .25f);
    memcpy(data + 4, &count, 4);
    memcpy(data + 8, &count, 4);
    for (size_t n = 0; n < sizeof data; n++) update_splitters(&r, data, n);
    uint32_t oversized = UINT32_MAX;
    memcpy(data + 0x20 + 12, &oversized, 4);
    update_splitters(&r, data, sizeof data);
    assert(destinations[3].mix[0] == .25f);
    puts("audio: variable splitter records, stale header and bounds passed");
}

static void check_formats(void) {
    // Every PCM sample format decodes to the same int16 stream as PCM16 (stereo, so the interleave stride is checked)
    enum { COUNT = 64 };
    int16_t ref[COUNT * 2];
    for (int i = 0; i < COUNT * 2; i++) ref[i] = (int16_t)(i * 1021 - 32768);
    uint8_t buf[COUNT * 2 * 4];
    for (int fmt = 1; fmt <= 5; fmt++) {
        for (int i = 0; i < COUNT * 2; i++) {
            int32_t v = ref[i] * 65536 + 0x7fff;  // low bits below the int16 range must be dropped, not rounded up
            float f = ref[i] / 32767.f;
            switch (fmt) {
            case 1: buf[i] = (uint8_t)(ref[i] >> 8); break;
            case 2: memcpy(buf + i * 2, &ref[i], 2); break;
            case 3: memcpy(buf + i * 3, (uint8_t *)&v + 1, 3); break;
            case 4: memcpy(buf + i * 4, &v, 4); break;
            case 5: memcpy(buf + i * 4, &f, 4); break;
            }
        }
        static const int bps[6] = {0, 1, 2, 3, 4, 4};
        Voice voice = {.fmt = (uint8_t)fmt, .chans = 2};
        voice.wb[0] = (WaveBuf){.addr = (uintptr_t)buf, .size = (uint64_t)COUNT * 2 * bps[fmt], .end = COUNT};
        for (int ch = 0; ch < 2; ch++) {
            ChanState cs = {.valid = {1}};
            int16_t out[COUNT];
            assert(decode_voice(&voice, ch, &cs, out, COUNT) == COUNT);
            for (int i = 0; i < COUNT; i++) {
                int want = fmt == 1 ? ref[i * 2 + ch] & ~0xff : ref[i * 2 + ch];
                if (abs(out[i] - want) > (fmt == 5)) fprintf(stderr, "fmt %d ch %d [%d]: %d != %d\n", fmt, ch, i, out[i], want);
                assert(abs(out[i] - want) <= (fmt == 5));
            }
        }
    }
    puts("audio: PCM int8/16/24/32/float decode passed");
}

static void check_depop(void) {
    // A voice stopped mid-wave must fade from its last level, not step to 0 (the click), and reach exactly 0
    enum { N = 160, COUNT = 8192 };
    static int16_t pcm[COUNT];
    for (int i = 0; i < COUNT; i++) pcm[i] = 16000;
    Voice voice = {.in_use = 1, .fmt = 2, .chans = 1, .rate = 32000, .pitch = 1,
                   .volume = 1, .prev_volume = 1, .mix_id = 0, .wb_count = 1};
    voice.wb[0] = (WaveBuf){.addr = (uintptr_t)pcm, .size = sizeof pcm, .end = COUNT, .sent = 1};
    ChanState cs = {.valid = {1}};
    ChanRes resource = {.mix = {1, 1}, .prev = {1, 1}};
    float mix_buf[MIXBUF_MAX * N] = {0}, out[2 * N];
    Mix mix = {.used = 1, .volume = 1, .bufs = 2, .buf = mix_buf};
    Renderer r = {.sample_rate = 32000, .sample_count = N, .voices = 1, .voice = &voice,
                  .chst = &cs, .chres = &resource, .mix = &mix, .nmix = 1,
                  .sink_used = 1, .sink_inputs = 2, .sink_input = {0, 1}};
    render_frame(&r, out);
    assert(fabsf(out[2 * (N - 1)] - 16000 / 32768.f) < 1e-4f);
    voice.play_state = 2;  // stop
    render_frame(&r, out);
    assert(out[0] > 0.9f * 16000 / 32768.f && out[0] < 16000 / 32768.f);  // one decay step, not silence
    for (int i = 1; i < 2 * N; i += 2) assert(out[i - 1] == out[i]);    // both channels
    for (int i = 2; i < 2 * N; i += 2) assert(out[i] <= out[i - 2]);
    assert(out[2 * (N - 1)] == 0 && mix.depop[0] == 0 && mix.depop[1] == 0);
    render_frame(&r, out);  // nothing left over
    for (int i = 0; i < 2 * N; i++) assert(out[i] == 0);
    puts("audio: depop on stop passed");
}

int main(void) {
    check_rate(32000, 1, 0, 160);
    check_rate(16000, 1, 0, 80);
    check_rate(32000, 2, 0, 320);
    check_rate(16000, .5f, 2, 160); // SkipPitchAndSampleRateConversion
    // Sequence notes far above a wave's root key (Ghirahim's thrown daggers: key 103 on a key-60 22 kHz wave, ratio ~8-18)
    // must still be resampled, not dropped back to 1:1.
    check_rate(32000, 12, 0, 1920);
    check_splitters();
    check_formats();
    check_depop();
    FILE *wav = tmpfile();
    assert(wav);
    wav_header(wav, 1280, 32000);
    uint32_t h[11];
    rewind(wav);
    assert(fread(h, sizeof h, 1, wav) == 1);
    assert(h[6] == 32000 && h[7] == 32000 * 2 * sizeof(float) && h[10] == 1280);
    fclose(wav);
    check_pacing();
    puts("audio: duration/rate/pitch and SRC bypass passed");
}
