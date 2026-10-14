# Concurrent networking roadmap

Status: proposed design, 2026-10-11. The phases below are not implemented by this document.

## Goal

Use one dedicated network thread to advance multiple connections concurrently. Expose synchronous, callback-based asynchronous, and eventually suspend APIs over the same operation engine. Keep callbacks and application objects on their originating thread, support Amiga task ownership, and bound memory independently of the number of resources a page discovers.

This is the shared roadmap for `am-web-client`, `am-net`, and `am-ssl`. It requires no new networking dependency; TLS continues to use the existing OpenSSL/AmiSSL backends.

## Current starting point

- `am-net` exposes blocking connect/send/receive operations. Hostname resolution also blocks.
- Existing async socket and web wrappers dispatch blocking work to an IO worker. That keeps the caller free but does not provide a readiness loop that advances many connections.
- The isolate runtime copies messages between heaps. Native sockets cannot be sent between threads, and mutations to a copied callback capture do not update the original object.
- Tucanium's loader currently fetches HTML, stylesheets, scripts, fonts, and images sequentially. Changing the socket implementation alone will not make a loop of synchronous HTTP calls concurrent.

## Package responsibilities

| Package | Responsibility |
| --- | --- |
| `am-net` | Network service, connection handles, nonblocking TCP, readiness waiting and wakeup, resolver dispatch, operation queues, deadlines, cancellation, bounded buffers, sync/async socket adapters. |
| `am-ssl` | TLS sessions owned by the network thread, nonblocking handshake/read/write/shutdown, verification, TLS readiness requirements and cleanup. |
| `am-web-client` | HTTP request/response state machines, framing, request scheduling, streaming, redirects, connection reuse, and public sync/async HTTP APIs. |
| Isolate runtime / `am-lang-core` | Any missing completion-wait and dispatcher primitives; safe ownership and thread lifecycle support. |
| Tucanium | Submit concurrent resource requests, prioritize visible content, progressively render, and preserve CSS and script semantics. |

## Execution and ownership contract

```text
Caller thread                            Network thread
register local completion / callback
submit operation ID + connection ID ---> queue operation
                                         advance ready connections
                                         wait for readiness or commands
receive result / error / cancellation <-- publish completion
return from sync call OR dispatch lambda
```

- The network thread owns native sockets, TLS objects, and their cleanup for their entire lifetime. Caller-side handles identify those objects without exposing native pointers.
- Use one explicitly shared service handle for participating callers. Isolate-local statics must not accidentally create a separate service for every caller.
- Keep callback lambdas in a registry on the originating thread. Send request IDs and sendable data, then route completions back by ID. Do not send a lambda capturing UI state to the network thread.
- A synchronous operation sleeps until its own completion arrives, then returns or throws on the caller. It still blocks the caller, including the UI when invoked there.
- An asynchronous operation returns a cancellable handle immediately. The caller's event loop dispatches completion later, never inline and never on the network thread. Document the required event-loop pump for CLI and UI programs.
- A synchronous wait must not consume unrelated mailbox messages or require that same blocked thread to run an async callback. Use a dedicated completion channel or runtime-supported matching wait. Avoid pumping arbitrary callbacks while synchronously waiting.
- Reject synchronous calls from the network thread that would wait for that thread itself.
- Preserve byte order within a connection, handle partial reads/writes, and distinguish EOF, would-block, and failure. Define concurrent-operation rules explicitly, initially allowing one active read and one active write per connection with bounded queues.
- Every accepted operation has one terminal outcome. Resolve completion/cancel/timeout races once; use generation IDs so stale completions cannot target a reused handle. Release registrations if their owning caller exits.

## Phase 1 — Runtime contract and nonblocking TCP

- [ ] Define operation/result types, connection IDs, ownership, error mapping, and callback dispatch API before fixing public API names.
- [ ] Add nonblocking connect/read/write primitives, including connect completion error checks and partial write offsets.
- [ ] Add a native readiness abstraction. Start with a small portable backend; use platform-specific waiting behind it rather than exposing OS descriptors to applications.
- [ ] Wake a sleeping readiness loop for submissions, cancellation, shutdown, and the nearest deadline. Avoid busy polling and lost wakeups.
- [ ] Use monotonic deadlines and fair per-connection byte/work quotas so one busy connection cannot starve others.
- [ ] Keep blocking DNS outside the network loop using a bounded resolver worker/queue or platform async resolver. Late DNS results must be discardable after cancellation; a timeout need not imply that a blocking resolver call can be forcibly interrupted.
- [ ] Implement caller completion registration and both synchronous and callback socket adapters.

Acceptance: a delayed local connection cannot delay a second connection; idle operation does not spin; partial writes preserve data; callbacks run on the originating thread; synchronous waits preserve unrelated messages; cancellation and shutdown cannot deadlock.

## Phase 2 — Nonblocking TLS in am-ssl

