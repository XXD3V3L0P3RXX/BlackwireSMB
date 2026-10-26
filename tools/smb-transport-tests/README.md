# Offline SMB receive integrity check

Run from Linux or WSL with Python 3 and a host C compiler:

```sh
python3 tools/smb-transport-tests/run.py --sanitize
```

The runner locates either `src/vendor/libsmb2` (canonical layout) or
`vendor/libsmb2` (public layout). `--vendor /path/to/libsmb2` selects another
source tree. The only generated executable and configuration live in a temporary
host directory. The runner does not invoke the project build, payload SDK,
production archive build, upload tools, console, or network services.

The fixture includes the actual vendor `socket.c`, replacing only its `readv`
call with a short-read memory stream. It links the actual vendor vector helpers,
PDU/header/payload parsers, signing implementations, and AES-CCM implementation.
It observes callbacks and uses both library-allocated and application-provided
READ buffers. ASan and UBSan stop on their first error when `--sanitize` is used.

## Local source patch

When `smb2_set_sign(context, 1)` has enabled required signing, the client receive
path rejects ordinary unsigned replies before their application callback. The
prior code entered verification only if the received header already advertised
`SMB2_FLAGS_SIGNED`, allowing the sender to bypass verification by omitting it.
The original HMAC-SHA256/AES-CMAC calculation and signature comparison remain.

The response command must match its correlated request before parsing. This
prevents a READ reply from claiming NEGOTIATE or SESSION_SETUP to select a
handshake exception while still being decoded as the original READ. A received
zero SessionId also cannot turn off the signing requirement. Ordinary error
statuses receive the same integrity check as successful replies.

The active header-vector index is retained in the private context across
`EAGAIN`. The old function-local compound offset was lost on a later call,
causing valid signed compound replies to be checked against the wrong vectors.

The client continues to exempt successfully authenticated SMB3 transforms,
interim `STATUS_PENDING` responses, and unsolicited oplock notifications with
MessageId `0xffffffffffffffff`, as required by
[MS-SMB2 section 3.2.5.1.3, Verifying the Signature](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-smb2/36172e53-ac81-48fb-b2e3-caa3761b9157).
The existing receive path rejects an unmatched unsolicited READ; only an
OPLOCK_BREAK is a valid unsolicited message under
[section 3.2.5.1.2, Finding the Application Request for This Response](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-smb2/be447bed-3e76-4fad-9999-3d647930b6d1).
Pending responses retain the request until the final response, which still
requires integrity, consistent with
[section 3.2.5.1.5, Handling Asynchronous Responses](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-smb2/a01c6101-b94b-45c1-b7ec-cbcc94d51138).

NEGOTIATE and pre-key SESSION_SETUP handling is preserved. For a new SMB3.1.1
authentication, a successful final SESSION_SETUP must carry SIGNED, including
when the response advertises guest, null-session, or encryption flags. This is
the explicit final-response requirement in
[section 3.2.5.3.1, Handling a New Authentication](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-smb2/7fd079ca-17e6-4f02-8449-46b606ea289c).
The local library obtains the session key in `session_setup_cb`, so the transport
uses the absence of an existing session key to distinguish new authentication;
it still defers cryptographic verification of that final reply to the existing
callback. It preserves existing-key reauthentication, which follows
[section 3.2.5.3.2, Handling a Reauthentication](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-smb2/c4887c81-8340-4f0f-8264-cf90d841f838).

Clients that require signing must set both local policy and the wire request
before connecting:

```c
smb2_set_sign(context, 1);
smb2_set_security_mode(context, SMB2_NEGOTIATE_SIGNING_ENABLED |
                                SMB2_NEGOTIATE_SIGNING_REQUIRED);
```

## Coverage and limits

The host fixture contains 83 cases: valid, unsigned, and payload-tampered READs
for SMB 2.0.2/2.1/3.0/3.0.2/3.1.1; signed and unsigned errors; pending and final
async replies; negotiate/session continuation/error replies; final SMB3.1.1
SIGNED-presence checks; command/session disguises; valid and corrupted AES-CCM
transforms; compound replies interrupted during the second response; unsolicited
notifications; a cleared SIGNED bit; and optional-signing compatibility.

Before the transport patch, this fixture failed because an unsigned READ reached
its application callback. Auth cases use a recording callback instead of the
NTLM/Kerberos authentication callback. They verify transport acceptance/rejection
and crypto deferral, not a complete login, key derivation, guest admission,
server interoperability, or session reauthentication exchange. No production
artifact was rebuilt or exercised. Source changes require a future authorized
production rebuild before they affect deployed readers.

## AES-CMAC key schedule check

Run the focused offline CMAC fixture with a new output directory:

```sh
python3 tools/smb-transport-tests/check_cmac.py --output /tmp/blackwire-cmac-check
```

The fixture compiles the actual vendored CMAC and AES implementation with ASan
and UBSan. It checks the four [RFC 4493 section 4](https://www.rfc-editor.org/rfc/rfc4493#section-4)
vectors, empty/partial/full/multiple-block and long messages, compatibility ECB
wrappers, independently keyed immutable contexts and per-message key-schedule
initialization counts. A reference through the existing per-block ECB entry
point is compared byte for byte with production CMAC. One 1 MiB message requires
one expanded schedule instead of 65,537 per-block expansions. These counts
describe work removed; they are not a measured console throughput improvement.

The production change keeps one caller-owned schedule on the stack for one
CMAC message, then clears it. There is no shared last-key cache or new heap
allocation. Required signing, algorithm selection, signature comparison and
receive acceptance rules remain unchanged; the receive fixture above must also
pass after changing the cryptographic implementation.

On supported x86 compilers, context initialization checks CPUID for AES and
SSE2 before selecting the isolated AES-NI encryptor. Other CPUs retain the
pure-C implementation. The helper explicitly disables AVX so its instructions
match that feature gate even when the SDK has a broader default CPU baseline.
The fixture runs both automatic selection and a separate compile forced to
software, then checks that generic entry points contain no AES instructions.
The forced-software define exists only for offline test compilation; there is
no user-facing signing toggle or instruction-set override.
