# Tester prompts (read this first)

The live tester (ChatGPT on the owner's PC) starts here. Each prompt below is
self-contained: build or install the `test` branch at the commit shown (or
newer), run it, and reply with the report template in
[TEST-CHANNEL](TEST-CHANNEL.md). **Report only** unless a prompt says
otherwise: do not change code, settings files or GitHub releases. Never
merge, tag or publish.

Claude adds a prompt here with every change that needs a live check, and moves
it to *Done* once a report arrives. Open prompts are listed newest first; when
time is short, run them in that order.

## Open

### T-2026-11-02-C · Two-GPU frame generation (check 23) · FSR `8a89d55`+

Setup: monitor on the second GPU (Intel B570). X-Plane renders on the AMD card
(X-Plane's Zink GPU choice, or Windows Settings > Display > Graphics). FSR on;
FG On (SDK paced, not synchronized); *Frame generation GPU: Second GPU*;
restart X-Plane.

1. Fly the same 2-minute flight. Then switch to *Frame generation GPU: Same as
   X-Plane*, restart, and fly it again.
2. Report for both runs:
   - real and displayed FPS (frame counter) and 1% lows
   - GPU-Z load on both cards
   - PCIe width from GPU-Z *Bus Interface*
   - what Diagnostics > Frame generation says
3. Copy every renderer-log line starting with `Second GPU`, `Two-GPU` or
   `AMD FG swapchain unavailable`.
4. Resize the window once and alt-tab once; say whether the picture recovers.
5. If it crashes, attach the renderer log and X-Plane's `Log.txt`.

### T-2026-11-02-B · Low FPS with Zink since 12.4.4 (P0 investigation)

Find where the drop comes from; do not fix anything. Use the same saved flight
and view for every run. For each run, record:

- average FPS and 1% lows
- X-Plane's frame and GPU ms
- Diagnostics > *PERFORMANCE PER FRAME*

Runs:

1. *Image processor: Original simulator* (both mods off): Zink on vs off. This
   shows whether the drop is X-Plane itself.
2. FSR mod, then Native AA: Zink on vs off.
3. With the B570 installed:
   - Check X-Plane's render GPU choice is the AMD card.
   - Find in `Log.txt` which GPU Zink uses. Zink on another card would send
     every plugin drawing across PCIe.
   - Repeat run 1 with the B570's monitor unplugged, or with the B570 disabled
     in Device Manager.
4. With Zink on: panels closed vs open, then plugins disabled one at a time.
5. Try `--gfx_no_defragment` once (12.4.4 stutter workaround).

Attach `Log.txt` and the renderer logs for runs 1-3. Say which run first shows
the drop.

### T-2026-11-02-A · New FSR options and 12.4.4 (checks 14-21) · FSR `80dbb15`+

1. **Check 14:** Diagnostics *PERFORMANCE PER FRAME* for FSR only, FSR + FG,
   and FSR + FG synchronized. Report mod work / waits / present call / frame,
   plus X-Plane frame and GPU ms.
2. **Check 15:** the *Driver pacing* row matches the log's pacing check;
   turning VSync on adds "turn VSync off".
3. **Check 16:** *CPU scheduling: Helper* vs *Windows decides*, same 2-minute
   flight. Report CPU model, FPS and 1% lows. The log must end with
   `restored Windows defaults`.
4. **Check 17:** FSR 4 load plus a window resize: no freeze, and the status
   shows *FSR GETTING READY (N s)*. Report `FSR context preparation: N ms` for
   a cold and a warm start.
5. **Check 18:** *Anti-Lag 2* On/Off, with FG Off and with FG On. Diagnostics
   shows On or why not. Report PresentMon latency and FPS for all four runs.
6. **Check 19:** panel rows animate in on a tab change; with Windows
   animations off, they don't. All buttons still work.
7. **Check 20:** *Texture detail* X-Plane default vs AMD recommended, restart
   between:
   - Report whether runway and city textures are sharper.
   - Check sky, clouds, water, bloom and cockpit screens look the same.
   - Copy the log lines `Texture detail: sampler ... bias A -> B`; the A value
     is X-Plane's own bias.
8. **Check 21:** everything above on X-Plane 12.4.4 final. Note anything that
   behaves differently from 12.4.3.
9. **Check 13 again:**
   - Esc on a pinned help card.
   - Airborne camera-reset count.
   - FG vs FG synchronized: source FPS and `sync-drain-avg-ms`.

### T-2026-11-02-I · Intel B570 run (single GPU)

1. Plug the monitor into the B570 and set X-Plane to render on it.
2. Repeat checks 9-11 and 14-20 with FSR 3.1 (FSR 4 is AMD-only).
3. Copy every "Not available" reason exactly as shown. Anti-Lag 2 and driver
   pacing are expected to say they need AMD.

### T-2026-11-01 · Motion vectors and timers (checks 9-12) · FSR `0066921`+

- Motion source Camera vs Native: compare trails on the airframe and cockpit,
  and native motion with FG, MSAA and the cockpit fix.
- Motion-quality capture (check 11) and motion survey (check 12): attach the
  outputs.

## Done

(Move prompts here with the date of the report and its verdict.)
