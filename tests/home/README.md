# Home-screen startup host regressions

Run these with Linux/WSL Python3 and clang-18 from a source checkout. Each creates a fresh output component; it does not contact a console. `--candidate` optionally supplies another source tree while `--root` supplies matching build helpers/dependency headers.

```sh
python3 tests/home/check_session.py --output build/home-host
python3 tests/home/check_backend.py --output build/home-host
python3 tests/home/check_main.py --output build/home-host
python3 tests/home/check_provision.py --output build/home-host
```

Session tests compile the actual protocol/session/handoff/process-and-log evaluators with ASan/UBSan and create only temporary Linux fixture files. The session process/app/UID callbacks are recording models. Main tests extract the actual request parser/header/peer route unchanged and use real host TCP/Unix sockets; the backend queue and asset bytes are models. Backend tests extract the actual queue/lease/source-selection and cleanup functions unchanged, with modeled Sony, worker-creation and receipt/registration boundaries. Provision tests extract actual Send-completion/provision order unchanged, with source-copy/receipt/bundle/Sony boundaries modeled. They prove reader installation precedes registration, known setup failure preserves VALID startup bytes, and both releases are attempted even when one is unknown. None proves native module startup, a game menu, throughput, or the Sony exit UI. Those require controlled console qualification.
