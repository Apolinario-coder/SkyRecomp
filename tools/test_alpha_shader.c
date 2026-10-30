// Compile alpha comparisons and verify SPIR-V dual-source output locations/indices.
// clang tools/test_alpha_shader.c runtime/nvn/maxwell/translate.c -lshaderc_shared -o build/test_alpha_shader.exe
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shaderc/shaderc.h>
#include "../runtime/nvn/maxwell/shader.h"

int main(void) {
    uint64_t blob[24] = {0};
    ((uint32_t *)blob)[0x78 / 4] = 0xff; // two RGBA outputs: MRT or dual source
    blob[17] = 0xe30000000007000full; // EXIT, always
    blob[18] = 0xe2400fffff87000full; // BRA self: end of shader
    for (int dual = 0; dual < 2; dual++) {
        ShaderKey key = {.dual_source = dual};
        ShaderInfo *s = maxwell_translate((const uint8_t *)blob, sizeof blob, STAGE_FRAGMENT, &key);
        assert(s && s->msl);
        assert((strstr(s->msl, "color(0), index(1)") != NULL) == (bool)dual);
        assert((strstr(s->msl, "color(1)") != NULL) != (bool)dual);
        maxwell_free(s);
    }
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compile_options_t options = shaderc_compile_options_initialize();
    shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    for (int dual = 0; dual < 2; dual++) for (int depth = 0; depth < 2; depth++) for (int func = 0; func <= 8; func++) {
        ((uint32_t *)blob)[0x7c / 4] = depth << 1;
        ShaderKey key = {.glsl = 1, .alpha_func = (uint8_t)func, .dual_source = dual};
        ShaderInfo *s = maxwell_translate((const uint8_t *)blob, sizeof blob, STAGE_FRAGMENT, &key);
        if (!s || !s->msl) { fprintf(stderr, "translation: %s\n", s ? s->error : "no result"); return 1; }
        shaderc_compilation_result_t r = shaderc_compile_into_spv(compiler, s->msl, strlen(s->msl),
            shaderc_fragment_shader, "alpha", "main", options);
        if (shaderc_result_get_compilation_status(r) != shaderc_compilation_status_success) {
            fprintf(stderr, "%s\n%s", shaderc_result_get_error_message(r), s->msl);
            return 1;
        }
        const uint32_t *spv = (const uint32_t *)shaderc_result_get_bytes(r);
        size_t words = shaderc_result_get_length(r) / 4;
        bool kill = false;
        uint32_t bound = spv[3];
        int *loc = malloc(bound * sizeof *loc), *index = calloc(bound, sizeof *index);
        for (uint32_t i = 0; i < bound; i++) loc[i] = -1;
        for (size_t i = 5; i < words; i += spv[i] >> 16) {
            uint32_t op = spv[i] & 0xffff;
            kill |= op == 252 || op == 4416 || op == 5380; // Kill / TerminateInvocation / DemoteToHelperInvocation
            if (op == 71 && (spv[i] >> 16) == 4 && spv[i + 1] < bound) {
                if (spv[i + 2] == 30) loc[spv[i + 1]] = spv[i + 3]; // Location
                if (spv[i + 2] == 32) index[spv[i + 1]] = spv[i + 3]; // Index
            }
        }
        assert(kill == (func > 0 && func < 8));
        bool secondary = false, mrt1 = false;
        for (uint32_t i = 0; i < bound; i++) {
            secondary |= loc[i] == 0 && index[i] == 1;
            mrt1 |= loc[i] == 1;
        }
        assert(secondary == (bool)dual && mrt1 != (bool)dual);
        free(loc); free(index);
        shaderc_result_release(r);
        maxwell_free(s);
    }
    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);
    puts("fragment outputs: Metal MRT/dual-source output mapping and 36 compiled SPIR-V variants passed");
}
