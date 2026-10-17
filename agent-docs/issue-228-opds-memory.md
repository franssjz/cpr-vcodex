# OPDS catalog memory and failure recovery (#228)

Implementation date: 2026-10-17. Prepared for release `1.6.5.6-cpr-vcodex`.

## Problem and evidence

[Issue #228](https://github.com/franssjz/cpr-vcodex/issues/228) reports that
Grimmory book lists fail on X3 with firmware 1.6.5.4. A one-book shelf opens
after restarting but stops opening after attempting a problematic shelf.
The same configuration worked previously, approximately in July.

The previous implementation created Expat and its entry storage before
starting HTTP, then parsed the feed while TLS remained active. This overlaps
network buffers with growing catalog strings on a C3 without PSRAM. The
reporter's actual allocation failure has not been captured on hardware;
native parser tests alone do not reproduce device networking or fragmentation.

## Reference and adaptation

Adapted the phase-separation design from
[CrossInk commit 6a45458f11504434d32e595b20de479432eeafd5](https://github.com/uxjulia/CrossInk/commit/6a45458f11504434d32e595b20de479432eeafd5),
linked by a Grimmory user in
[CrossInk #372](https://github.com/uxjulia/CrossInk/issues/372#issuecomment-4951366790).
That proposal is not included in CrossInk v1.6.1. Its current wolfSSL/minimal
boot solution and CrossPoint's newer OPDS 2.0 migration were reviewed but are
not transplanted into CPR-vCodex.

The adapted flow:

1. Clear old entries, search links, pagination flags and selection under the
   render lock. Complete the Loading render before the blocking operation.
2. Hold the render lock while releasing SD reader-font/runtime caches and
   performing the memory-sensitive transfer and parse. Reading restores the
   configured font through the existing font-system reload path.
3. Download to `/.crosspoint/opds-feed.tmp` through the existing verified C3
   HTTP/TLS path. Check the TLS memory floor for HTTPS feeds and reject failure
   to remove an old temporary file. The shared file downloader now checks the
   final SD close/flush result: if it fails after a successful transfer, remove
   the destination and return `FILE_ERROR`. Earlier HTTP errors keep precedence.
4. Only after the downloader has returned and destroyed HTTP/TLS, create Expat
   and read the file in 1,024-byte blocks. Check the file's expected length and
   yield periodically for the idle task/watchdog.
5. Stop at the first parser failure. Release the read buffer and close the file
   before building display rows. Scope cleanup closes/removes the temporary
   file on all paths; partial entries remain parser-owned and are destroyed.
6. Distinguish Expat allocation failures from invalid XML, including errors
   discovered only at finalization, using existing translated memory text.

URLs, credentials, the 62-entry cap, pagination, HTTPS verification and OTA
paths retain their existing behavior. No automatic restart or Wi-Fi reconnect
is added. The flow now requires a writable SD card and adds a temporary
write/read pass; storage failure returns through the normal retry/back screen.

## Validation

- Native MSVC Release suite: **785/785 passed**, including 31 new feed-loading
  cases, 7 parser recovery cases and 12 file-downloader cases. The feed tests
  compile the production activity methods, parser, Expat and URL resolver;
  hardware/network dependencies are mocked. They check HTTP teardown before
  parser creation, bounded reads, stale/partial file removal, SD/HTTP failures,
  memory errors, retry recovery, URL/authentication, pagination and the HTTPS-only
  heap floor.
- Downloader tests compile the real `downloadToFile` method and `Sink` with
  simulated transport and SD. They check failed final flushes after complete
  writes, closing before removal, previous-error precedence, short writes,
  empty transfers and successful recovery with preserved request parameters.
- Expat allocation tracking: **45 injected allocation failures**, followed by
  successful parsing, with zero retained Expat allocations. Invalid and truncated
  XML, including errors discovered at finalization, remain distinct from OOM.
- X4 simulator with a real loopback HTTP server: one session navigated
  `/root -> /small -> /root -> /bad -> /root -> /small`. All six Loading
  framebuffers matched; the small shelf and root matched before/after failure;
  the temporary file was removed and no restart or deadlock occurred. Screenshots
  were inspected visually. This checks UI/recovery, not ESP32 heap or TLS.
  After removing the fixture, a clean normal simulator build with a fresh cache
  and a Home-screen smoke test also passed; the header and menu were inspected.
- C3 `default` and `gh_release` builds passed. Static RAM remains **62,072 B**
  in both. Final `default`: **6,351,713 B** linked flash / **6,365,936 B** BIN.
  Final `gh_release`: **6,273,063 B** linked flash / **6,287,280 B** BIN,
  **95.72%** of the 6,553,600 B OTA slot. The 97.5% budget check passed with
  **116,697 B** headroom. This implementation validation used
  `VCODEX_RELEASE_DRY_RUN=1`; final release packaging and publication are
  checked separately by the release workflow.
- `clang-format --dry-run --Werror` on the changed C++ and `git diff --check`
  passed. No fixture hook was added to the production main or activity.

Logs and screenshots are retained locally in `artifacts/issue-228/`; the simulator
case is reproducible using `test/simulator/opds_feed_loading.py` and its isolated
fixture patch (see `agent-docs/simulator.md`). Physical X3 confirmation with the
reporter's Grimmory shelf remains necessary to establish that the change resolves
their exact report.
