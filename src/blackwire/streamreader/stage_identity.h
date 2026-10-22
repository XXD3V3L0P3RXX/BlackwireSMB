#ifndef BW_STAGE_IDENTITY_H
#define BW_STAGE_IDENTITY_H
#include <stdint.h>
#include <string.h>

#define BW_STAGE_CHALLENGE_BYTES 64u
#define BW_STAGE_IDENTITY_CAPACITY (UINT64_C(256)*1024*1024)

/* Pure policy only. Payload snapshots come from held fstat plus canonical
 * named lstat; game snapshots come from the independently validated native
 * fstat on the current intercepted game thread. No integer-FD cache alone is
 * identity. Source game files and SMB handles are never written by this code. */
struct bw_stage_identity { uint64_t device,inode,size,links; int regular; };
struct bw_stage_snapshot { struct bw_stage_identity held,named,game; };
struct bw_stage_proof { uint64_t game_device; int verified; };

static inline int bw_stage_payload_matches(const struct bw_stage_identity *anchor,
    const struct bw_stage_identity *held,const struct bw_stage_identity *named) {
    return anchor && held && named && anchor->regular && anchor->links==1 && anchor->inode &&
        anchor->size==BW_STAGE_IDENTITY_CAPACITY && held->regular && named->regular &&
        held->links==1 && named->links==1 && held->device==anchor->device && named->device==anchor->device &&
        held->inode==anchor->inode && named->inode==anchor->inode &&
        held->size==anchor->size && named->size==anchor->size;
}
/* Preliminary shape check only: zero links in a game view is NOT trusted until
 * the complete per-run challenge and before/after snapshots prove its view. */
static inline int bw_stage_game_shape(const struct bw_stage_identity *anchor,
    const struct bw_stage_identity *game) {
    return anchor && game && game->regular && game->links<=1 &&
        game->inode==anchor->inode && game->size==anchor->size;
}
static inline int bw_stage_challenge(const char *run,unsigned char out[BW_STAGE_CHALLENGE_BYTES]) {
    static const unsigned char domain[16]={'B','W','-','C','A','C','H','E','-','P','R','O','O','F',1,0};
    static const char hex[]="0123456789abcdef";
    if (!out) return -1;
    memset(out,0,BW_STAGE_CHALLENGE_BYTES);
    if (!run || strlen(run)!=32) return -1;
    for (unsigned i=0;i<32;i++) if (!strchr(hex,run[i])) return -1;
    memcpy(out,domain,sizeof domain);
    for (unsigned i=0;i<16;i++) {
        unsigned a=(unsigned)(strchr(hex,run[2*i])-hex);
        unsigned b=(unsigned)(strchr(hex,run[2*i+1])-hex);
        out[16+i]=(unsigned char)((a<<4)|b);
    }
    memcpy(out+32,run,32);
    return 0;
}
static inline int bw_stage_proof_latch(struct bw_stage_proof *out,const struct bw_stage_identity *anchor,
    const struct bw_stage_snapshot *before,const struct bw_stage_snapshot *after,const char *run,
    const unsigned char observed[BW_STAGE_CHALLENGE_BYTES],int64_t returned) {
    unsigned char challenge[BW_STAGE_CHALLENGE_BYTES];
    if (!out) return -1;
    memset(out,0,sizeof *out);
    if (!before || !after || !observed || returned!=BW_STAGE_CHALLENGE_BYTES ||
        bw_stage_challenge(run,challenge) || memcmp(challenge,observed,sizeof challenge) ||
        !bw_stage_payload_matches(anchor,&before->held,&before->named) ||
        !bw_stage_payload_matches(anchor,&after->held,&after->named) ||
        !bw_stage_game_shape(anchor,&before->game) || !bw_stage_game_shape(anchor,&after->game) ||
        before->game.device!=after->game.device) return -1;
    out->game_device=after->game.device; out->verified=1;
    return 0;
}
static inline int bw_stage_proof_current(const struct bw_stage_proof *proof,const struct bw_stage_identity *anchor,
    const struct bw_stage_snapshot *current) {
    return proof && proof->verified && current &&
        bw_stage_payload_matches(anchor,&current->held,&current->named) &&
        bw_stage_game_shape(anchor,&current->game) && current->game.device==proof->game_device;
}
#endif
