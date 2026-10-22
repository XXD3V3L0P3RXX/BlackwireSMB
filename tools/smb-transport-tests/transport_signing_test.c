/* Offline wire-byte tests: real vendor receive/parser/crypto, no socket I/O. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

static ssize_t memory_readv(int fd, const struct iovec *iov, int count);
#define readv memory_readv
#include "socket.c"
#undef readv
#include "aes128ccm.h"

#define CHECK(condition) do { if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #condition, case_name); \
        exit(1); } } while (0)

struct fixture {
        struct smb2_context context;
        uint8_t wire[4096];
        uint8_t received[64];
        size_t wire_len, position, chunk, pause_at;
        int callbacks, last_status;
        unsigned data_bytes;
};

static const char *case_name;
static struct fixture *active;
static unsigned cases;

static void put16(uint8_t *p, uint16_t value)
{
        p[0] = (uint8_t)value;
        p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
        for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static void put64(uint8_t *p, uint64_t value)
{
        for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static void done(struct smb2_context *context, int status, void *data, void *opaque)
{
        struct fixture *f = opaque;
        ++f->callbacks;
        f->last_status = status;
        if (status == SMB2_STATUS_SUCCESS && context->hdr.command == SMB2_READ) {
                struct smb2_read_reply *reply = data;
                CHECK(reply && reply->data_length == 32);
                f->data_bytes += reply->data_length;
                for (unsigned i = 0; i < reply->data_length; ++i) {
                        CHECK(reply->data[i] == (uint8_t)(i + 0x40));
                }
        }
}

static void begin(struct fixture *f, const char *name, uint16_t dialect)
{
        memset(f, 0, sizeof(*f));
        case_name = name;
        active = f;
        ++cases;
        f->context.fd = -1;
        f->context.dialect = dialect;
        f->context.sign = 1;
        f->context.session_id = UINT64_C(0x1122334455667788);
        f->context.session_key_size = 16;
        f->context.tree_id_cur = 1;
        f->context.tree_id_top = 1;
        f->context.tree_id[1] = 7;
        f->context.opaque = f;
        for (unsigned i = 0; i < 16; ++i) {
                f->context.signing_key[i] = (uint8_t)(i + 1);
                f->context.serverout_key[i] = (uint8_t)(i + 0x20);
        }
        f->chunk = 3; /* Exercise short reads at every receive phase. */
}

static void finish(struct fixture *f)
{
        if (f->context.pdu) {
                if (f->context.waitqueue) {
                        SMB2_LIST_REMOVE(&f->context.waitqueue, f->context.pdu);
                }
                smb2_free_pdu(&f->context, f->context.pdu);
                f->context.pdu = NULL;
        }
        while (f->context.waitqueue) {
                struct smb2_pdu *pdu = f->context.waitqueue;
                SMB2_LIST_REMOVE(&f->context.waitqueue, pdu);
                smb2_free_pdu(&f->context, pdu);
        }
        smb2_free_iovector(&f->context, &f->context.in);
        free(f->context.enc);
}

static void request(struct fixture *f, uint16_t command, uint64_t id, int app_buffer)
{
        struct smb2_pdu *pdu = smb2_allocate_pdu(&f->context, command, done, f);
        CHECK(pdu);
        pdu->header.message_id = id;
        if (app_buffer) {
                CHECK(smb2_add_iovector(&f->context, &pdu->in, f->received, 32, NULL));
        }
        SMB2_LIST_ADD_END(&f->context.waitqueue, pdu);
}

