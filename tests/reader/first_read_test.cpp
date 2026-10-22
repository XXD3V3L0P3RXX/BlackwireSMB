#include "reader/preload/first_read.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
static unsigned checks,cases;
#define CHECK(x) do{checks++;if(!(x)){std::fprintf(stderr,"first-read line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static bool eligible(int64_t returnValue,bool remote,bool jobCanceled,bool stopping){
    struct {bool bpRemote;std::atomic<bool>cancelRequested;}job{remote,jobCanceled};
    struct {std::atomic<bool>workerStopRequested;}state{stopping};bool bpPublished=false;
#include "completion-witness-under-test.h"
    return bpPublished;
}
static bp_read_witness fixture(){
    static const unsigned char digest[32]={0x36,0xbb,0xe5,0x0e,0xd9,0x68,0x41,0xd1,0x04,0x43,0xbc,0xb6,0x70,0xd6,0x55,0x4f,0x0a,0x34,0xb7,0x61,0xbe,0x67,0xec,0x9c,0x4a,0x8a,0xd2,0xc0,0xc4,0x4c,0xa4,0x2c}; // SHA256("abcde")
    bp_read_witness w{};w.map_id=2;w.offset=w.digest_offset=11;w.requested=w.returned=w.digest_size=5;w.fetched_payload_bytes=65536;w.valid=1;std::memcpy(w.digest_sha256,digest,sizeof digest);return w;
}
int main(){
    bp_first_read state{};auto w=fixture();char line[512],sample[512];cases++;
    CHECK(eligible(5,true,false,false));CHECK(!eligible(0,true,false,false));CHECK(!eligible(-EIO,true,false,false));CHECK(!eligible(5,true,true,false));CHECK(!eligible(5,true,false,true));CHECK(!eligible(5,false,false,false));
    int n=bp_first_read_format(&state,&w,5,0,line,sizeof line);CHECK(n>0&&state.emitted==1);CHECK(std::strstr(line,"mapId=2 offset=11 requested=5 returned=5"));CHECK(std::strstr(line,"fetchedPayloadBytes=65536 copiedBytes=5"));
    CHECK(std::strstr(line,"36bbe50ed96841d10443bcb670d6554f0a34b761be67ec9c4a8ad2c0c44ca42c"));std::strcpy(sample,line);
    CHECK(bp_first_read_format(&state,&w,5,0,line,sizeof line)==0&&line[0]==0);
    for(int kind=0;kind<7;kind++){
        state={};w=fixture();cases++;int64_t result=5;int cancel=0;
        if(kind==0)result=0;else if(kind==1)result=-EIO;else if(kind==2)cancel=1;else if(kind==3)w.valid=0;else if(kind==4)w.fetched_payload_bytes=0;else if(kind==5)w.digest_size=0;else w.requested=4;
        CHECK(bp_first_read_format(&state,&w,result,cancel,line,sizeof line)==0&&!state.emitted&&line[0]==0);
    }
    cases++;state={};w=fixture();CHECK(bp_first_read_format(&state,&w,5,0,line,8)==-1&&!state.emitted&&line[0]==0);
    cases++;state={};w=fixture();w.map_id=UINT32_MAX;w.offset=w.digest_offset=UINT64_MAX;w.requested=UINT64_MAX;w.returned=INT64_MAX;w.digest_size=65536;w.fetched_payload_bytes=UINT64_MAX;
    int max=bp_first_read_format(&state,&w,INT64_MAX,0,line,sizeof line);CHECK(max>0&&max<498); // existing kernel logger's prefixed 512-byte line
    std::printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"maximumMarkerBytes\":%d,\"actualCFormatter\":true,\"actualGeneratedCompletionPredicate\":true,\"sample\":\"%s\"}\n",cases,checks,max,sample);return 0;
}
