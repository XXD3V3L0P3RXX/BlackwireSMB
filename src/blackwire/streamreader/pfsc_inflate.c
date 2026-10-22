#include "pfsc_range.h"
#ifndef MINIZ_NO_MALLOC
#define MINIZ_NO_MALLOC
#endif
#ifndef MINIZ_NO_STDIO
#define MINIZ_NO_STDIO
#endif
#include "vendor/miniz/miniz.h"
/* This retained subset keeps the low-level decoder static. Compile it in this
 * one translation unit, preserving all vendor files byte-for-byte. Its coroutine
 * restores several fields before state zero, so initialize the complete object.
 */
#undef tinfl_init
#define tinfl_init(reader) memset((reader), 0, sizeof *(reader))
#include "vendor/miniz/miniz_tinfl.c"

/* No allocation. A complete zlib stream must produce exactly one PFSC block,
 * including its checksum, with no trailing or concatenated input streams.
 */
int bw_pfsc_inflate_miniz(void *context, const void *input, size_t input_size,
                           void *output, size_t output_size) {
    tinfl_decompressor decoder;
    size_t consumed = input_size, produced = output_size;
    tinfl_status result;
    (void)context;
    if (!input || !output || !input_size || input_size >= BW_PFSC_BLOCK ||
        output_size != BW_PFSC_BLOCK) return -1;
    tinfl_init(&decoder);
    result = tinfl_decompress(&decoder, input, &consumed, output, output, &produced,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    return result == TINFL_STATUS_DONE && consumed == input_size &&
           produced == output_size ? 0 : -1;
}
