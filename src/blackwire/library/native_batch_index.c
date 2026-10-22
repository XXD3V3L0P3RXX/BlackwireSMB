static void bpo_put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void bpo_put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static int bpo_index(struct bpo_context *ctx,int root){
    struct cp_plan *p=ctx->plan;bw_source_smb_progress(ctx->source,BW_SOURCE_INDEXING,0,p->manifest.count,0,0);
    uint32_t slots=2;uint64_t pool=0;
    for(uint32_t i=0;i<p->manifest.count;i++){struct bw_manifest_record r;if(bw_manifest_get(&p->manifest,i,&r))return -1;pool+=7u+r.path_length;}
    while(slots<2*p->manifest.count)slots*=2;
    uint64_t paths_at=48u+24u*p->manifest.count,slots_at=(paths_at+pool+15)&~UINT64_C(15),size=slots_at+16u*slots;
    if(pool>32u*1024u*1024u||size>BW_BATCH_TERMINAL_INDEX_LIMIT)return v2_fail("Native index exceeds its bounded size");
    uint8_t *bytes=calloc(1,(size_t)size);if(!bytes)return -1;
    memcpy(bytes,"AMPRIDX3",8);bpo_put32(bytes+8,3);bpo_put32(bytes+12,24);bpo_put64(bytes+16,p->manifest.count);bpo_put64(bytes+24,pool);bpo_put64(bytes+32,slots_at);bpo_put32(bytes+40,16);bpo_put32(bytes+44,slots);
    size_t cursor=0;int bad=1;
    for(uint32_t i=0;i<p->manifest.count;i++){
        struct bw_manifest_record r;char leaf[256],relative[1024];if(bw_manifest_get(&p->manifest,i,&r)||cp_refresh(p))goto done;
        int parent=v2_parent(&p->cfg,root,r.path,0,leaf,relative);if(parent<0)goto done;
        int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);struct stat st;
        int invalid=fd<0||cp_regular(parent,leaf,fd,r.size,&st)||!v2_directory_current(&p->cfg,root,parent,relative);
        if(fd>=0&&close(fd))invalid=1;
        if(close(parent))invalid=1;if(invalid)goto done;
        uint8_t *row=bytes+48u+24u*i,*path=bytes+paths_at+cursor;uint32_t length=6u+r.path_length;
        memcpy(path,"/app0/",6);memcpy(path+6,r.path,r.path_length);bpo_put32(row,(uint32_t)cursor);bpo_put32(row+4,length);bpo_put64(row+8,r.size);bpo_put64(row+16,(uint64_t)st.st_mtime);cursor+=length+1;
        uint64_t hash=UINT64_C(1469598103934665603);
        for(uint32_t k=0;k<length;k++){uint8_t c=path[k];if(c>='A'&&c<='Z')c+=32;hash=(hash^c)*UINT64_C(1099511628211);}if(!hash)hash=1;
        uint32_t slot=(uint32_t)hash&(slots-1),duplicate=0;
        while(cp_u32(bytes+slots_at+16u*slot+8)){
            uint8_t *prior=bytes+slots_at+16u*slot;
            if(cp_u64(prior)==hash){bpo_put32(prior+12,cp_u32(prior+12)|1u);duplicate=1;}
            slot=(slot+1)&(slots-1);
        }
        uint8_t *entry=bytes+slots_at+16u*slot;bpo_put64(entry,hash);bpo_put32(entry+8,i+1);bpo_put32(entry+12,duplicate);
        bw_source_smb_progress(ctx->source,BW_SOURCE_INDEXING,i+1,p->manifest.count,0,0);
    }
    p->index=bytes;p->index_size=(size_t)size;cp_sha_bytes(bytes,p->index_size,p->index_sha);bad=0;
done:
    if(bad)free(bytes);return bad?-1:0;
}

/* Same AMPRIDX3 validation as the frozen copy helper, with outer cancellation,
 * explicit policy2 size bound and checked descriptor closes on this new path. */