static size_t message(struct fixture *f, uint8_t *p, uint16_t command,
                      uint32_t status, uint64_t id, int signed_message)
{
        size_t body;
        memset(p, 0, 256);
        memcpy(p, "\xfeSMB", 4);
        put16(p + 4, 64);
        put16(p + 6, 1);
        put32(p + 8, status);
        put16(p + 12, command);
        put16(p + 14, 1);
        put32(p + 16, SMB2_FLAGS_SERVER_TO_REDIR |
                        (signed_message ? SMB2_FLAGS_SIGNED : 0));
        put64(p + 24, id);
        put32(p + 36, 7);
        put64(p + 40, f->context.session_id);
        if (status != SMB2_STATUS_SUCCESS && status != SMB2_STATUS_MORE_PROCESSING_REQUIRED) {
                body = 8;
                put16(p + 64, SMB2_ERROR_REPLY_SIZE);
        } else if (command == SMB2_READ) {
                body = 48;
                put16(p + 64, SMB2_READ_REPLY_SIZE);
                p[66] = 80;
                put32(p + 68, 32);
                for (unsigned i = 0; i < 32; ++i) p[80 + i] = (uint8_t)(i + 0x40);
        } else if (command == SMB2_NEGOTIATE) {
                body = 64;
                put16(p + 64, SMB2_NEGOTIATE_REPLY_SIZE);
                put16(p + 66, SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED);
                put16(p + 68, SMB2_VERSION_0210);
                put32(p + 92, 1024 * 1024);
                put32(p + 96, 1024 * 1024);
                put32(p + 100, 1024 * 1024);
        } else if (command == SMB2_SESSION_SETUP) {
                body = 8;
                put16(p + 64, SMB2_SESSION_SETUP_REPLY_SIZE);
        } else if (command == SMB2_OPLOCK_BREAK) {
                body = 24;
                put16(p + 64, SMB2_OPLOCK_BREAK_NOTIFICATION_SIZE);
        } else {
                body = 4;
                put16(p + 64, SMB2_ECHO_REPLY_SIZE);
        }
        return 64 + body;
}

static void sign_message(struct fixture *f, uint8_t *p, size_t length)
{
        struct smb2_iovec iov = { .buf = p, .len = length };
        CHECK(smb2_calc_signature(&f->context, p + 48, &iov, 1) == 0);
}

static void frame(struct fixture *f, size_t length)
{
        f->wire[0] = (uint8_t)(length >> 24);
        f->wire[1] = (uint8_t)(length >> 16);
        f->wire[2] = (uint8_t)(length >> 8);
        f->wire[3] = (uint8_t)length;
        f->wire_len = length + 4;
}

static ssize_t memory_readv(int fd, const struct iovec *iov, int count)
{
        struct fixture *f = active;
        size_t copied = 0, limit = f->chunk;
        (void)fd;
        if (f->pause_at && f->position == f->pause_at) {
                f->pause_at = 0;
                errno = EAGAIN;
                return -1;
        }
        if (f->position == f->wire_len) {
                errno = EAGAIN;
                return -1;
        }
        if (limit > f->wire_len - f->position) limit = f->wire_len - f->position;
        if (f->pause_at && limit > f->pause_at - f->position) limit = f->pause_at - f->position;
        for (int i = 0; i < count && copied < limit; ++i) {
                size_t amount = iov[i].iov_len;
                if (amount > limit - copied) amount = limit - copied;
                memcpy(iov[i].iov_base, f->wire + f->position + copied, amount);
                copied += amount;
        }
        f->position += copied;
        return (ssize_t)copied;
}

static int receive(struct fixture *f)
{
        active = f;
        return smb2_read_from_socket(&f->context);
}

static void read_case(uint16_t dialect, int signed_message, int tamper, int app_buffer)
{
        struct fixture f;
        begin(&f, !signed_message ? "unsigned READ rejected" :
                tamper ? "modified signed READ rejected" : "signed READ accepted", dialect);
        request(&f, SMB2_READ, 19, app_buffer);
        size_t length = message(&f, f.wire + 4, SMB2_READ, SMB2_STATUS_SUCCESS, 19, signed_message);
        if (signed_message) sign_message(&f, f.wire + 4, length);
        if (tamper) f.wire[4 + 87] ^= 1;
        frame(&f, length);
        if (!signed_message || tamper) {
                CHECK(receive(&f) < 0);
                CHECK(f.callbacks == 0);
                CHECK(strstr(smb2_get_error(&f.context), signed_message ? "Wrong signature" : "Unsigned"));
        } else {
                CHECK(receive(&f) == 0);
                CHECK(f.callbacks == 1 && f.data_bytes == 32);
        }
        finish(&f);
}

