/*
 * decoder.c — Citrin bytecode file loader + dump
 *
 * Build:
 *   make decoder
 *
 * Usage:
 *   ./decoder <file.cbc>...
 */

#define BYTECODE_NO_MAIN
#include "bytecode.c"

/* ================================================================
 *  Reader
 * ================================================================ */

typedef struct {
    const uint8_t *data;
    uint32_t       pos;
    uint32_t       len;
} bc_reader;

static uint8_t bcr_u8(bc_reader *r) {
    if (r->pos + 1 > r->len) b_error("decode: unexpected end of file");
    return r->data[r->pos++];
}

static uint16_t bcr_u16(bc_reader *r) {
    uint16_t v = bcr_u8(r);
    return v | ((uint16_t)bcr_u8(r) << 8);
}

/* ================================================================
 *  Dump
 * ================================================================ */

static void bc_dump(const uint8_t *data, uint32_t len) {
    bc_reader rr = { data, 0, len };
    bc_reader *r = &rr;

    /* header */
    char magic[5] = {0};
    for (int i = 0; i < 4; i++) magic[i] = (char)bcr_u8(r);
    uint16_t version = bcr_u16(r);
    uint16_t str_cnt = bcr_u16(r);
    uint16_t fn_cnt  = bcr_u16(r);

    if (magic[0] != 'C' || magic[1] != 'I' || magic[2] != 'B' || magic[3] != 'C')
        b_error("not a CIBC file (magic=%.4s)", magic);

    printf("magic=%.4s version=%u strings=%u functions=%u\n",
           magic, version, str_cnt, fn_cnt);

    /* string table */
    char **strs = b_malloc(str_cnt * sizeof(char *));
    printf("\n--- strings (%u) ---\n", str_cnt);
    for (uint16_t i = 0; i < str_cnt; i++) {
        uint16_t slen = bcr_u16(r);
        strs[i] = b_malloc((uint32_t)slen + 1);
        for (uint16_t c = 0; c < slen; c++) strs[i][c] = (char)bcr_u8(r);
        strs[i][slen] = '\0';
        printf("  [%u] \"%s\"\n", i, strs[i]);
    }

    /* functions */
    for (uint16_t fi = 0; fi < fn_cnt; fi++) {
        uint16_t name_idx  = bcr_u16(r);
        uint8_t  arg_count = bcr_u8(r);
        uint8_t  reg_count = bcr_u8(r);
        uint16_t loc_count = bcr_u16(r);
        for (uint16_t li = 0; li < loc_count; li++) { bcr_u16(r); bcr_u8(r); }
        uint16_t op_count  = bcr_u16(r);

        const char *fname = (name_idx < str_cnt) ? strs[name_idx] : "?";
        printf("\n--- fn[%u] '%s'  args=%u regs=%u words=%u ---\n",
               fi, fname, arg_count, reg_count, op_count);

        uint32_t word_idx = 0;
        while (word_idx < op_count) {
            uint8_t     b0      = bcr_u8(r);
            uint8_t     subtype = b0 >> 6;
            uint8_t     opnum   = b0 & 0x3F;
            const char *oname   = (opnum < B_OP_COUNT) ? b_op_names[opnum] : "???";

            printf("  [%3u] %-12s", word_idx++, oname);

            switch (subtype) {
            case 0: {
                uint8_t b1 = bcr_u8(r), b2 = bcr_u8(r), b3 = bcr_u8(r);
                printf("%u, %u, %u", b1, b2, b3);
                break;
            }
            case 1: {
                uint8_t b1 = bcr_u8(r), b2 = bcr_u8(r), imm = bcr_u8(r);
                printf("%u, %u, [%u]", b1, b2, imm);
                break;
            }
            case 2: {
                uint8_t  b1  = bcr_u8(r);
                uint16_t imm = bcr_u8(r);
                imm |= (uint16_t)bcr_u8(r) << 8;
                if (opnum == B_JMP || opnum == B_JMPF || opnum == B_JMPT) {
                    int32_t rel = (int32_t)imm - (int32_t)(word_idx - 1);
                    printf("%u, %u [%+d]", b1, imm, rel);
                } else {
                    printf("%u, [%u]", b1, imm);
                }
                break;
            }
            case 3: {
                uint8_t  b1    = bcr_u8(r);
                uint8_t  extra = bcr_u8(r);
                bcr_u8(r); /* reserved */
                uint8_t  payload[256];
                uint32_t pbytes = (uint32_t)extra * 4;
                for (uint32_t k = 0; k < pbytes; k++) payload[k] = bcr_u8(r);

                printf("%u, [%u],", b1, extra);
                for (uint8_t w = 0; w < extra; w++) {
                    uint32_t base = (uint32_t)w * 4;
                    if (w == 0)
                        printf("\n  [%3u]           ( ", word_idx);
                    else
                        printf("\n  [%3u]             ", word_idx);
                    word_idx++;
                    for (int k = 0; k < 4; k++) {
                        int last = (w == extra - 1) && (k == 3);
                        printf("%u%s", payload[base + k], last ? " )" : ", ");
                    }
                }
                break;
            }
            }
            printf("\n");
        }
    }

    for (uint16_t i = 0; i < str_cnt; i++) free(strs[i]);
    free(strs);
}

/* ================================================================
 *  Main
 * ================================================================ */

int main(int argc, char **argv) {
    if (argc < 2) {
        const char msg[] = "usage: decoder <file.cbc>...\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        FILE *fp = fopen(argv[i], "rb");
        if (!fp) {
            fprintf(stderr, "error: cannot open '%s'\n", argv[i]);
            continue;
        }

        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        rewind(fp);

        if (sz <= 0) {
            fprintf(stderr, "error: empty file '%s'\n", argv[i]);
            fclose(fp);
            continue;
        }

        uint8_t *data = b_malloc((uint32_t)sz);
        if (fread(data, 1, (size_t)sz, fp) != (size_t)sz) {
            fprintf(stderr, "error: read failed '%s'\n", argv[i]);
            free(data);
            fclose(fp);
            continue;
        }
        fclose(fp);

        printf("=== %s (%ld bytes) ===\n", argv[i], sz);
        bc_dump(data, (uint32_t)sz);
        free(data);
    }

    return 0;
}