- [ ] Integrate TLS as a connection state machine on the same network thread, without a reverse dependency from `am-net` to `am-ssl`.
- [ ] Advance handshake, read, write, and shutdown incrementally. Handle both WANT_READ and WANT_WRITE from every applicable TLS operation, including reads that require writing and writes that require reading.
- [ ] Preserve retry buffers and arguments as required by the TLS backend, and service already-buffered plaintext without waiting unnecessarily for another socket event.
- [ ] Preserve certificate-chain and hostname verification, SNI, and existing trust configuration. Never bypass verification to make an async operation succeed.
- [ ] Apply handshake and shutdown deadlines; distinguish orderly TLS closure from transport truncation and protocol failure.
- [ ] Keep socket, TLS, library initialization, and finalization on the owning task on Amiga-family platforms. Verify each backend's readiness/wakeup API and library behavior before claiming support.

Acceptance: simultaneous local TLS connections make independent progress; stalled handshakes time out; invalid certificates and hostnames fail; repeated cancellation/close frees sessions; Amiga tests confirm task ownership and cleanup.

TLS retry reference: [OpenSSL SSL_get_error](https://docs.openssl.org/1.1.1/man3/SSL_get_error/).

## Phase 3 — HTTP integration in am-web-client

- [ ] Build sync and callback HTTP APIs on the common transport engine; migrate the existing async wrappers after behavior is covered by tests.
- [ ] Parse headers and bodies incrementally, including content-length, chunked framing, and close-delimited responses. Do not use read-until-close for reusable connections.
- [ ] Offer bounded streaming with consumer backpressure plus a size-limited convenience API that collects a whole response.
- [ ] Apply limits to headers, received bytes, and decoded/decompressed bodies, with explicit errors when limits are reached.
- [ ] Start with four active HTTP requests globally and two per origin as configurable initial limits. Bound queued requests too; tune using measurements rather than starting unlimited work.
- [ ] Add bounded keep-alive connection reuse once framing and cancellation are reliable. Evict idle connections and discard connections whose response framing is incomplete or uncertain.
- [ ] Apply deadlines and cancellation across DNS, connect, TLS, redirects, and body transfer. Keep redirect limits and credential/header forwarding policy explicit; do not automatically retry non-idempotent requests.
- [ ] Add suspend adapters only after callback dispatch and isolate ownership work reliably; resume on the originating thread.

Acceptance: local HTTP/HTTPS fixtures cover slow responses, chunked and truncated bodies, redirects, cancellation, streaming backpressure, connection reuse, and matching sync/async results. A queued slow request cannot block unrelated active requests.

## Phase 4 — Tucanium resource scheduling

- [ ] Submit multiple asynchronous resource requests rather than looping over blocking `get` calls.
- [ ] Deliver HTML and useful intermediate results before every asset has finished; progressively update layout and paint.
- [ ] Prioritize stylesheets, required scripts, fonts, and visible images; defer lower-priority resources within bounded queues.
- [ ] Preserve stylesheet source order regardless of completion order. Implement classic, async, defer, and module script scheduling according to their supported semantics; download completion order must not define script execution order.
- [ ] Cancel requests from superseded navigations and ignore stale completions.
- [ ] Keep image decoding and other expensive application work outside the network loop, with separate concurrency and decoded-pixel limits.

Acceptance: controlled delayed assets demonstrate overlapping downloads and earlier useful rendering; CSS and script ordering tests still pass; navigating away releases queued requests, callbacks, and buffers.

## Memory and lifecycle requirements

- Keep Tucanium's existing **1 GiB process limit** throughout implementation and testing. Generic libraries expose configurable budgets rather than imposing the browser's process policy on every application.
- Bound active connections, pending operations, resolver work, queued completions, and receive/send buffers. Account for both sides of copied inter-isolate messages and decoded HTTP bodies.
- Pause reads when the consumer falls behind. Do not let an unpumped caller's completion mailbox grow without limit; use credits/acknowledgements or another explicitly bounded delivery protocol.
- Collect on the network thread itself; collection on the UI thread does not collect another isolate's heap. Release native resources explicitly on completion/close/shutdown rather than relying only on GC.
- Stop accepting work during shutdown, cancel pending operations, wake all synchronous waiters with a terminal result, release callbacks and buffers, then close native resources on their owning thread.
- Expose counters for active/queued operations, buffered bytes, cancellations, timeouts, and connection reuse so soak tests can detect retained state.

## Validation and rollout

1. Use deterministic local TCP, TLS, and HTTP fixtures first. Include delayed peers, partial transfers, cancellation races, caller exit, unpumped consumers, and shutdown with pending waits.
2. Test native code with sanitizers on supported development hosts. Run repeated connect/read/cancel/close cycles and verify descriptor, native allocation, and managed-object counts settle.
3. Validate macOS first, then Linux and the Amiga-family backends individually. Record cross-compilation separately from actual runtime verification.
4. Compare serial and concurrent HTTP loading for identical fixtures: first useful render, total transfer time, idle CPU, and peak/steady memory. Use bounded browser runs for VG and Rice & Spice only after local tests pass.
5. Retain a controlled migration path until synchronous behavior and existing callers pass regression checks. Document async dispatch requirements and any unsupported platform capabilities.

HTTP/2, HTTP/3, a new TLS implementation, and full browser standards coverage are outside the first milestone. First deliver independently progressing TCP/TLS connections with correct ownership, bounded memory, and reliable completion semantics.
