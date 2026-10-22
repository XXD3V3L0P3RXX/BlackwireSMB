#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "filemap.h"
#include "startup_mode.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef BW_FILEMAP_HOST
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#endif

struct bw_file_identity { uint64_t inode, size; uint32_t file_id; };

static int failure(struct bw_files *files, const char *message) {
    if (files) files->_error = message;
    return -1;
}
const char *bw_files_error(const struct bw_files *files) {
    return files && files->_error ? files->_error : "No file map error";
}
static int same_object(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}
static int canonical(const char *path, struct stat *st, int directory) {
    char resolved[PATH_MAX];
    return realpath(path, resolved) && !strcmp(path, resolved) && !lstat(path, st) &&
           (directory ? S_ISDIR(st->st_mode) : (S_ISREG(st->st_mode) && st->st_nlink == 1));
}
static int joined(char *out, size_t capacity, const char *root, const char *path) {
    int n = snprintf(out, capacity, "%s/%s", root, path);
    return n < 0 || (size_t)n >= capacity ? -1 : 0;
}
static int identity_compare(const void *left, const void *right) {
    const struct bw_file_identity *a = left, *b = right;
    if (a->inode != b->inode) return a->inode < b->inode ? -1 : 1;
    if (a->size != b->size) return a->size < b->size ? -1 : 1;
    return a->file_id < b->file_id ? -1 : a->file_id != b->file_id;
}
static const struct bw_file_identity *identity_find(const struct bw_files *files, uint64_t inode) {
    size_t low = 0, high = files->stream_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (files->_identities[mid].inode < inode) low = mid + 1;
        else high = mid;
    }
    return low < files->stream_count && files->_identities[low].inode == inode ? &files->_identities[low] : NULL;
}
void bw_files_destroy(struct bw_files *files) {
    if (!files) return;
    for (size_t i = 0; i < BW_FILES_HANDLE_LIMIT; i++)
        if (files->_handles[i].handle && files->_ops.close)
            files->_ops.close(files->_remote_context, files->_handles[i].handle);
    free(files->_identities); free(files->_bytes);
    memset(files, 0, sizeof *files);
}
int bw_files_load(struct bw_files *files, const char *root, const char *manifest_path) {
    char path[BW_FILES_PATH_CAPACITY]; struct stat root_st, before, opened, after;
    int fd = -1, result = -1;
    if (!files || !root || !manifest_path) return failure(files, "Invalid file map arguments");
    if (files->_bytes || files->_identities || files->manifest.count)
        return failure(files, "Destroy the previous file map before loading");
    memset(files, 0, sizeof *files);
    size_t root_length = strlen(root);
    if (!root_length || root[0] != '/' || root_length >= sizeof files->_root || root[root_length - 1] == '/' ||
        !canonical(root, &root_st, 1)) return failure(files, "Game root is not its exact canonical directory");
    memcpy(files->_root, root, root_length + 1);
    files->_local_device = (uint64_t)root_st.st_dev;
    if (manifest_path[0] == '/') {
        if (strlen(manifest_path) >= sizeof path || strncmp(manifest_path, root, root_length) || manifest_path[root_length] != '/')
            return failure(files, "Manifest is outside the selected game root");
        strcpy(path, manifest_path);
    } else {
        if (!*manifest_path || strchr(manifest_path, '/') || strchr(manifest_path, '\\') ||
            !strcmp(manifest_path, ".") || !strcmp(manifest_path, "..") || joined(path, sizeof path, root, manifest_path))
            return failure(files, "Invalid manifest filename");
    }
    if (!canonical(path, &before, 0) || before.st_size < BW_MANIFEST_HEADER_SIZE ||
        (uint64_t)before.st_size > BW_FILES_MAX_MANIFEST_BYTES || before.st_dev != root_st.st_dev)
        return failure(files, "Manifest identity, size, or links are invalid");
    fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0 || fstat(fd, &opened) || !S_ISREG(opened.st_mode) || opened.st_nlink != 1 ||
        !same_object(&before, &opened) || before.st_size != opened.st_size) {
        failure(files, "Manifest changed before read-only open"); goto done;
    }
    size_t size = (size_t)opened.st_size;
    files->_bytes = malloc(size);
    if (!files->_bytes) { failure(files, "Cannot allocate bounded manifest storage"); goto done; }
    size_t done = 0;
    while (done < size) {
        ssize_t got = read(fd, (uint8_t *)files->_bytes + done, size - done);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0 || (size_t)got > size - done) { failure(files, "Manifest read was incomplete"); goto done; }
        done += (size_t)got;
    }
    if (fstat(fd, &after) || !same_object(&opened, &after) || opened.st_size != after.st_size ||
        opened.st_mtime != after.st_mtime || !canonical(path, &before, 0) || !same_object(&after, &before)) {
        failure(files, "Manifest changed during validation"); goto done;
    }
    close(fd); fd = -1;
    enum bw_manifest_error error = bw_manifest_open(&files->manifest, files->_bytes, size);
    if (error) { failure(files, bw_manifest_error_string(error)); goto done; }
    for (uint32_t i = 0; i < files->manifest.count; i++) {
        struct bw_manifest_record record; bw_manifest_get(&files->manifest, i, &record);
        if (record.flags == BW_MANIFEST_STREAM) files->stream_count++;
    }
    if (files->stream_count) {
        files->_identities = calloc(files->stream_count, sizeof *files->_identities);
        if (!files->_identities) { failure(files, "Cannot allocate bounded identity index"); goto done; }
    }
    size_t at = 0;
    for (uint32_t i = 0; i < files->manifest.count; i++) {
        struct bw_manifest_record record; struct stat st;
        bw_manifest_get(&files->manifest, i, &record);
        if (joined(path, sizeof path, root, record.path) || !canonical(path, &st, 0) ||
            st.st_size < 0 || (uint64_t)st.st_size != record.size || st.st_dev != root_st.st_dev) {
            failure(files, "A manifest file is missing, linked, outside its root, or has the wrong size"); goto done;
        }
        if (record.flags == BW_MANIFEST_LOCAL && (bw_startup_mode(record.path) & 0111) && (st.st_mode & 0111) != 0111) {
            failure(files, "A local executable or module lacks execute permission; send required files again"); goto done;
        }
        if (record.flags == BW_MANIFEST_STREAM)
            files->_identities[at++] = (struct bw_file_identity){(uint64_t)st.st_ino, record.size, i};
    }
    if (!canonical(root, &after, 1) || !same_object(&root_st, &after)) {
        failure(files, "Game root changed while loading file identities"); goto done;
    }
    if (files->stream_count) qsort(files->_identities, files->stream_count, sizeof *files->_identities, identity_compare);
    for (size_t i = 1; i < files->stream_count; i++) if (files->_identities[i - 1].inode == files->_identities[i].inode) {
        failure(files, "Stream file identities are ambiguous"); goto done;
    }
    files->allocation_bytes = size + (size_t)files->stream_count * sizeof *files->_identities;
    result = 0;
