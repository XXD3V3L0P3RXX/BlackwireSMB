#include "preload_launch.h"
#include "launch_timing.h"
#include <string.h>
#include <stdlib.h>

struct pl_operation {
    const struct bw_preload_launch_request *request;
    struct bw_preload_launch *owner;
    double deadline;
    int assigning,cleaning;
};
static int pl_valid(const struct bw_preload_launch_ops *o) {
    return o&&o->now&&o->pause&&o->current&&o->bundle&&o->launch&&o->discover&&
        o->consumer&&o->live&&o->exited&&o->event;
}
static double pl_now(struct pl_operation *p) {return p->request->ops.now(p->request->ops.opaque);}
static int pl_time(struct pl_operation *p) {
    double at=pl_now(p);return !__builtin_isfinite(at)||at<0||!__builtin_isfinite(p->deadline)||at>=p->deadline?-1:0;
}
static int pl_guard(void *opaque) {
    struct pl_operation *p=opaque;const struct bw_preload_launch_ops *o=&p->request->ops;
    if(pl_time(p))return -1;
    if(p->cleaning)return o->exited(o->opaque)||pl_time(p)?-1:0;
    if(o->current(o->opaque))return -1;
    if(p->assigning) {
        uid_t observed=(uid_t)-1;
        if(p->owner->pid<=1||o->consumer(o->opaque,p->owner->pid,&observed)||
           observed!=p->owner->uid)return -1;
    }
    return pl_time(p);
}
static void pl_event(struct pl_operation *p,const char *phase,const char *message) {
    const struct bw_preload_launch_ops *o=&p->request->ops;
    o->event(o->opaque,phase,message,p->owner->pid);
}
static void pl_pause(struct pl_operation *p) {
    /* A failed sleep changes no ownership. Continue readonly observation;
     * it cannot authorize a new launch, repeated assignment or a release. */
    if(p->request->ops.pause(p->request->ops.opaque,100))
        pl_event(p,"error","Waiting for the exact game to exit; the observation delay failed.");
}
static int pl_run(const struct bw_preload_launch_request *q,struct bw_preload_launch *s,void **projection,size_t projection_size) {
    if(!q||!s||!q->connection||!q->root||!q->selection||!pl_valid(&q->ops)) {
        if(q&&q->connection)bw_share_clear(q->connection);
        return -1;
    }
    if(s->busy||s->blocked||(s->handoff.initialized&&!s->finished)) {
        bw_share_clear(q->connection);return -1;
    }
    /* Only a proved release closed this object's retained FD capability. */
    if(s->finished) {
        if(!s->handoff.released||!s->handoff.descriptors_closed){bw_share_clear(q->connection);return -1;}
        memset(s,0,sizeof *s);
    }
    s->busy=1;s->uid=(uid_t)-1;s->launch_result=-1;s->release_result=-1;s->descriptor_result=-1;
    struct pl_operation p={q,s,0,0,0};double started=pl_now(&p);
    if(!__builtin_isfinite(started)||started<0){bw_share_clear(q->connection);s->busy=0;return -1;}
    p.deadline=started+30.0;int result=-1,failure_reported=0;
    if((bw_reader_selection_is_projection(q->selection)?(!projection||!*projection||!projection_size):(projection||projection_size))||pl_guard(&p))goto no_publish;
    int bundled=BW_LT_CALL(BW_LT_BUNDLE,q->ops.bundle(q->ops.opaque,q->root));
    if(bundled) {
        s->bundle_uncertain=bundled==-2;s->blocked=s->bundle_uncertain;
        pl_event(&p,"error",s->bundle_uncertain?"The private library set has a partial or uncertain outcome; inspect it before another launch.":"The bundled reader or per-game library set could not be qualified.");
        goto no_publish;
    }
    s->bundle_verified=1;
    struct bw_preload_handoff_request handoff={q->root,q->selection,q->connection,pl_guard,&p,p.deadline};
    int published=pl_guard(&p)?BW_PRELOAD_HANDOFF_REFUSED:BW_LT_CALL(BW_LT_HANDOFF,
        projection?bw_preload_handoff_publish_projection_retained(&handoff,*projection,projection_size,&s->handoff):
        bw_preload_handoff_publish_retained(&handoff,&s->handoff));
    if(projection){free(*projection);*projection=NULL;}
    bw_share_clear(q->connection);
    if(published) {
        pl_event(&p,"error","Private reader inputs were not fully published; no game launch was requested.");
        goto cleanup;
    }
    if(pl_guard(&p)) {
        pl_event(&p,"error","The selected launch changed after private input publication.");
        goto cleanup;
    }
    started=pl_now(&p);
    if(!__builtin_isfinite(started)||started<0)goto cleanup;
    /* Module input wait is 10 seconds. Keep discovery/assignment within the
     * first 8 seconds sampled from the actual launch intent. Native scheduling
     * and Sony return timing still require console validation. */
    p.deadline=started+8.0;
    s->launch_called=1;s->launch_result=q->ops.launch(q->ops.opaque);
    if(s->launch_result!=1&&s->launch_result!=0)s->launch_result=-1;
    if(!s->launch_result) {
        pl_event(&p,"error","The normal Sony launch was not accepted.");
        goto cleanup;
    }
    if(s->launch_result<0) {
        pl_event(&p,"error","The launch outcome is unknown; retaining private inputs until exact exit is established.");
        failure_reported=1;
    }
    /* Readonly discovery may continue after its bounded assignment window.
     * This preserves ownership after an accepted/unknown launch that appears
     * late. It never reissues the launch or adopts an unrelated generation. */
    while(s->pid<=1) {
        int32_t observed=BW_LT_CALL(BW_LT_DISCOVER,q->ops.discover(q->ops.opaque));
        if(observed>1)s->pid=observed;
        if(s->pid>1)break;
        if(!failure_reported&&pl_time(&p)) {
            pl_event(&p,"error","The exact launched process was not bound during the reader handoff window; retaining this launch.");
            failure_reported=1;
        }
        /* Only the real runtime may prove a no-process launch outcome. */
        if(!q->ops.exited(q->ops.opaque))goto cleanup;
        pl_pause(&p);
    }
    if(s->launch_result==1&&!pl_guard(&p)&&
       !q->ops.consumer(q->ops.opaque,s->pid,&s->uid)&&s->uid!=(uid_t)-1) {
        p.assigning=1;s->assignment_attempted=1;
        s->assignment_confirmed=!BW_LT_CALL(BW_LT_ASSIGN,bw_preload_handoff_assign_consumer(&s->handoff,s->uid,pl_guard,&p,p.deadline));
        p.assigning=0;
    }
    if(!s->assignment_confirmed) {
        pl_event(&p,"error","The exact game's private reader ownership could not be confirmed; retaining inputs until normal game exit.");
        failure_reported=1;
    }else {
        /* Process launch/ownership is observable here. Worker readiness,
         * delivered byte counts and visible menu are separate evidence. */
        pl_event(&p,"running","Game launched; private reader inputs assigned. Delivery and the game screen are not yet verified.");
    }
    BW_LT_REPORT(BW_LT_PROCESS_BOUND);
    for(;;) {
        int live=q->ops.live(q->ops.opaque,s->pid);
        if(!live)break;
        if(live<0&&!failure_reported) {
            pl_event(&p,"error","Exact game liveness is unknown; reader inputs remain owned.");failure_reported=1;
        }
        pl_pause(&p);
    }
    if(s->assignment_confirmed&&!failure_reported)result=0;
cleanup:
    bw_share_clear(q->connection);
    if(s->handoff.initialized) {
        if(!s->launch_called&&bw_preload_handoff_empty_released(&s->handoff)){
            /* Definite refusal before any create/launch owns no filesystem
             * cleanup and already discharged the empty consumer capability. */
            s->release_result=BW_PRELOAD_HANDOFF_OK;s->finished=1;s->busy=0;return result;
        }
        while(q->ops.exited(q->ops.opaque))pl_pause(&p);
        p.cleaning=1;p.assigning=0;started=pl_now(&p);p.deadline=started+5.0;
        s->release_attempted=1;
        s->release_result=bw_preload_handoff_release(&s->handoff,pl_guard,&p,p.deadline);
        if(s->release_result) {
            /* This closes only exact retained creator FDs. It does not erase
             * an uncertain file or repeat a previous close/unlink. */
            if(!s->handoff.descriptor_close_started&&!s->handoff.descriptors_closed)
                s->descriptor_result=bw_preload_handoff_close_descriptors(&s->handoff,pl_guard,&p,p.deadline);
            s->blocked=1;result=-1;
            pl_event(&p,"error","The game exited, but private reader cleanup is unconfirmed; inspect the owned preparation before retrying.");
        }else s->finished=1;
    }
    s->busy=0;return result;
no_publish:
    bw_share_clear(q->connection);s->busy=0;return -1;
}
int bw_preload_launch_run(const struct bw_preload_launch_request *q,struct bw_preload_launch *s){return pl_run(q,s,NULL,0);}
int bw_preload_launch_run_projection(const struct bw_preload_launch_request *q,struct bw_preload_launch *s,void **projection,size_t size){
    if(!projection)return -1;int result=pl_run(q,s,projection,size);free(*projection);*projection=NULL;return result;
}
