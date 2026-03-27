# demo_malicious_mapping

This demo models cross-USER malicious page mapping and shows how PVT and EWC provide layered
protection.

Actors:

- Alice: `user_id=1001`
- Bob: `user_id=1002`

The demo covers three cases:

1. Normal execution
   - Alice runs inside her own execution window.
   - Expected result: `FINAL_REASON=HALT`.

2. `PVT_MISMATCH` on malicious mapping
   - A malicious OS attempts to register Alice's virtual page under Bob's `context_handle`.
   - PVT queries the EWC windows registered under the supplied `context_handle`, finds no matching
     code window for Alice's `base_va`, and rejects the registration.
   - Expected observable outcome: `REGISTER_PAGE_OK=false` and an audit event of type
     `PVT_MISMATCH`.

3. EWC defense-in-depth
   - Even if a malicious OS bypasses the mapping intent and makes Bob's code jump toward Alice's
     execution window, fetch still re-validates the destination against Bob's active EWC windows.
   - Expected result: `FINAL_REASON=EWC_ILLEGAL_PC`.

The purpose of this demo is to show that:

- PVT blocks cross-user malicious page registration at mapping time.
- EWC independently blocks cross-user execution at fetch time.
- The combined behavior is defense in depth rather than reliance on a single enforcement point.