done:
    if (fd >= 0) close(fd);
    if (result) {
        const char *message = files->_error;
        bw_files_destroy(files); files->_error = message;
    }
    return result;
}
uint32_t bw_files_count(const struct bw_files *files) { return files ? files->manifest.count : 0; }
uint64_t bw_files_checksum(const struct bw_files *files) { return files ? files->manifest.checksum : 0; }
const void *bw_files_manifest_bytes(const struct bw_files *files, size_t *size) {
    if (size) *size=0;
    if (!files || !size || !files->_bytes || !files->manifest.count) return NULL;
    *size=BW_MANIFEST_HEADER_SIZE+(size_t)files->manifest.count*BW_MANIFEST_RECORD_SIZE+files->manifest.path_bytes;
    return files->_bytes;
}
int bw_files_get(const struct bw_files *files, uint32_t id, struct bw_manifest_record *record) {
    return !files || bw_manifest_get(&files->manifest, id, record) ? -1 : 0;
}
const char *bw_files_path(const struct bw_files *files, uint32_t id) {
    return files ? bw_manifest_path(&files->manifest, id, NULL) : NULL;
}
int bw_files_first_stream(const struct bw_files *files, uint32_t *id) {
    if (id) *id = UINT32_MAX;
    if (!files || !id || !files->stream_count) return -1;
    *id = files->_identities[0].file_id; return 0;
}
int bw_files_latch_device(struct bw_files *files, uint64_t device, uint64_t inode,
                          uint64_t size, uint32_t expected_file_id) {
    if (!files || !files->manifest.count) return failure(files, "File identities are not loaded");
    const struct bw_file_identity *identity = identity_find(files, inode);
    if (!identity || identity->file_id != expected_file_id || identity->size != size ||
        (files->_latched && files->_game_device != device))
        return failure(files, "Game mount identity probe did not match its known stream path");
    files->_game_device = device; files->_latched = 1; return 0;
}
enum bw_files_match bw_files_lookup(struct bw_files *files, uint64_t device,
                                    uint64_t inode, uint64_t size, uint32_t *id) {
    if (id) *id = UINT32_MAX;
    if (!files || !id || !files->manifest.count) { failure(files, "File identities are not loaded"); return BW_FILES_ERROR; }
    const struct bw_file_identity *identity = identity_find(files, inode);
    if (!identity) return BW_FILES_LOCAL;
    if (!files->_latched || files->_game_device != device || identity->size != size) {
        failure(files, "Known stream inode has a wrong size or unverified game device"); return BW_FILES_ERROR;
    }
    *id = identity->file_id; return BW_FILES_MATCH;
}
static int safe_prefix(const char *prefix) {
    if (!prefix || !*prefix || strlen(prefix) > 255 || !strcmp(prefix, ".") || !strcmp(prefix, "..")) return 0;
    for (const unsigned char *p = (const unsigned char *)prefix; *p; p++)
        if (*p < 32 || *p == 127 || strchr("/\\:=|", *p)) return 0;
    /* Manifest paths already validate UTF-8. The prefix comes from the selected
     * canonical game root; requiring it to equal that root's last component
     * keeps both namespace boundaries identical without an alternate path. */
    return 1;
}
static int same_ops(const struct bw_files_remote_ops *a, const struct bw_files_remote_ops *b) {
    return a->stat == b->stat && a->open_readonly == b->open_readonly && a->fstat == b->fstat && a->close == b->close;
}
static uint64_t next_age(struct bw_files *files) {
    if (files->_clock == UINT64_MAX) {
        size_t order[BW_FILES_HANDLE_LIMIT], count = 0;
        for (size_t i = 0; i < BW_FILES_HANDLE_LIMIT; i++) if (files->_handles[i].handle) order[count++] = i;
        for (size_t i = 1; i < count; i++) {
            size_t value = order[i], at = i;
            while (at && files->_handles[order[at - 1]].age > files->_handles[value].age) {
                order[at] = order[at - 1]; at--;
            }
            order[at] = value;
        }
        for (size_t i = 0; i < count; i++) files->_handles[order[i]].age = i + 1;
        files->_clock = count;
    }
    return ++files->_clock;
}
static int close_slot(struct bw_files *files, struct bw_files_handle *slot) {
    void *handle = slot->handle;
    slot->handle = NULL; slot->age = 0; files->handle_count--;
    if (files->_ops.close(files->_remote_context, handle)) {
        files->_remote_failed = 1;
        return failure(files, "SMB handle close failed; no further opens are allowed");
    }
    return 0;
}
void *bw_files_remote_get_ops(struct bw_files *files, const struct bw_files_remote_ops *ops,
                              void *context, uint32_t id, const char *prefix) {
    struct bw_manifest_record record; char path[BW_FILES_PATH_CAPACITY];
    if (!files || !ops || !ops->stat || !ops->open_readonly || !ops->fstat || !ops->close ||
        !context || !safe_prefix(prefix) || bw_files_get(files, id, &record) || record.flags != BW_MANIFEST_STREAM) {
        failure(files, "Invalid remote stream request"); return NULL;
    }
    const char *name = strrchr(files->_root, '/');
    if (!name || strcmp(name + 1, prefix)) { failure(files, "SMB prefix differs from the selected game directory"); return NULL; }
    if (files->_remote_failed) { failure(files, "SMB handle pool has failed"); return NULL; }
    if (!files->_remote_bound) {
        files->_ops = *ops; files->_remote_context = context; strcpy(files->_prefix, prefix); files->_remote_bound = 1;
    } else if (files->_remote_context != context || strcmp(files->_prefix, prefix) || !same_ops(&files->_ops, ops)) {
        failure(files, "Cannot rebind the file map to another SMB share"); return NULL;
    }
    struct bw_files_handle *victim = NULL;
    for (size_t i = 0; i < BW_FILES_HANDLE_LIMIT; i++) {
        struct bw_files_handle *slot = &files->_handles[i];
        if (slot->handle && slot->file_id == id) {
            slot->age = next_age(files); files->handle_hits++; return slot->handle;
        }
        if (!victim || (!slot->handle && victim->handle) ||
            (slot->handle && victim->handle && slot->age < victim->age)) victim = slot;
    }
    if (joined(path, sizeof path, prefix, record.path)) { failure(files, "SMB stream path exceeds its bound"); return NULL; }
    struct bw_files_remote_stat before, after;
    memset(&before, 0, sizeof before); memset(&after, 0, sizeof after);
    if (ops->stat(context, path, &before) || !before.regular || before.size != record.size || before.mtime_sec != record.mtime_sec) {
        failure(files, "Preserved SMB file type, size, or mtime differs from the manifest"); return NULL;
    }
    if (victim->handle) {
        if (close_slot(files, victim)) return NULL;
        files->handle_evictions++;
    }
    void *handle = ops->open_readonly(context, path);
    if (!handle) { failure(files, "Cannot open preserved SMB file read only"); return NULL; }
    files->handle_opens++;
    if (ops->fstat(context, handle, &after) || !after.regular || after.size != record.size ||
        after.mtime_sec != record.mtime_sec || (before.inode && after.inode && before.inode != after.inode)) {
        if (ops->close(context, handle)) files->_remote_failed = 1;
        failure(files, "Opened SMB file identity, size, or mtime did not match"); return NULL;
    }
    victim->handle = handle; victim->file_id = id; victim->age = next_age(files); files->handle_count++;
    return handle;
}
#ifndef BW_FILEMAP_HOST
static void copy_smb_stat(struct bw_files_remote_stat *out, const struct smb2_stat_64 *st) {
    out->regular = st->smb2_type == SMB2_TYPE_FILE;
    out->size = st->smb2_size; out->mtime_sec = st->smb2_mtime; out->inode = st->smb2_ino;
}
static int remote_stat(void *context, const char *path, struct bw_files_remote_stat *out) {
    struct smb2_stat_64 st;
    if (smb2_stat(context, path, &st)) return -1;
    copy_smb_stat(out, &st); return 0;
}
static void *remote_open(void *context, const char *path) { return smb2_open(context, path, O_RDONLY); }
static int remote_fstat(void *context, void *handle, struct bw_files_remote_stat *out) {
    struct smb2_stat_64 st;
    if (smb2_fstat(context, handle, &st)) return -1;
    copy_smb_stat(out, &st); return 0;
}
static int remote_close(void *context, void *handle) { return smb2_close(context, handle); }
struct smb2fh *bw_files_remote_get(struct bw_files *files, struct smb2_context *smb,
                                  uint32_t id, const char *prefix) {
    static const struct bw_files_remote_ops ops = {remote_stat, remote_open, remote_fstat, remote_close};
    return bw_files_remote_get_ops(files, &ops, smb, id, prefix);
}
#endif