static void error_case(uint16_t command, uint32_t status, int signed_message)
{
        struct fixture f;
        begin(&f, signed_message ? "signed error accepted" : "unsigned error rejected", SMB2_VERSION_0210);
        request(&f, command, 20, 0);
        size_t length = message(&f, f.wire + 4, command, status, 20, signed_message);
        if (signed_message) sign_message(&f, f.wire + 4, length);
        frame(&f, length);
        if (signed_message) {
                CHECK(receive(&f) == 0 && f.callbacks == 1 && (uint32_t)f.last_status == status);
        } else {
                CHECK(receive(&f) < 0 && f.callbacks == 0);
                CHECK(strstr(smb2_get_error(&f.context), "Unsigned"));
        }
        finish(&f);
}

static void handshake_case(uint16_t dialect, uint16_t command, uint32_t status)
{
        struct fixture f;
        begin(&f, "pre-key handshake response accepted", dialect);
        f.context.session_key_size = 0;
        f.context.session_id = 0;
        request(&f, command, 21, 0);
        frame(&f, message(&f, f.wire + 4, command, status, 21, 0));
        CHECK(receive(&f) == 0 && f.callbacks == 1 && (uint32_t)f.last_status == status);
        finish(&f);
}

static void session_final_case(int signed_message, int have_key,
                               uint16_t session_flags, int sign_enabled)
{
        struct fixture f;
        begin(&f, "SMB3.1.1 final authentication SIGNED presence and deferred crypto", SMB2_VERSION_0311);
        f.context.session_key_size = have_key ? 16 : 0;
        f.context.sign = sign_enabled;
        request(&f, SMB2_SESSION_SETUP, 28, 0);
        size_t length = message(&f, f.wire + 4, SMB2_SESSION_SETUP, SMB2_STATUS_SUCCESS, 28, signed_message);
        put16(f.wire + 4 + 66, session_flags);
        if (signed_message) sign_message(&f, f.wire + 4, length);
        frame(&f, length);
        if (!signed_message && !have_key) {
                CHECK(receive(&f) < 0 && f.callbacks == 0);
                CHECK(strstr(smb2_get_error(&f.context), "Unsigned SMB3.1.1 session setup"));
        } else {
                /* This recording callback deliberately does not run the real
                 * auth/key-derivation callback. Crypto remains deferred there.
                 */
                CHECK(receive(&f) == 0 && f.callbacks == 1);
        }
        finish(&f);
}

static void pending_case(int signed_final)
{
        struct fixture f;
        begin(&f, "unsigned async pending retained until signed final READ", SMB2_VERSION_0210);
        request(&f, SMB2_READ, 22, 1);
        size_t length = message(&f, f.wire + 4, SMB2_READ, SMB2_STATUS_PENDING, 22, 0);
        put32(f.wire + 4 + 16, SMB2_FLAGS_SERVER_TO_REDIR | SMB2_FLAGS_ASYNC_COMMAND);
        frame(&f, length);
        CHECK(receive(&f) == 0 && f.callbacks == 0 && f.context.waitqueue);
        f.position = 0;
        length = message(&f, f.wire + 4, SMB2_READ, SMB2_STATUS_SUCCESS, 22, signed_final);
        put32(f.wire + 4 + 16, SMB2_FLAGS_SERVER_TO_REDIR | SMB2_FLAGS_ASYNC_COMMAND |
                (signed_final ? SMB2_FLAGS_SIGNED : 0));
        if (signed_final) sign_message(&f, f.wire + 4, length);
        frame(&f, length);
        if (signed_final) {
                CHECK(receive(&f) == 0 && f.callbacks == 1 && f.data_bytes == 32 && !f.context.waitqueue);
        } else {
                CHECK(receive(&f) < 0 && f.callbacks == 0);
        }
        finish(&f);
}

static void bypass_case(uint16_t claimed_command, uint64_t claimed_session)
{
        struct fixture f;
        begin(&f, "READ cannot claim a handshake command or session zero", SMB2_VERSION_0210);
        request(&f, SMB2_READ, 23, 1);
        size_t length = message(&f, f.wire + 4, SMB2_READ, SMB2_STATUS_SUCCESS, 23, 0);
        put16(f.wire + 4 + 12, claimed_command);
        put64(f.wire + 4 + 40, claimed_session);
        frame(&f, length);
        CHECK(receive(&f) < 0 && f.callbacks == 0);
        finish(&f);
}

