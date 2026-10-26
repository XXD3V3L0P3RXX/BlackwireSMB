/* Internal read-only MkPFS exFAT adapter, compiled in container_source.c.
 * Format: Microsoft exFAT specification, sections 3, 4, 6 and 7.
 * Only one-FAT, fully initialized, contiguous allocations are admitted.
 * FAT-linked contiguous allocations and NoFatChain streams are both accepted;
 * fragmentation, TexFAT and unknown critical entries refuse explicitly.
 * No mounting, extraction, filesystem writes or retained allocations. */
#include <stdlib.h>

static uint16_t ex16(const uint8_t *p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t ex32(const uint8_t *p){return (uint32_t)ex16(p)|(uint32_t)ex16(p+2)<<16;}
static uint64_t ex64(const uint8_t *p){return (uint64_t)ex32(p)|(uint64_t)ex32(p+4)<<32;}
static uint32_t ex_sum(uint32_t sum){return (sum>>1)|(sum<<31);}
static int ex_read(struct bw_exfat *p,uint64_t at,void *out,size_t size){
    uint8_t *dest=out;if(at>p->size||size>p->size-at)return BW_PFS_RANGE;
    while(size){uint64_t page=at/sizeof p->cache*sizeof p->cache;size_t within=(size_t)(at-page);
        if(!p->cache_valid||p->cached_page!=page){
            p->cache_valid=0;uint64_t left=p->size-page;p->cached_bytes=left<sizeof p->cache?(size_t)left:sizeof p->cache;
            if(p->pread(p->source,page,p->cache,p->cached_bytes))return BW_PFS_IO;p->cached_page=page;p->cache_valid=1;
        }
        size_t take=p->cached_bytes-within;if(take>size)take=size;
        memcpy(dest,p->cache+within,take);dest+=take;at+=take;size-=take;
    }return 0;
}
static int ex_cluster(const struct bw_exfat *p,uint32_t first,uint64_t length,uint64_t *base,uint32_t *count){
    uint64_t n=length/p->cluster_size+!!(length%p->cluster_size);
    if(!n){if(first)return BW_PFS_FORMAT;*base=0;*count=0;return 0;}
    if(first<2||first-2>=p->clusters||n>p->clusters-(first-2))return BW_PFS_FORMAT;
    *base=p->heap+(uint64_t)(first-2)*p->cluster_size;*count=(uint32_t)n;return 0;
}
static int ex_fat(struct bw_exfat *p,uint32_t cluster,uint32_t *next){
    uint8_t v[4];if(cluster<2||cluster-2>=p->clusters)return BW_PFS_FORMAT;
    int r=ex_read(p,p->fat+(uint64_t)cluster*4,v,4);if(!r)*next=ex32(v);return r;
}
static int ex_chain(struct bw_exfat *p,uint32_t first,uint64_t length,int nofat){
    uint64_t base;uint32_t count;int r=ex_cluster(p,first,length,&base,&count);if(r||nofat)return r;
    for(uint32_t i=0;i<count;i++){uint32_t next;r=ex_fat(p,first+i,&next);if(r)return r;
        if(i+1==count){if(next!=UINT32_MAX)return BW_PFS_FORMAT;}
        else if(next!=first+i+1)return next>=2&&next-2<p->clusters?BW_PFS_UNSUPPORTED:BW_PFS_FORMAT;
    }return 0;
}
static int ex_allocated(struct bw_exfat *p,uint32_t first,uint64_t length){
    uint64_t base;uint32_t count;int r=ex_cluster(p,first,length,&base,&count);if(r||!count)return r;
    uint64_t bit=first-2,last=bit+count;uint8_t data[512];
    while(bit<last){uint64_t byte=bit/8;size_t take=(size_t)((last+7)/8-byte);if(take>sizeof data)take=sizeof data;
        if(byte>p->bitmap_size||take>p->bitmap_size-byte)return BW_PFS_FORMAT;
        r=ex_read(p,p->bitmap+byte,data,take);if(r)return r;
        uint64_t end=(byte+take)*8;if(end>last)end=last;
        while(bit<end){if(!(data[bit/8-byte]&(1u<<(bit%8))))return BW_PFS_FORMAT;bit++;}
    }return 0;
}
static int ex_boot_checksum(struct bw_exfat *p,uint32_t sector,uint64_t start){
    uint8_t data[512];uint32_t sum=0;
    for(uint64_t at=0;at<(uint64_t)sector*11;at+=sizeof data){
        int r=ex_read(p,start+at,data,sizeof data);if(r)return r;
        for(unsigned i=0;i<sizeof data;i++){uint64_t pos=at+i;if(pos==106||pos==107||pos==112)continue;sum=ex_sum(sum)+data[i];}
    }
    for(uint32_t at=0;at<sector;at+=sizeof data){int r=ex_read(p,start+(uint64_t)sector*11+at,data,sizeof data);if(r)return r;
        for(unsigned i=0;i<sizeof data;i+=4)if(ex32(data+i)!=sum)return BW_PFS_FORMAT;
    }return 0;
}
struct ex_item {struct bw_pfs_inode node;uint16_t name[255];uint8_t length,nofat;uint32_t bytes;char text[256];};
static uint16_t ex_name_hash(struct bw_exfat *p,const uint16_t *name,unsigned length){
    uint16_t sum=0;for(unsigned i=0;i<length;i++){uint16_t c=p->upcase[name[i]];
        sum=(uint16_t)((sum>>1)|(sum<<15));sum=(uint16_t)(sum+(c&255));
        sum=(uint16_t)((sum>>1)|(sum<<15));sum=(uint16_t)(sum+(c>>8));
    }return sum;
}
static int ex_name_text(struct ex_item *item){
    size_t used=0;for(unsigned i=0;i<item->length;i++){uint32_t c=item->name[i];
        if(c<32||c==127||(c<128&&strchr("\"*/:<>?\\|",(int)c)))return BW_PFS_PATH;
        if(c>=0xd800&&c<=0xdbff){if(++i>=item->length||item->name[i]<0xdc00||item->name[i]>0xdfff)return BW_PFS_PATH;
            c=0x10000+((c-0xd800)<<10)+(item->name[i]-0xdc00);
        }else if(c>=0xdc00&&c<=0xdfff)return BW_PFS_PATH;
        unsigned n=c<0x80?1:c<0x800?2:c<0x10000?3:4;if(used+n>255)return BW_PFS_PATH;
        if(n==1)item->text[used++]=(char)c;
        else{item->text[used++]=(char)((n==2?0xc0:n==3?0xe0:0xf0)|(c>>(6*(n-1))));
            for(unsigned j=n-1;j;j--)item->text[used++]=(char)(0x80|((c>>(6*(j-1)))&63));}
    }item->text[used]=0;return bw_manifest_validate_path(item->text,used)?BW_PFS_PATH:0;
}
/* 0=item, 1=unused/end entry, 2=skipped inactive/benign/system entry. */
static int ex_entry(struct bw_exfat *p,uint64_t base,uint64_t length,uint32_t at,int root,struct ex_item *item){
    uint8_t data[32*19];memset(item,0,sizeof *item);
    if(at==length)return 1;if(at>length||length-at<32)return BW_PFS_FORMAT;
    int r=ex_read(p,base+at,data,32);if(r)return r;item->bytes=32;
    if(!data[0])return 1;if(!(data[0]&0x80))return 2;
    if(root&&(data[0]==0x81||data[0]==0x82||data[0]==0x83))return 2;
    if(data[0]!=0x85)return (data[0]&0x20)?2:BW_PFS_UNSUPPORTED;
    unsigned count=data[1];if(count<2||count>18||(uint64_t)(count+1)*32>length-at)return BW_PFS_FORMAT;
    item->bytes=(count+1)*32;r=ex_read(p,base+at+32,data+32,count*32);if(r)return r;
    uint16_t sum=0;for(unsigned i=0;i<item->bytes;i++){if(i==2||i==3)continue;sum=(uint16_t)(((sum>>1)|(sum<<15))+data[i]);}
    if(sum!=ex16(data+2)||data[32]!=0xc0)return BW_PFS_FORMAT;
    const uint8_t *stream=data+32;item->length=stream[3];unsigned names=(item->length+14u)/15u;
    if(!item->length||count!=names+1||stream[1]&~3u)return BW_PFS_UNSUPPORTED;
    for(unsigned i=0;i<names;i++){const uint8_t *name=data+64+i*32;if(name[0]!=0xc1||name[1])return BW_PFS_FORMAT;
        for(unsigned j=0;j<15&&i*15+j<item->length;j++)item->name[i*15+j]=ex16(name+2+j*2);}
    if(ex_name_hash(p,item->name,item->length)!=ex16(stream+4))return BW_PFS_FORMAT;
    if((r=ex_name_text(item)))return r;
    uint16_t attr=ex16(data+4);if(attr&~0x37u)return BW_PFS_UNSUPPORTED;
    uint64_t size=ex64(stream+24),valid=ex64(stream+8);uint32_t first=ex32(stream+20),clusters;
    if(valid>size||size>BW_PFS_MAX_VOLUME)return BW_PFS_FORMAT;
    if(valid!=size)return BW_PFS_UNSUPPORTED;
    if(size&&!(stream[1]&1u))return BW_PFS_FORMAT;
    r=ex_cluster(p,first,size,&item->node.base,&clusters);if(r)return r;
    item->node.id=first;item->node.blocks=clusters;item->node.links=1;item->node.mode=(attr&16)?0x4000:0x8000;
    item->node.logical_size=item->node.stored_size=size;
    if((attr&16)&&(!size||size%p->cluster_size||size>BW_PFS_MAX_DIRECTORY))return BW_PFS_LIMIT;
    item->nofat=!!(stream[1]&2u);return 0;
}
struct ex_extent {uint64_t first,last;};
static int ex_extent_compare(const void *a,const void *b){const struct ex_extent *x=a,*y=b;return x->first<y->first?-1:x->first>y->first?1:0;}
struct ex_slot {uint64_t hash;uint32_t parent,at;};
struct ex_frame {uint64_t base,size;uint32_t cluster,at;size_t prefix;};
static int ex_equal(struct bw_exfat *p,const struct ex_item *a,const struct ex_item *b){
    if(a->length!=b->length)return 0;for(unsigned i=0;i<a->length;i++)if(p->upcase[a->name[i]]!=p->upcase[b->name[i]])return 0;return 1;
}
static int ex_walk_nodes(struct bw_exfat *p,bw_pfs_visit_fn visit,void *context,uint32_t *count,int directories){
    if(count)*count=0;if(!p||!p->ready)return BW_PFS_ARGUMENT;p->namespace_validated=0;
    const size_t capacity=BW_PFS_MAX_INODES*2u;struct ex_slot *slots=calloc(capacity,sizeof *slots);
    struct ex_extent *extents=calloc(BW_PFS_MAX_INODES+3u,sizeof *extents);
    struct ex_frame frames[BW_PFS_MAX_DEPTH+1];char path[BW_MANIFEST_MAX_PATH+1]={0};
    unsigned depth=0;uint32_t entries=0,files=0,total=3;int result=BW_PFS_MEMORY;
    if(!slots||!extents)goto done;
    extents[0]=(struct ex_extent){p->heap+(uint64_t)(p->root-2)*p->cluster_size,p->heap+(uint64_t)(p->root-2)*p->cluster_size+p->root_size};
    extents[1]=(struct ex_extent){p->bitmap,p->bitmap+(p->bitmap_size+p->cluster_size-1)/p->cluster_size*p->cluster_size};
    /* up-case allocation was separately validated at open; preserve its extent. */
    extents[2]=(struct ex_extent){p->upcase_base,p->upcase_base+(p->upcase_size+p->cluster_size-1)/p->cluster_size*p->cluster_size};
    frames[0]=(struct ex_frame){extents[0].first,p->root_size,p->root,0,0};
    for(;;){struct ex_frame *frame=frames+depth;struct ex_item item;uint32_t at=frame->at;
        result=ex_entry(p,frame->base,frame->size,at,!depth,&item);
        if(result==1){if(!depth)break;depth--;path[frames[depth].prefix]=0;continue;}
        if(result<0)goto done;frame->at+=item.bytes;if(result==2)continue;
        if(++entries>BW_PFS_MAX_INODES){result=BW_PFS_LIMIT;goto done;}
        uint64_t hash=UINT64_C(14695981039346656037);for(unsigned i=0;i<item.length;i++)hash=(hash^p->upcase[item.name[i]])*UINT64_C(1099511628211);
        size_t slot=(size_t)(hash^frame->cluster)&(capacity-1);for(size_t n=0;;n++,slot=(slot+1)&(capacity-1)){
            struct ex_slot *old=slots+slot;if(!old->parent){*old=(struct ex_slot){hash,frame->cluster,at};break;}
            if(n==capacity){result=BW_PFS_LIMIT;goto done;}
            if(old->parent==frame->cluster&&old->hash==hash){struct ex_item other;result=ex_entry(p,frame->base,frame->size,old->at,!depth,&other);
                if(result||ex_equal(p,&item,&other)){result=result<0?result:BW_PFS_PATH;goto done;}}
        }
        /* Validate allocation once per namespace walk, not again for every
         * unrelated directory entry scanned during each path lookup. Source
         * generation fences keep the admitted metadata immutable afterward. */
        result=ex_chain(p,item.node.id,item.node.logical_size,item.nofat);if(result)goto done;
        result=ex_allocated(p,item.node.id,item.node.logical_size);if(result)goto done;
        if(item.node.blocks){extents[total++]=(struct ex_extent){item.node.base,item.node.base+(uint64_t)item.node.blocks*p->cluster_size};
            /* Reject cycles and repeated directory allocations before descending. */
            if(item.node.mode==0x4000)for(unsigned i=0;i<=depth;i++)if(frames[i].cluster==item.node.id){result=BW_PFS_FORMAT;goto done;}}
        size_t prefix=frame->prefix,length=strlen(item.text);if(prefix+!!prefix+length>BW_MANIFEST_MAX_PATH){result=BW_PFS_PATH;goto done;}
        if(prefix)path[prefix++]='/';memcpy(path+prefix,item.text,length+1);
        if(item.node.mode==0x4000){if(depth==BW_PFS_MAX_DEPTH){result=BW_PFS_LIMIT;goto done;}
            if(directories){files++;if(visit&&visit(context,path,&item.node)){result=BW_PFS_CALLBACK;goto done;}}
            frames[++depth]=(struct ex_frame){item.node.base,item.node.logical_size,item.node.id,0,prefix+length};
        }else{files++;if(visit&&visit(context,path,&item.node)){result=BW_PFS_CALLBACK;goto done;}path[frame->prefix]=0;}
    }
    qsort(extents,total,sizeof *extents,ex_extent_compare);for(uint32_t i=1;i<total;i++)if(extents[i].first<extents[i-1].last){result=BW_PFS_FORMAT;goto done;}
    result=0;p->namespace_validated=1;if(count)*count=files;
done:free(slots);free(extents);return result;
}
static int ex_walk(struct bw_exfat *p,bw_pfs_visit_fn visit,void *context,uint32_t *count){return ex_walk_nodes(p,visit,context,count,0);}
static int ex_open(struct bw_exfat *p,bw_pfs_pread_fn read_source,void *context,uint64_t size){
    memset(p,0,sizeof *p);p->pread=read_source;p->source=context;p->size=size;uint8_t head[512],other[512];
    if(size<24*512u||size>BW_PFS_MAX_VOLUME)return BW_PFS_FORMAT;int r=ex_read(p,0,head,sizeof head);if(r)return r;
    if(memcmp(head,"\xeb\x76\x90",3)||memcmp(head+3,"EXFAT   ",8)||ex16(head+510)!=0xaa55)return BW_PFS_FORMAT;
    for(unsigned i=11;i<64;i++)if(head[i])return BW_PFS_FORMAT;
    if(ex16(head+104)!=0x100||head[110]!=1||ex16(head+106))return BW_PFS_UNSUPPORTED;
    if(head[108]<9||head[108]>12||head[109]>25-head[108])return BW_PFS_FORMAT;
    uint32_t sector=1u<<head[108];p->cluster_size=1u<<(head[108]+head[109]);
    uint64_t volume=ex64(head+72),fat=ex32(head+80),fat_length=ex32(head+84),heap=ex32(head+88);
    p->clusters=ex32(head+92);p->root=ex32(head+96);
    if(volume>size/sector||volume<24||fat<24||!fat_length||fat_length>volume-fat||fat>volume||
       heap<fat+fat_length||heap>volume||!p->clusters||p->clusters>0xfffffff5u||
       (uint64_t)p->clusters+2>fat_length*sector/4||(uint64_t)p->clusters*(p->cluster_size/sector)>volume-heap)return BW_PFS_FORMAT;
    p->size=volume*sector;p->fat=fat*sector;p->heap=heap*sector;
    if((r=ex_boot_checksum(p,sector,0))||(r=ex_boot_checksum(p,sector,(uint64_t)sector*12)))return r;
    if((r=ex_read(p,(uint64_t)sector*12,other,sizeof other)))return r;
    if(memcmp(head,other,106)||memcmp(head+108,other+108,4))return BW_PFS_FORMAT;
    uint32_t cluster=p->root;for(;;){uint32_t next;if((r=ex_fat(p,cluster,&next)))return r;p->root_size+=p->cluster_size;
        if(p->root_size>BW_PFS_MAX_DIRECTORY)return BW_PFS_LIMIT;if(next==UINT32_MAX)break;
        if(next!=cluster+1)return next>=2&&next-2<p->clusters?BW_PFS_UNSUPPORTED:BW_PFS_FORMAT;cluster=next;}
    uint64_t root;uint32_t root_clusters;if((r=ex_cluster(p,p->root,p->root_size,&root,&root_clusters)))return r;
    uint32_t bitmap_first=0,upcase_first=0,upcase_sum=0;unsigned label=0;int end=0;
    for(uint64_t at=0;at<p->root_size;at+=32){uint8_t entry[32];if((r=ex_read(p,root+at,entry,sizeof entry)))return r;
        if(!entry[0]){end=1;break;}if(!(entry[0]&0x80))continue;
        if(entry[0]==0x81){if(bitmap_first||entry[1])return BW_PFS_FORMAT;bitmap_first=ex32(entry+20);p->bitmap_size=ex64(entry+24);}
        else if(entry[0]==0x82){if(upcase_first)return BW_PFS_FORMAT;upcase_first=ex32(entry+20);p->upcase_size=ex64(entry+24);upcase_sum=ex32(entry+4);}
        else if(entry[0]==0x83){if(label++||entry[1]>11)return BW_PFS_FORMAT;}
        else if(entry[0]==0x85){unsigned n=entry[1];if(n<2||n>18||(uint64_t)(n+1)*32>p->root_size-at)return BW_PFS_FORMAT;at+=(uint64_t)n*32;}
        else if(!(entry[0]&0x20))return BW_PFS_UNSUPPORTED;
    }
    (void)end;if(!bitmap_first||!upcase_first||p->bitmap_size!=((uint64_t)p->clusters+7)/8||
       p->upcase_size<256||p->upcase_size>131072||p->upcase_size%2)return BW_PFS_FORMAT;
    uint32_t n;if((r=ex_cluster(p,bitmap_first,p->bitmap_size,&p->bitmap,&n))||
       (r=ex_cluster(p,upcase_first,p->upcase_size,&p->upcase_base,&n))||
       (r=ex_chain(p,bitmap_first,p->bitmap_size,0))||(r=ex_chain(p,upcase_first,p->upcase_size,0))||
       (r=ex_allocated(p,bitmap_first,p->bitmap_size))||(r=ex_allocated(p,upcase_first,p->upcase_size))||
       (r=ex_allocated(p,p->root,p->root_size)))return r;
    uint32_t sum=0,index=0;int run=0;for(uint64_t at=0;at<p->upcase_size;at+=2){uint8_t bytes[2];if((r=ex_read(p,p->upcase_base+at,bytes,2)))return r;
        sum=ex_sum(sum)+bytes[0];sum=ex_sum(sum)+bytes[1];uint16_t value=ex16(bytes);
        if(run){if(!value||value>65536u-index)return BW_PFS_FORMAT;for(unsigned i=0;i<value;i++)p->upcase[index]=(uint16_t)index,index++;run=0;}
        else if(value==0xffff&&index!=65535)run=1;
        else{if(index>=65536)return BW_PFS_FORMAT;p->upcase[index++]=value;}
    }
    if(run||index!=65536||sum!=upcase_sum)return BW_PFS_FORMAT;
    for(unsigned i=0;i<128;i++)if(p->upcase[i]!=(i>='a'&&i<='z'?i-32:i))return BW_PFS_FORMAT;
    p->ready=1;r=ex_walk(p,NULL,NULL,NULL);if(r)p->ready=0;return r;
}
static int ex_find(struct bw_exfat *p,const char *path,struct bw_pfs_inode *out){
    if(!p||!p->ready||!out||!path)return BW_PFS_ARGUMENT;size_t length=strnlen(path,BW_MANIFEST_MAX_PATH+1);
    if(bw_manifest_validate_path(path,length))return BW_PFS_PATH;
    uint64_t base=p->heap+(uint64_t)(p->root-2)*p->cluster_size,size=p->root_size;const char *part=path;int root=1;
    for(;;){const char *slash=strchr(part,'/');size_t n=slash?(size_t)(slash-part):strlen(part);int found=0;struct ex_item match={0},query={0};
        unsigned units=0;for(size_t at=0;at<n;){uint32_t c=(uint8_t)part[at++];unsigned more=c<128?0:c<224?1:c<240?2:3;
            if(more){c&=(1u<<(6-more))-1;for(unsigned j=0;j<more;j++)c=(c<<6)|((uint8_t)part[at++]&63);}
            if(units+(c>65535?2:1)>255)return BW_PFS_PATH;
            if(c>65535){c-=65536;query.name[units++]=(uint16_t)(0xd800+(c>>10));query.name[units++]=(uint16_t)(0xdc00+(c&1023));}
            else query.name[units++]=(uint16_t)c;
        }query.length=(uint8_t)units;
        for(uint32_t at=0;at<size;){struct ex_item item;int r=ex_entry(p,base,size,at,root,&item);if(r==1)break;if(r<0)return r;at+=item.bytes;if(r==2)continue;
            if(ex_equal(p,&item,&query)){if(found)return BW_PFS_PATH;match=item;found=1;}}
        if(!found)return BW_PFS_NOT_FOUND;if(!slash){if(match.node.mode!=0x8000)return BW_PFS_NOT_FOUND;*out=match.node;return 0;}
        if(match.node.mode!=0x4000)return BW_PFS_NOT_FOUND;base=match.node.base;size=match.node.logical_size;part=slash+1;root=0;
    }
}
static int ex_file_read(struct bw_exfat *p,uint32_t first,uint64_t length,uint64_t at,void *out,size_t size){
    if(!p||!p->ready||(!out&&size)||size>BW_PFSC_MAX_READ)return BW_PFS_ARGUMENT;
    if(at>length||size>length-at)return BW_PFS_RANGE;uint64_t base;uint32_t count;int r=ex_cluster(p,first,length,&base,&count);
    if(r||!size)return r;return p->pread(p->source,base+at,out,size)?BW_PFS_IO:0;
}
