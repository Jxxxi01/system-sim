# demo_ablation

This demo is an Issue 14B ablation narrative for a cross-user attack (`Alice` vs `Bob`).

It uses three independent layers. Each layer creates its own `SecurityHardware`, `Gateway`, and
`KernelProcessTable` so the observed behavior comes only from that layer's configured defenses.

Actors:

- Alice: `user_id=1001`, `base_va=0x1000`, nominal `key_id=11`
- Bob: `user_id=1002`

The three layers are:

1. Full security
   - Alice is loaded at `0x1000` with `key_id=11`.
   - Bob is loaded at `0x2000` with `key_id=22`.
   - Bob's program contains a direct `J` into Alice's base address.
   - Expected result: `FINAL_REASON=EWC_ILLEGAL_PC`.
   - Interpretation: cross-user execution is blocked at Fetch by Bob's active EWC windows.

2. EWC bypassed, key barrier remains
   - Alice is loaded at `0x1000` with `key_id=11`.
   - Bob is loaded at the same `base_va=0x1000` with `key_id=22`.
   - After both loads, a malicious OS overwrites Bob's `code_region` with Alice's encrypted bytes.
   - The malicious OS then installs a wildcard Bob window covering the injected instruction address
     range, with `owner_user_id=1002` and `key_id=22`.
   - Expected result: `FINAL_REASON=DECRYPT_DECODE_FAIL`.
   - Interpretation: execution passes the weakened EWC policy, but the ciphertext still does not
     decrypt under Bob's key.

3. All removed, attack succeeds
   - Alice is loaded at `0x1000` with `key_id=11`.
   - Bob is loaded at the same `base_va=0x1000`, also with `key_id=11`, and `cfi_level=0`.
   - The same code-region overwrite and wildcard Bob window are applied.
   - Expected result: `FINAL_REASON=HALT`.
   - Additional observable: the register result matches Alice's computation, and the demo prints an
     explicit confirmation line of the form
     `ATTACK_SUCCESS=Alice code executed under Bob context_handle=<X> user_id=1002`, where `<X>` is
     extracted from the `CTX_SWITCH` audit event.
   - Interpretation: once EWC separation and key separation are both removed, Alice's payload can
     execute successfully under Bob's `context_handle`.

Claim boundary:

- This demo shows progressive degradation of the prototype's execution-path protections under a
  malicious-OS overwrite scenario.
- It does not claim complete system compromise modeling, realistic cryptography, or protection
  against every memory/data attack path outside the prototype's current scope.
