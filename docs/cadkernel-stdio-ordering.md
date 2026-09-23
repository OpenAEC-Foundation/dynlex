# JSON-RPC stderr capture at message delivery

The complete required run on compiler
`d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`
recorded 621 passes and one `lsp_client_lifecycle` output mismatch. The same
unmodified fixture reproduced on the earlier 610-pass compiler
`ec4f37ae0da9455353f8825f05f69caed3b3567e529d484e6037993f8c0e85bc`:
22/24 current and 23/24 earlier executions passed. Failed executions returned
success but reported false at the two stderr assertions. This establishes a
preexisting capture-order issue, not an inference regression.

## Delivery boundary

The connection drained stderr before reading stdout. Bytes arriving after
that drain could remain unread when the frame was delivered. An already
buffered frame bypassed the drain entirely, including the configured stderr
limit. Complete-frame delivery now performs the ordinary nonblocking stderr
drain before parsing/returning the payload and propagates its real error status.
The drain is shared with normal reads and close; it does not suppress failures,
retry a failed assertion or introduce a sleep.

## Independent Windows readers

Windows uses independent stdout and stderr reader threads. The child writing
stderr before stdout does not guarantee the stderr reader has published its
bytes when the stdout frame is observed. The lifecycle fixture now verifies
that the early take plus the final capture contains the two stderr writes in
order, exactly once. Shutdown joins the readers. The early take may be empty;
the final combined-content check still detects lost or duplicated bytes.

`json_rpc_buffered_stderr` separately establishes process completion before
delivering a prebuffered frame. That makes pending stderr deterministic. It
checks both successful collection and exact truncation/broken-connection status
when the configured limit is exceeded. The final synchronized fixture fails at
O0/O2 without the delivery drain and passes at both levels with it. The existing
expected output of the lifecycle fixture is unchanged.

Evidence: `build/lsp-lifecycle-comparison.log`,
`build/json-rpc-stderr-confirmed-red.log`,
`build/json-rpc-stderr-targeted.log`. After repair, both compiler builds pass
24/24 lifecycle executions each (`build/lsp-lifecycle-repaired.log`). The full
required suite subsequently passed **633 checks, zero failures and zero skips**
on compiler `d78976c58457424dbd67474e8df46a7d77f8f76f5632e8f9879f5b7eb793ccf1`.
Evidence: `build/required-area2-budget.log`. Its LSP lifecycle, buffered-stderr
and transport checks all passed.
