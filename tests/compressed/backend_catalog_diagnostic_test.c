/* SPDX-License-Identifier: GPL-3.0-or-later
 * Reuse maintained ownership spies; state() and all diagnostic helpers are real.
 * No copied serializer, runtime/game actions or network calls. */
#define main unused_preparation_fixture_main
#include "../dashboard/lifecycle/test_play_preparation_failure.c"
#undef main
static void emit_state(const char *label){
 struct bw_native_response response={0};allow_locked_settings=1;
 CHECK(!state(&owner,&response)&&response.status==200);allow_locked_settings=0;
 printf("\"%s\":",label);fwrite(response.bytes,1,response.size,stdout);bw_native_response_clear(&response);
}
int main(void){
 begin_case();free(owner.library.catalog.items);free(owner.library.prepared);
 owner.library.catalog.count=owner.library.catalog.capacity=3;
 owner.library.catalog.items=calloc(3,sizeof *owner.library.catalog.items);
 owner.library.prepared=calloc(3,sizeof *owner.library.prepared);CHECK(owner.library.catalog.items&&owner.library.prepared);
 owner.library.prepared[0]=owner.playing.selection.prepared;
 struct bw_catalog_item *rows=owner.library.catalog.items;
 rows[0].state=BW_CATALOG_FOLDER;strcpy(rows[0].source,"Neutral folder");strcpy(rows[0].name,"Neutral folder");strcpy(rows[0].title_id,"PPSA99999");
 rows[1].state=BW_CATALOG_CONTAINER_READY;rows[1].container_kind=BW_CONTAINER_PFSC_EXFAT;rows[1].container_stage=BW_CATALOG_IMAGE_VALIDATED;
 rows[1].namespace_files=5000;rows[1].namespace_path_bytes=1200000;strcpy(rows[1].source,"Neutral-valid.ffpfsc");strcpy(rows[1].name,"Neutral valid");strcpy(rows[1].title_id,"PPSA09998");
 rows[2].state=BW_CATALOG_CONTAINER;rows[2].container_error=BW_PFS_LIMIT;rows[2].container_stage=BW_CATALOG_IMAGE_NAMESPACE;
 rows[2].namespace_files=65538;rows[2].namespace_path_bytes=BW_MANIFEST_MAX_PATH_BYTES;strcpy(rows[2].source,"Neutral-rejected.ffpfsc");strcpy(rows[2].name,"Neutral rejected");
 printf("{\"configured\":");struct bw_native_response response={0};allow_locked_settings=1;CHECK(!state(&owner,&response)&&response.status==200);allow_locked_settings=0;
 fwrite(response.bytes,1,response.size,stdout);bw_native_response_clear(&response);
 settings_status=owner.settings_status=BW_SETUP_INVALID;printf(",");emit_state("hidden");
 settings_status=owner.settings_status=BW_SETUP_VALID;rows[2].container_error=-1234;rows[2].container_stage=(enum bw_catalog_container_stage)999;printf(",");emit_state("unknown");
 end_case();printf(",\"passed\":true,\"cases\":3,\"checks\":%u,\"actualBackendStateJson\":true,\"sourceAdmissionAndTransportModeled\":true}\n",checks);return 0;
}
