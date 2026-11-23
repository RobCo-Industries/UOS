/*
 * Host tool, not a unit test: validates tape files with hf_open() so the
 * Python tests can check that the C validator agrees with tools/mkholo.py.
 *   holocheck FILE...   prints "FILE ok" or "FILE <error name>" per file
 * Exits 0 when every file is valid, 1 otherwise, 2 on a read failure.
 */

#include <stdio.h>
#include <stdlib.h>

#include "holo/holofmt.h"

int main(int argc, char **argv)
{
    static unsigned char buf[HF_TAPE_MAX + 1];
    int i, bad = 0;

    for (i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        size_t n;
        hf_tape_t t;
        int rc;

        if (f == NULL) {
            fprintf(stderr, "%s: cannot open\n", argv[i]);
            return 2;
        }
        n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        rc = hf_open(&t, buf, (uint32_t)n);
        printf("%s %s\n", argv[i], hf_err_name(rc));
        if (rc != HF_OK) {
            bad = 1;
        }
    }
    return bad;
}
