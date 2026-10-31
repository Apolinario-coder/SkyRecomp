// Aux (AuxiliaryBuffer, effect type 2) round trip against fake send/return rings, independent of a device or the game.
// clang -O1 -I/opt/homebrew/include -Iruntime tools/test_audio_aux.c -L/opt/homebrew/lib -lSDL3 -o build/test_audio_aux
// Run it a second time with LARECOMP_AUDIO_NO_AUX=1 to check the A/B switch (the rings and mix must stay untouched).
#include <assert.h>
#include <sys/mman.h>
#include "../runtime/services/audio.c"

Settings g_settings = {.volume = 1, .reverb = true};  // (Room reverb off skips aux)
pthread_mutex_t g_klock = PTHREAD_MUTEX_INITIALIZER;
Service *service_new(const char *name, ServiceHandler h, void *state) { abort(); }
void ipc_return_object(IpcResp *resp, Service *s) { abort(); }
void kernel_signal(KObject *o) { abort(); }
KEvent *kernel_create_event_pair(uint32_t *writable, uint32_t *readable) { abort(); }

enum { N = 4, CH = 2, FRAMES = 3, RING = FRAMES * CH * N };  // 24 samples: wraps on the 4th frame
static int no_aux;

// The rings live in the guest window, where aux_update() insists they are (the runtime reserves it at startup).
typedef struct { uint32_t cpu[16], dsp[16]; int32_t ring[RING]; } GuestRing;  // 0x40 + 0x40 header, then samples
static GuestRing *guest_ring(int slot) {
    void *want = (void *)(uintptr_t)(GUEST_LO + 0x100000 * slot);
    void *p = mmap(want, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    assert(p == want);
    return p;
}

static void make_effect(uint8_t e[0xC0], int enabled, int mix, const int8_t in[CH], const int8_t out[CH], uint32_t ch,
                        uint32_t size, uint64_t send, uint64_t ret) {
    memset(e, 0, 0xC0);
    e[0] = 2, e[1] = 1, e[2] = (uint8_t)enabled;
    memcpy(e + 4, &mix, 4);  // order stays 0
    uint8_t *d = e + 0x20;
    memset(d, 0xFF, 48);  // unused channels: invalid indices
    memcpy(d, in, CH), memcpy(d + 0x18, out, CH);
    uint32_t rate = 32000;
    memcpy(d + 0x30, &ch, 4), memcpy(d + 0x34, &rate, 4), memcpy(d + 0x38, &size, 4), memcpy(d + 0x3C, &ch, 4);
    uint64_t send_storage = send + AUX_HDR, ret_storage = ret + AUX_HDR;
    memcpy(d + 0x40, &send, 8), memcpy(d + 0x48, &send_storage, 8);
    memcpy(d + 0x50, &ret, 8), memcpy(d + 0x58, &ret_storage, 8);
}

int main(void) {
    no_aux = getenv("LARECOMP_AUDIO_NO_AUX") != NULL;
    GuestRing *send = guest_ring(0), *ret = guest_ring(1);
    int8_t in[CH] = {0, 1}, out[CH] = {0, 2};  // channel 0 in place, channel 1 from buffer 1 to buffer 2
    uint8_t e[0xC0];
    Effect fx = {0};
    float buf[3 * N];
    Mix mix = {.used = 1, .volume = 1, .bufs = 3, .buf = buf};
    Renderer r = {.sample_count = N, .effects = 1, .fx = &fx, .nmix = 2, .mix = &mix};

    // --- enabled: parameters parse, input lands in the send ring as int32, the return ring lands in the output
    make_effect(e, 1, 1, in, out, CH, RING, (uintptr_t)send, (uintptr_t)ret);
    effect_update(&fx, e);
    assert(fx.type == 2 && fx.enabled && fx.ax.ok && fx.ax.ch == CH && fx.ax.size == RING);
    for (int i = 0; i < RING; i++) ret->ring[i] = 1000 + i;
    for (int frame = 0; frame < 2 * FRAMES; frame++) {  // 6 frames: the ring wraps once
        // samples above int16 range survive (int32), fractions round to nearest, negatives too
        float c0[N] = {100.4f, -7.6f, 100000.f, 3.5f}, c1[N] = {-1.f * frame, 2.f * frame, 0.f, -123456.f};
        memcpy(buf, c0, sizeof c0), memcpy(buf + N, c1, sizeof c1), memset(buf + 2 * N, 0, N * sizeof(float));
        uint32_t wo = send->dsp[1], ro = ret->dsp[0];
        uint32_t expect_wo = (frame * CH * N) % RING;
        if (!no_aux) assert(wo == expect_wo && ro == expect_wo);
        apply_effects(&r, 1, &mix);
        if (no_aux) {  // switch on: nothing moved
            assert(send->dsp[1] == wo && ret->dsp[0] == ro);
            assert(memcmp(buf, c0, sizeof c0) == 0 && memcmp(buf + N, c1, sizeof c1) == 0);
            continue;
        }
        // send ring: channel c's block at wo + c * N, wrapping at RING; CPU copy and the other DSP fields untouched
        const int32_t want0[N] = {100, -8, 100000, 4}, want1[N] = {-frame, 2 * frame, 0, -123456};
        for (int i = 0; i < N; i++) {
            assert(send->ring[(wo + i) % RING] == want0[i]);
            assert(send->ring[(wo + N + i) % RING] == want1[i]);
        }
        assert(send->dsp[1] == (wo + CH * N) % RING && send->dsp[0] == 0 && send->cpu[0] == 0 && send->cpu[1] == 0);
        // return ring: channel 0 replaced buffer 0 (the same buffer that was sent), channel 1 filled buffer 2; the input
        // of channel 1 stays, as the rounded int the DSP would hold
        for (int i = 0; i < N; i++) {
            assert(buf[i] == (float)ret->ring[(ro + i) % RING]);
            assert(buf[2 * N + i] == (float)ret->ring[(ro + N + i) % RING]);
            assert(buf[N + i] == (float)want1[i]);
        }
        assert(ret->dsp[0] == (ro + CH * N) % RING && ret->dsp[1] == 0);
        // the guest refills the return ring behind the read offset, as WriteAuxReturnBuffer would
        for (int i = 0; i < CH * N; i++) ret->ring[(ro + i) % RING] = 5000 * (frame + 1) + i;
    }
    // other mixes are none of this effect's business
    float before[3 * N];
    memcpy(before, buf, sizeof buf);
    apply_effects(&r, 0, &mix);
    assert(memcmp(before, buf, sizeof buf) == 0);

    if (no_aux) { puts("audio aux: NO_AUX leaves rings and mix alone"); return 0; }

    // --- return ring offset past the end (corrupt/garbage): silence in the output, no offset update, send still works
    ret->dsp[0] = RING + 1;
    for (int i = 0; i < 3 * N; i++) buf[i] = 9.f;
    uint32_t wo = send->dsp[1];
    apply_effects(&r, 1, &mix);
    for (int i = 0; i < N; i++) assert(buf[i] == 0 && buf[2 * N + i] == 0 && buf[N + i] == 9.f);
    assert(ret->dsp[0] == RING + 1 && send->dsp[1] == (wo + CH * N) % RING);

    // --- DSP offsets off the frame grid (a guest scribble): a block starting exactly at the ring end wraps to 0, one
    // starting past it skips the transfer and, on the last channel, the offset update too
    for (int i = 0; i < RING; i++) send->ring[i] = -1;
    for (int i = 0; i < N; i++) buf[i] = 7.f, buf[N + i] = 8.f;
    send->dsp[1] = RING - N;
    apply_effects(&r, 1, &mix);
    for (int i = 0; i < N; i++) assert(send->ring[RING - N + i] == 7 && send->ring[i] == 8);
    assert(send->dsp[1] == (RING - N + CH * N) % RING);
    for (int i = 0; i < RING; i++) send->ring[i] = -1;
    for (int i = 0; i < N; i++) buf[i] = 7.f, buf[N + i] = 8.f;
    send->dsp[1] = RING - 2;  // channel 0 wraps mid-block, channel 1 would start at RING + 2
    apply_effects(&r, 1, &mix);
    assert(send->ring[RING - 2] == 7 && send->ring[RING - 1] == 7 && send->ring[0] == 7 && send->ring[1] == 7);
    assert(send->ring[2] == -1 && send->ring[RING - 3] == -1 && send->dsp[1] == RING - 2);

    // --- disabled: DSP infos reset (CPU copies untouched), input passes through; in == out stays as it is
    send->dsp[0] = 5, send->dsp[1] = 7, send->dsp[2] = 9, ret->dsp[0] = 11, ret->dsp[1] = 13, ret->dsp[2] = 17;
    send->cpu[1] = ret->cpu[1] = 21;
    int32_t ring_before[RING];
    memcpy(ring_before, send->ring, sizeof ring_before);
    make_effect(e, 0, 1, in, out, CH, RING, (uintptr_t)send, (uintptr_t)ret);
    e[1] = 0;
    effect_update(&fx, e);
    assert(!fx.enabled);
    for (int i = 0; i < N; i++) buf[i] = 1.5f + i, buf[N + i] = -2.f - i, buf[2 * N + i] = 99.f;
    apply_effects(&r, 1, &mix);
    for (int i = 0; i < N; i++) assert(buf[i] == 1.5f + i && buf[N + i] == -2.f - i && buf[2 * N + i] == -2.f - i);
    for (int i = 0; i < 3; i++) assert(send->dsp[i] == 0 && ret->dsp[i] == 0);
    assert(send->cpu[1] == 21 && ret->cpu[1] == 21 && memcmp(ring_before, send->ring, sizeof ring_before) == 0);

    // --- re-enabled: restarts at offset 0
    make_effect(e, 1, 1, in, out, CH, RING, (uintptr_t)send, (uintptr_t)ret);
    e[1] = 0;
    effect_update(&fx, e);
    for (int i = 0; i < N; i++) buf[i] = 10.f + i;
    apply_effects(&r, 1, &mix);
    for (int i = 0; i < N; i++) assert(send->ring[i] == 10 + i);
    assert(send->dsp[1] == CH * N && ret->dsp[0] == CH * N);

    // --- parameters that must be rejected (and so never touched): null, outside the guest window, misaligned, too
    // big for the window, no channels, too many channels, empty ring
    GuestRing *spare = guest_ring(2);
    struct { uint64_t send, ret; uint32_t ch, size; } bad[] = {
        {0, (uintptr_t)ret, CH, RING},
        {(uintptr_t)send, 0, CH, RING},
        {(uintptr_t)&before, (uintptr_t)ret, CH, RING},
        {(uintptr_t)send, GUEST_HI - 0x40, CH, RING},
        {(uintptr_t)send + 2, (uintptr_t)ret, CH, RING},
        {(uintptr_t)send, (uintptr_t)ret, CH, 0xFFFFFFFF},
        {(uintptr_t)send, (uintptr_t)ret, 0, RING},
        {(uintptr_t)send, (uintptr_t)ret, 25, RING},
        {(uintptr_t)send, (uintptr_t)ret, CH, 0},
    };
    uint32_t wo_now = send->dsp[1];
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        make_effect(e, 1, 1, in, out, bad[i].ch, bad[i].size, bad[i].send, bad[i].ret);
        effect_update(&fx, e);
        assert(!fx.ax.ok);
        for (int j = 0; j < 3 * N; j++) buf[j] = 3.f;
        apply_effects(&r, 1, &mix);
        for (int j = 0; j < 3 * N; j++) assert(buf[j] == 3.f);
        assert(send->dsp[1] == wo_now);
    }
    // an invalid mix buffer index on an otherwise valid effect: skipped, not out of bounds
    int8_t bad_in[CH] = {0, 3};
    make_effect(e, 1, 1, bad_in, out, CH, RING, (uintptr_t)send, (uintptr_t)spare);
    effect_update(&fx, e);
    assert(fx.ax.ok);
    apply_effects(&r, 1, &mix);
    assert(send->dsp[1] == wo_now);
    puts("audio aux: send/return rings, wrap, disabled pass-through and parameter validation passed");
    return 0;
}
