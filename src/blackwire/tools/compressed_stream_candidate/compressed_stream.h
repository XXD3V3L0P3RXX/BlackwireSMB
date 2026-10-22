#ifndef BW_COMPRESSED_STREAM_CANDIDATE_H
#define BW_COMPRESSED_STREAM_CANDIDATE_H
#include "../../library/source_binding.h"
#include "../../streamreader/cache.h"
#include "../../streamreader/container_manifest.h"
#include "../../streamreader/container_smb.h"

enum bw_compressed_stream_error {
    BW_CSTREAM_STATE=-80, BW_CSTREAM_IDENTITY=-81, BW_CSTREAM_MAP=-82,
    BW_CSTREAM_NOT_STREAM=-83, BW_CSTREAM_CANCELLED=-84,
    BW_CSTREAM_BUSY=-85, BW_CSTREAM_CACHE=-86
};
/* Zero means still selected/current; positive means cancelled; negative means
 * selection/owner identity is no longer current. This callback grants no job,
 * publication or process authority and must not reenter this adapter. */
typedef int (*bw_cstream_current_fn)(void *);

struct bw_cstream_admission {
    struct bw_container_smb *image;
    struct bw_container_manifest *files;
    const struct bw_source_binding *binding;
    const void *map_bytes;
    size_t map_size;
    const uint8_t *expected_binding_sha256,*expected_map_sha256;
    bw_cstream_current_fn current;
    void *opaque;
    /* Optional borrowed, freshly initialized EMPTY existing cache, exclusively
     * used for this session while bound (no foreign loaders or mutations).
     * Never allocated/destroyed by this adapter. Required for cached APIs. */
    struct bw_cache *cache;
};

/* DATA-ONLY candidate. Zero initialize, keep at a stable address and serialize
 * every call. All borrowed objects and the connected SMB context must outlive
 * this adapter. The raw map and token table are immutable, validated inputs;
 * caller writes or freeing them while bound violate this lifetime contract.
 * They are not an externally mutable source file to be reloaded here.
 *
 * Accepts an ALREADY admitted read-only image and exact identity/map selection.
 * The upstream caller owns real server/share selection admission and authority.
 * No connection, credential access, source open/close, extraction, destination
 * write, process, module or game-memory operation is performed here.
 *
 * Uses unchanged unsigned contiguous PFS/PFSC parser formats only. Has no heap
 * allocation and no second data cache; manifest validation's temporary bounded
 * metadata and the image decoder's existing fixed workspace are additional.
 * Source/selection failures latch until destroy. Invalid caller ranges/capacity
 * or LOCAL IDs refuse without poisoning a healthy owner.
 * Cancellation is checked between bounded operations and after provisional
 * bytes, not by interrupting an in-flight SMB call. Its timeout belongs to the
 * borrowed transport; no hard wall-clock cancellation deadline is promised.
 */
struct bw_compressed_stream {
    struct bw_container_smb *image;
    struct bw_container_manifest *files;
    struct bw_container_file *tokens;
    const struct bw_source_binding *live_binding;
    struct bw_source_binding binding;
    struct bw_manifest map;
    const uint8_t *map_bytes;
    size_t map_size;
    uint8_t map_header[BW_MANIFEST_HEADER_SIZE],map_sha256[32];
    struct smb2_context *smb;
    struct smb2fh *handle;
    struct smb2_stat_64 admitted;
    uint64_t session;
    int kind,has_pfsc;
    struct bw_container_ops ops;
    bw_cstream_current_fn current;
    void *opaque;
    struct bw_cache *cache;
    int ready,failed,error,busy;
};

int bw_compressed_stream_bind(struct bw_compressed_stream *,const struct bw_cstream_admission *);
int bw_compressed_stream_current(struct bw_compressed_stream *);
/* Exact existing bw_cache_loader callback ABI. capacity must equal 64 KiB;
 * valid bytes are published through out_size only after all checks succeed.
 * EOF block loads refuse; final partial blocks retain their actual length.
 * A failed read may have changed dest. Discard ALL attempted output on error. */
int bw_compressed_stream_load_block(void *,uint32_t,uint64_t,uint8_t *,size_t,size_t *);
int bw_compressed_stream_read(struct bw_compressed_stream *,uint32_t,uint64_t,void *,size_t);
/* These wrappers also validate generation on existing 64 MiB cache HITS.
 * Bypassing them with a naked bw_cache_get requires equivalent pre/post current
 * guards in the caller; the load callback alone cannot guard cache hits.
 * Cached pointer lifetime is cache.h's lifetime, not a source lease. */
int bw_compressed_stream_cached_get(struct bw_compressed_stream *,uint32_t,uint64_t,const uint8_t **,size_t *);
int bw_compressed_stream_cached_read(struct bw_compressed_stream *,uint32_t,uint64_t,void *,size_t);
/* Releases only the binding: borrowed image/tokens/cache remain owned by caller.
 * Destroy/reinitialize the existing cache before a NEW source-session binding.
 * Must not be called from the current callback or during an active operation. */
void bw_compressed_stream_destroy(struct bw_compressed_stream *);
#endif