static void encrypted_case(int tamper, int inner_signed)
{
        struct fixture f;
        uint8_t plain[256];
        begin(&f, tamper ? "modified encrypted READ rejected" : "authenticated encrypted READ accepted", SMB2_VERSION_0300);
        request(&f, SMB2_READ, 24, 1);
        size_t length = message(&f, plain, SMB2_READ, SMB2_STATUS_SUCCESS, 24, inner_signed);
        /* A real AES-CCM transform authenticates the inner header and body. */
        uint8_t *transform = f.wire + 4;
        memset(transform, 0, 52);
        memcpy(transform, "\xfdSMB", 4);
        for (unsigned i = 0; i < 11; ++i) transform[20 + i] = (uint8_t)(i + 1);
        put32(transform + 36, (uint32_t)length);
        put16(transform + 42, 1);
        put64(transform + 44, f.context.session_id);
        memcpy(transform + 52, plain, length);
        aes128ccm_encrypt(f.context.serverout_key, transform + 20, 11,
                          transform + 20, 32, transform + 52, length, transform + 4, 16);
        if (tamper) transform[52 + 87] ^= 1;
        frame(&f, 52 + length);
        if (tamper) {
                CHECK(receive(&f) < 0 && f.callbacks == 0);
                CHECK(strstr(smb2_get_error(&f.context), "decryp"));
        } else {
                CHECK(receive(&f) == 0 && f.callbacks == 1 && f.data_bytes == 32);
        }
        finish(&f);
}

static void compound_case(int tamper, int pause)
{
        struct fixture f;
        begin(&f, tamper ? "modified compound READ rejected" : "signed compound READs accepted after short read", SMB2_VERSION_0210);
        request(&f, SMB2_READ, 25, 0);
        request(&f, SMB2_READ, 26, 1);
        size_t first = message(&f, f.wire + 4, SMB2_READ, SMB2_STATUS_SUCCESS, 25, 1);
        put32(f.wire + 4 + 20, (uint32_t)first);
        sign_message(&f, f.wire + 4, first);
        size_t second = message(&f, f.wire + 4 + first, SMB2_READ, SMB2_STATUS_SUCCESS, 26, 1);
        sign_message(&f, f.wire + 4 + first, second);
        if (tamper) f.wire[4 + first + 87] ^= 1;
        frame(&f, first + second);
        if (pause) f.pause_at = 4 + first + 64 + 5;
        int rc = receive(&f);
        if (pause) {
                CHECK(rc == 0 && f.callbacks == 1);
                rc = receive(&f);
        }
        if (tamper) {
                CHECK(rc < 0 && f.callbacks == 1 && f.data_bytes == 32);
        } else {
                CHECK(rc == 0 && f.callbacks == 2 && f.data_bytes == 64);
        }
        finish(&f);
}

static void unsolicited_case(uint16_t command, int signed_message)
{
        struct fixture f;
        begin(&f, "unsigned unsolicited oplock notification allowed", SMB2_VERSION_0210);
        if (command == SMB2_OPLOCK_BREAK) {
                /* Supply the usual notification PDU with a recording callback. */
                f.context.pdu = smb2_allocate_pdu(&f.context, SMB2_OPLOCK_BREAK, done, &f);
                CHECK(f.context.pdu);
        }
        frame(&f, message(&f, f.wire + 4, command, SMB2_STATUS_SUCCESS, UINT64_MAX, signed_message));
        if (command == SMB2_OPLOCK_BREAK) {
                CHECK(receive(&f) == 0 && f.callbacks == 1);
        } else {
                CHECK(receive(&f) < 0 && f.callbacks == 0);
        }
        finish(&f);
}

