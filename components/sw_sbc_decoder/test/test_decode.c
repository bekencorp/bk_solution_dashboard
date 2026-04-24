#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sbc.h>

static int load_file(const char *path, uint8_t **out, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    *out_len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = malloc(*out_len);
    if (fread(*out, 1, *out_len, f) != *out_len) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

static int write_wav(const char *path, const int16_t *pcm, size_t n_samples,
                     int channels, int sample_rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t data_size = (uint32_t)(n_samples * 2);
    uint32_t file_size = 36 + data_size;
    uint16_t block_align = (uint16_t)(channels * 2);
    uint32_t byte_rate = (uint32_t)(sample_rate * block_align);
    uint16_t bits = 16;
    fwrite("RIFF", 1, 4, f);
    fwrite(&file_size, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, f);
    uint16_t fmt = 1;
    fwrite(&fmt, 2, 1, f);
    uint16_t ch = (uint16_t)channels;
    fwrite(&ch, 2, 1, f);
    uint32_t sr = (uint32_t)sample_rate;
    fwrite(&sr, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
    fwrite(pcm, 2, n_samples, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.sbc> [reference.pcm]\n", argv[0]);
        return 1;
    }

    uint8_t *sbc_data = NULL;
    size_t sbc_len = 0;
    if (load_file(argv[1], &sbc_data, &sbc_len) < 0)
        return 1;

    printf("SBC input: %zu bytes\n", sbc_len);

    sbc_t sbc;
    sbc_reset(&sbc);

    size_t pcm_cap = 1024 * 1024;
    int16_t *pcm_out = malloc(pcm_cap * sizeof(int16_t));
    size_t pcm_pos = 0;
    int frame_count = 0;

    struct sbc_frame frame;
    int16_t pcml[SBC_MAX_SAMPLES];
    int16_t pcmr[SBC_MAX_SAMPLES];

    size_t offset = 0;
    while (offset < sbc_len) {
        if (sbc_data[offset] != 0x9C && sbc_data[offset] != 0xAD) {
            offset++;
            continue;
        }

        int ret = sbc_decode(&sbc, sbc_data + offset, (unsigned)(sbc_len - offset),
                             &frame, pcml, 1, pcmr, 1);
        if (ret < 0) {
            offset++;
            continue;
        }

        unsigned fsize = sbc_get_frame_size(&frame);
        offset += fsize;
        frame_count++;

        int nsamples = frame.nblocks * frame.nsubbands;
        int nch = (frame.mode == SBC_MODE_MONO) ? 1 : 2;

        if (nch == 2) {
            for (int i = 0; i < nsamples; i++) {
                if (pcm_pos + 2 <= pcm_cap) {
                    pcm_out[pcm_pos++] = pcml[i];
                    pcm_out[pcm_pos++] = pcmr[i];
                }
            }
        } else {
            for (int i = 0; i < nsamples; i++) {
                if (pcm_pos + 1 <= pcm_cap)
                    pcm_out[pcm_pos++] = pcml[i];
            }
        }

        if (frame_count <= 3) {
            int sr = sbc_get_freq_hz(frame.freq);
            printf("Frame %d: size=%u sr=%d ch=%d blk=%d sub=%d bp=%d\n",
                   frame_count, fsize, sr, nch, frame.nblocks, frame.nsubbands,
                   frame.bitpool);
            printf("  pcmL[0..3] = %d %d %d %d\n",
                   pcml[0], pcml[1], pcml[2], pcml[3]);
        }
    }

    int nch = (frame.mode == SBC_MODE_MONO) ? 1 : 2;
    int sr = sbc_get_freq_hz(frame.freq);
    double duration = (double)(pcm_pos / nch) / sr;

    printf("\nDecoded %d frames, %zu PCM samples (%zu per channel)\n",
           frame_count, pcm_pos, pcm_pos / nch);
    printf("Sample rate: %d, Channels: %d\n", sr, nch);
    printf("Duration: %.2f seconds\n", duration);

    write_wav("test_output_v2.wav", pcm_out, pcm_pos, nch, sr);
    printf("Written: test_output_v2.wav\n");

    size_t mono_len = pcm_pos / nch;
    int16_t *L = malloc(mono_len * sizeof(int16_t));
    for (size_t i = 0; i < mono_len && i * nch < pcm_pos; i++)
        L[i] = pcm_out[i * nch];

    size_t n = mono_len < 20000 ? mono_len : 20000;
    double xx = 0, xy = 0;
    for (size_t i = 0; i < n; i++) xx += (double)L[i] * L[i];
    for (size_t i = 0; i + 1 < n; i++) xy += (double)L[i] * L[i + 1];
    double autocorr = xx > 0 ? xy / xx : 0;
    printf("\nSignal quality:\n");
    printf("  Adjacent autocorrelation: %.4f (music>0.95, noise~0.0)\n", autocorr);

    int zc = 0;
    size_t zn = n < 10000 ? n : 10000;
    for (size_t i = 1; i < zn; i++)
        if ((L[i] >= 0) != (L[i - 1] >= 0)) zc++;
    printf("  Zero crossing rate: %.1f%% (music 5-20%%, noise ~50%%)\n",
           100.0 * zc / zn);

    if (argc >= 3) {
        uint8_t *ref_raw = NULL;
        size_t ref_len = 0;
        if (load_file(argv[2], &ref_raw, &ref_len) == 0) {
            size_t ref_samples = ref_len / 2;
            int16_t *ref = (int16_t *)ref_raw;
            size_t cmp_len = ref_samples < pcm_pos ? ref_samples : pcm_pos;

            int exact_match = 0;
            double mse = 0;
            int max_diff = 0;
            for (size_t i = 0; i < cmp_len; i++) {
                int diff = (int)pcm_out[i] - (int)ref[i];
                if (diff == 0) exact_match++;
                mse += (double)diff * diff;
                if (abs(diff) > max_diff) max_diff = abs(diff);
            }
            mse /= cmp_len;

            printf("\nReference comparison (%zu samples):\n", cmp_len);
            printf("  Exact match: %d/%zu (%.1f%%)\n",
                   exact_match, cmp_len, 100.0 * exact_match / cmp_len);
            printf("  MSE: %.1f, RMSE: %.1f\n", mse, sqrt(mse));
            printf("  Max diff: %d\n", max_diff);
            printf("  SNR: %.1f dB\n",
                   mse > 0 ? 10.0 * log10(xx * n / (mse * cmp_len)) : 999.0);

            if (max_diff == 0)
                printf("  RESULT: PERFECT MATCH!\n");
            else if (max_diff <= 1)
                printf("  RESULT: PASS (rounding differences only)\n");
            else if (sqrt(mse) < 2.0)
                printf("  RESULT: PASS (minor differences, RMSE < 2)\n");
            else
                printf("  RESULT: FAIL (significant differences)\n");

            free(ref_raw);
        }
    }

    free(L);
    free(pcm_out);
    free(sbc_data);
    return 0;
}
