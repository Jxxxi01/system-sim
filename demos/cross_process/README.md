# demo_cross_process

This demo shows same-user, cross-process execution-window isolation enforced by EWC.

Both processes belong to `user_id=1001`, but they are loaded as distinct contexts with different
`context_handle`, `window_id`, `key_id`, and `base_va` values:

- Process A: `context_handle=<runtime assigned>`, `window_id=1`, `key_id=11`, `base_va=0x1000`
- Process B: `context_handle=<runtime assigned>`, `window_id=2`, `key_id=12`, `base_va=0x2000`

The demo runs two cases:

1. `CASE_A_NORMAL_EXECUTION`
   - Load only process A.
   - Context-switch to A and execute normally.
   - Expected result: `FINAL_REASON=HALT`.

2. `CASE_B_SAME_USER_CROSS_PROCESS`
   - Load both process A and process B.
   - Process B contains a `J` instruction that jumps into process A's code window at `0x1000`.
   - Context-switch to B and execute.
   - Expected result: `FINAL_REASON=EWC_ILLEGAL_PC`.
   - The audit stream must contain an `EWC_ILLEGAL_PC` event with `user_id=1001` and B's
     `context_handle`.

Claim boundary:

- This demo demonstrates per-process execution isolation for the same user via EWC.
- It does not claim full cross-process data isolation, memory confidentiality, or protection against
  every malicious-OS data path.