static void signing_option_case(int require_signing)
{
        struct fixture f;
        begin(&f, require_signing ? "cleared SIGNED flag rejected" : "optional signing behavior preserved", SMB2_VERSION_0210);
        request(&f, SMB2_READ, 27, 0);
        size_t length = message(&f, f.wire + 4, SMB2_READ, SMB2_STATUS_SUCCESS, 27, 1);
        sign_message(&f, f.wire + 4, length);
        put32(f.wire + 4 + 16, SMB2_FLAGS_SERVER_TO_REDIR);
        frame(&f, length);
        f.context.sign = require_signing;
        if (require_signing) {
                CHECK(receive(&f) < 0 && f.callbacks == 0);
        } else {
                CHECK(receive(&f) == 0 && f.callbacks == 1 && f.data_bytes == 32);
        }
        finish(&f);
}

int main(void)
{
        const uint16_t dialects[] = { SMB2_VERSION_0202, SMB2_VERSION_0210,
                SMB2_VERSION_0300, SMB2_VERSION_0302, SMB2_VERSION_0311 };
        for (unsigned d = 0; d < sizeof(dialects) / sizeof(dialects[0]); ++d) {
                for (int app = 0; app < 2; ++app) {
                        read_case(dialects[d], 1, 0, app);
                        read_case(dialects[d], 0, 0, app);
                        read_case(dialects[d], 1, 1, app);
                }
        }
        const uint32_t errors[] = { SMB2_STATUS_ACCESS_DENIED, SMB2_STATUS_END_OF_FILE,
                SMB2_STATUS_USER_SESSION_DELETED, SMB2_STATUS_NETWORK_NAME_DELETED,
                SMB2_STATUS_FILE_CLOSED };
        for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
                error_case(SMB2_READ, errors[i], 1);
                error_case(SMB2_READ, errors[i], 0);
        }
        handshake_case(SMB2_VERSION_0210, SMB2_NEGOTIATE, SMB2_STATUS_SUCCESS);
        handshake_case(SMB2_VERSION_0210, SMB2_NEGOTIATE, SMB2_STATUS_NOT_SUPPORTED);
        handshake_case(SMB2_VERSION_0210, SMB2_SESSION_SETUP, SMB2_STATUS_MORE_PROCESSING_REQUIRED);
        handshake_case(SMB2_VERSION_0210, SMB2_SESSION_SETUP, SMB2_STATUS_SUCCESS);
        handshake_case(SMB2_VERSION_0210, SMB2_SESSION_SETUP, SMB2_STATUS_LOGON_FAILURE);
        handshake_case(SMB2_VERSION_0311, SMB2_NEGOTIATE, SMB2_STATUS_SUCCESS);
        handshake_case(SMB2_VERSION_0311, SMB2_NEGOTIATE, SMB2_STATUS_NOT_SUPPORTED);
        handshake_case(SMB2_VERSION_0311, SMB2_SESSION_SETUP, SMB2_STATUS_MORE_PROCESSING_REQUIRED);
        handshake_case(SMB2_VERSION_0311, SMB2_SESSION_SETUP, SMB2_STATUS_LOGON_FAILURE);
        const uint16_t session_flags[] = { 0, SMB2_SESSION_FLAG_IS_GUEST,
                SMB2_SESSION_FLAG_IS_NULL, SMB2_SESSION_FLAG_IS_ENCRYPT_DATA };
        for (unsigned i = 0; i < sizeof(session_flags) / sizeof(session_flags[0]); ++i) {
                for (int sign = 0; sign < 2; ++sign) {
                        session_final_case(0, 0, session_flags[i], sign);
                        session_final_case(1, 0, session_flags[i], sign);
                }
        }
        session_final_case(0, 1, 0, 0);
        session_final_case(1, 1, 0, 1);
        pending_case(1);
        pending_case(0);
        bypass_case(SMB2_SESSION_SETUP, 0);
        bypass_case(SMB2_NEGOTIATE, 0);
        bypass_case(SMB2_READ, 0);
        encrypted_case(0, 0);
        encrypted_case(0, 1);
        encrypted_case(1, 0);
        compound_case(0, 0);
        compound_case(0, 1);
        compound_case(1, 1);
        unsolicited_case(SMB2_OPLOCK_BREAK, 0);
        unsolicited_case(SMB2_OPLOCK_BREAK, 1);
        unsolicited_case(SMB2_READ, 0);
        signing_option_case(1);
        signing_option_case(0);
        printf("SMB transport signing: %u cases passed (real receive/parser/crypto; no network)\n", cases);
        return 0;
}