static int bs_index_rows(struct cp_plan *p,int root){
    const uint8_t *b=p->index;size_t size=p->index_size;uint8_t hash[32];cp_sha_bytes(b,size,hash);
    if(size<48||size>BW_BATCH_TERMINAL_INDEX_LIMIT||memcmp(hash,p->index_sha,32)||memcmp(b,"AMPRIDX3",8)||cp_u32(b+8)!=3||cp_u32(b+12)!=24||cp_u64(b+16)!=p->manifest.count)return -1;
    uint64_t pool=cp_u64(b+24),slots_at=cp_u64(b+32);uint32_t slot_size=cp_u32(b+40),slots=cp_u32(b+44);uint64_t paths_at=48u+24u*p->manifest.count;
    if(pool>32u*1024u*1024u||paths_at+pool>size||slot_size!=16||slots<2||slots>524288||(slots&(slots-1))||slots_at!=((paths_at+pool+15)&~UINT64_C(15))||slots_at+(uint64_t)slots*16!=size)return -1;
    uint8_t *seen=calloc(p->manifest.count,1);if(!seen)return -1;int bad=0;uint64_t cursor=0;
    for(uint32_t i=0;i<p->manifest.count&&!bad;i++){
        const uint8_t *entry=b+48u+24u*i;uint32_t offset=cp_u32(entry),length=cp_u32(entry+4);struct bw_manifest_record r;
        if(!cp_alive(p)||cp_io_current()||bw_manifest_get(&p->manifest,i,&r)||offset!=cursor||offset>pool||length>=pool-offset||b[paths_at+offset+length]||length!=6u+r.path_length||
           memcmp(b+paths_at+offset,"/app0/",6)||memcmp(b+paths_at+offset+6,r.path,r.path_length)||cp_u64(entry+8)!=r.size){bad=1;break;}
        char leaf[256],relative[1024];int parent=v2_parent(&p->cfg,root,r.path,0,leaf,relative);if(parent<0){bad=1;break;}
        int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);struct stat s;
        if(fd<0||!v2_directory_current(&p->cfg,root,parent,relative)||cp_regular(parent,leaf,fd,r.size,&s)||s.st_uid!=geteuid()||(uint64_t)s.st_mtime!=cp_u64(entry+16)||
           (strcmp(p->phase,"copy")&&r.flags==BW_MANIFEST_LOCAL&&(bw_startup_mode(r.path)&0111)&&(s.st_mode&0111)!=0111))bad=1;
        if(fd>=0&&close(fd))bad=1;if(close(parent))bad=1;cursor+=(uint64_t)length+1;
    }
    if(cursor!=pool)bad=1;for(uint64_t i=paths_at+pool;i<slots_at;i++)if(b[i])bad=1;
    for(uint32_t j=0;j<slots&&!bad;j++){
        const uint8_t *s=b+slots_at+16u*j;uint64_t value=cp_u64(s);uint32_t record=cp_u32(s+8),flags=cp_u32(s+12);
        if(!record){if(value||flags)bad=1;continue;}if(record>p->manifest.count||seen[record-1]++||flags>1){bad=1;break;}
        const uint8_t *entry=b+48u+24u*(record-1),*path=b+paths_at+cp_u32(entry);uint32_t length=cp_u32(entry+4);uint64_t actual=UINT64_C(1469598103934665603);
        for(uint32_t k=0;k<length;k++){uint8_t c=path[k];if(c>='A'&&c<='Z')c+=32;actual=(actual^c)*UINT64_C(1099511628211);}if(!actual)actual=1;
        if(value!=actual){bad=1;break;}uint32_t at=(uint32_t)value&(slots-1),collisions=0;
        while(at!=j){const uint8_t *prior=b+slots_at+16u*at;if(!cp_u32(prior+8)){bad=1;break;}if(cp_u64(prior)==value)collisions++;at=(at+1)&(slots-1);}if(collisions&&!flags)bad=1;
    }
    for(uint32_t i=0;i<p->manifest.count;i++)if(seen[i]!=1)bad=1;free(seen);return bad?-1:0;
}

