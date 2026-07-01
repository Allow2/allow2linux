# Harden allow2linux against bypass on Steam Deck, using Steam Families / Family View

**Audience:** parents who have installed allow2linux on a child's Steam Deck and want to make it
meaningfully harder to bypass.
**Companion parent-facing page:** generated per channel by
[`flatpak/gen-hardening-page.sh`](../flatpak/gen-hardening-page.sh) and published as
`hardening.html` to `get.allow2.com/steamdeck/<channel>/` (wired into `scripts/publish.sh`).
**Grounding docs (cross-checked):** `docs/steamdeck-state-and-completeness.md` Part C (tamper landscape,
sourced), `docs/PROJECT_BRIEF.md` §"Critical rule" (new-Linux-user hole), the Allow2 enforcement/tamper
positioning (on-device agent = deterrent; durable authority lives off-device).

> **Honest one-line framing (non-negotiable, repeated on the page):** This is **layered hardening that
> makes bypass much harder, not absolute.** Any on-device control has a ceiling on a **stock** Steam Deck.
> The real strength is the layered stack **plus** Allow2's off-device authority, which **detects** the missing
> check-ins, **alerts the parent** when a device goes dark, and **reconciles / claws back** the overage. Coarse
> on-device control is a deterrent; the durable authority is off-device. We do not publish a bypass recipe:
> the "can't stop" side is framed as "we detect it and tell you", not "here is how to get around it".

---

## 1. Current state of Steam's own parental system (verify before relying)

Steam's parental system was **revamped in 2024–2025**. Two names you will see:

- **Steam Families:** the **current** system. Role-based (an **adult** manages; a **child** account is
  managed). Per-child controls for: game allow-listing, **daily/weekly playtime limits**, **scheduled
  access windows**, purchase approval, and community/chat toggles. Managed from the **parent's** Steam
  account. This **replaced** the older Family View for most of its function.
  Confidence: **HIGH** (multiple secondary guides + Valve help topic titles). [S1, S2, S3, S4]
- **Family View:** the **older** PIN-lockdown feature (a local 4-digit PIN that gated which parts of the
  Steam client were reachable). Its role has been folded into Steam Families. On the **Steam Deck** the
  **Game-Mode Security toggles** (below) are the surviving, high-value piece of this: they are what can
  **PIN-lock Desktop Mode**. Confidence: **HIGH** that the toggle exists; **exact current menu path
  `[unverified-device]`** because Valve's own FAQ page would not return body text to fetch (only nav chrome),
  so the path below is from secondary guides. [S2, S5, S6, S7]

> **§10 note (verify against reality):** Valve's official FAQ pages
> (`help.steampowered.com/.../Family View`, `.../Steam Families`) returned only navigation chrome to
> automated fetch, so **every exact menu label in this doc is corroborated from secondary guides, not
> lifted from Valve's live page.** Treat menu paths as **strong hints to confirm on the actual Deck**, and
> re-check against the live FAQ before publishing externally. Anything path-specific is flagged
> `[unverified-device]`.

---

## 2. The single highest-value lock: PIN-gate Desktop Mode (Game-Mode Security)

**Why it matters for allow2linux:** allow2linux runs as a `systemd --user` daemon. The #1 bypass is to
leave **Game Mode** for **Desktop Mode** (full KDE + Konsole terminal as the `deck` user) and then
`systemctl --user stop/disable/mask` the daemon, `kill` it, or delete its unit (the user owns their own
session services, per `docs/steamdeck-state-and-completeness.md` C.3 #2). **Game Mode has no terminal**, so if
the child cannot reach Desktop Mode, that whole class of "just kill the daemon" attacks becomes much harder.

### Steps (confirm labels on your Deck; `[unverified-device]` on exact path)

1. On the Deck in **Game Mode**, open **Settings**. [S2, S5]
2. Go to the **Security** section. [S2, S5] `[unverified-device]` (some guides reach these toggles via the
   Steam Families / parental-controls flow rather than a top-level "Security" page; the revamp moved menus).
3. Turn **ON** the toggle **"When switching to desktop mode."** This forces a **PIN prompt** before Game
   Mode → Desktop Mode. [S2, S5]
4. Also turn **ON** **"Before showing login screen"** to require the PIN before **account switching** (stops
   swapping to a different, unmanaged Steam account). [S5]
5. **Set / confirm the PIN** so the toggles are locked in the ON position. Under Steam Families, unlocking a
   gated feature requires **requesting access (e.g. 1 hour) from the managing adult** rather than a purely
   local PIN. [S2, S5] `[unverified-device]` (request-access-for-1-hour flow; confirm current wording).

> One secondary guide states **Desktop Mode is locked by default for child accounts under Steam Families**.
> Do **not** assume this; **verify it is actually ON** in your child's parental controls. [S1, S2]
> `[unverified-device]`

**What this genuinely buys you:** with Desktop Mode PIN-gated and no terminal in Game Mode, a child can no
longer trivially open a shell to stop/mask the allow2linux service or edit its config. This is the biggest
single hardening win available from Steam's own settings.

**Critical caveat (non-negotiable to state):** this holds **only if the child does not have the Steam
account password or the family email.** Either one can **reset the PIN / parental lock**, which re-opens
Desktop Mode. Keep both away from the child. [S1, S5, S8]

---

## 3. Layer Steam Families playtime limits *on top of* allow2linux

These do **not** harden allow2linux, but they add a **second, independent** enforcement layer for Steam
games specifically, so even if allow2linux is defeated, Steam's own limits still bite for Steam content.

### Steps

1. On the **parent's** Steam account: **Steam → Settings → Family**. [S1, S3]
2. **Create a Steam Family** (name it), then **add the child** (the child must first be a **Steam Friend**
   of the parent). [S1, S3]
3. **Manage your Steam Family →** open the child **→** enable **"Enable parental controls for this user."**
   [S1, S3] (May require a Steam Mobile Authenticator code.)
4. **Set playtime limits:** toggle **playtime limits** on, then set a per-day **Time Limit** and/or
   **scheduled access windows** (e.g. allowed only 3pm–8pm). When the child hits the limit or is outside the
   window, Steam shows a "you can't play right now" screen. [S1, S3]
5. Optionally restrict the **game allow-list**, **purchases**, and **community/chat**. [S1, S3]

**Coverage limit (state plainly):** Steam Families is **Steam-only**. It does **not** cover desktop apps,
web browsers, emulators, or non-Steam launchers, and it is **per-account, not per-device**: a different,
new, or offline Steam account sidesteps it. This is exactly the gap allow2linux fills (per-device,
per-child, all activities, cross-device pooled quota). [S1, S4, S8]

---

## 4. Set a sudo / desktop password (Desktop Mode hardening)

On a fresh Deck the **sudo password is unset**, so anyone in Desktop Mode has free `sudo`. Setting one
raises the bar for tampering **inside** Desktop Mode (should the child get there).

1. Enter **Desktop Mode** once (as the parent). [S9]
2. Open **Konsole**, run `passwd`, set a password only the parent knows. [S9]

**Ceiling (state plainly):** a sudo password is **not** a hard root of trust on a stock Deck. It is
**resettable by anyone with hands-on physical access**, as on any consumer handheld, so treat it as a
speed-bump, not a wall. We deliberately do not publish the exact reset procedure here.
[S10, S11] (`docs/steamdeck-state-and-completeness.md` C.3 #3)

---

## 5. Bypass-vector coverage matrix

How each allow2linux bypass is affected by the Steam / Deck hardening above. Legend: **Blocks** = hardening
meaningfully prevents it; **Raises bar** = harder but defeatable; **Detected off-device** = no on-device
setting stops it, but the device goes silent and Allow2 alerts the parent (see §6). We describe the residual
class in the abstract rather than as a step-by-step recipe.

| # | Bypass vector (how a child defeats allow2linux) | Steam Families / Family View / Deck hardening | Result | Confidence |
|---|---|---|---|---|
| 1 | Switch to **Desktop Mode**, then kill / `systemctl --user disable`/`mask` the daemon | **"When switching to desktop mode" PIN lock** (§2); Game Mode has no terminal | **Blocks** (if PIN set & child lacks password/email) | HIGH toggle exists; path `[unverified-device]` |
| 2 | **Switch to a different / new Steam account** to escape per-account limits | **"Before showing login screen" PIN lock** on account switching (§2) | **Raises bar** (Steam accounts); a *new* account still isn't managed | MED–HIGH |
| 2b | Add a **new Linux user** to get an unmapped session | Requires Desktop Mode → gated by §2 PIN. allow2linux itself also triggers the child selector for **any unmapped OS account** (no free pass), per `docs/PROJECT_BRIEF.md` "Critical rule" | **Raises bar** | MED |
| 3 | Enable **Developer Mode** | Dev Mode is a Steam setting; **may** be gated if Settings are PIN-locked, and Dev Mode grants little beyond Desktop Mode, which §2 already gates | **Raises bar (indirect)** | LOW; `[unverified-device]` whether Family View gates the Dev Mode toggle specifically |
| 4 | **Wipe or replace the operating system entirely** (past any on-device control) | Nothing on the device can stop this; it is the honest ceiling on a **stock** handheld | **Detected off-device**: the Deck stops checking in, Allow2 alerts the parent and reconciles the overage (§6) | HIGH |

**Bottom line of the matrix:** Steam's controls **shut down the easy, no-tools, no-reboot escapes**
(vectors 1, 2, 2b), which is where most kids actually go. The residual ceiling on a **stock** Deck is
someone wiping or replacing the OS outright; no on-device app survives that. Rather than document how, we
lean on the off-device backstop: the device goes dark and Allow2 tells the parent. This matches Allow2's
tamper model exactly: **on-device agent = deterrent; durable enforcement lives off-device, detect-and-report
plus reconcile.** [`docs/steamdeck-state-and-completeness.md` C.3 "Ceiling", D.4;
`allow2/ENFORCEMENT-ARCHITECTURE.md` §1.5]

---

## 6. The backstop: why bypassing still has a consequence

Even against the residual OS-wipe class that no on-device setting can stop, bypassing is **not
consequence-free**, because **Allow2 is the authority and it lives off the device**:

- Quota is **pooled per child, per activity, across every device** (Deck, console, phone) via the Allow2
  cloud, not a per-device timer. [`~/.claude` memory: offline-first & deficit intent]
- Every device **checks in** with Allow2. When the Deck stops checking in (killed daemon, wiped OS), Allow2
  sees the **missing check-ins** and is designed to **alert the parent that the device went dark**
  ("haven't heard from this device in X days"), so tampering surfaces rather than hides.
  [`allow2/ENFORCEMENT-ARCHITECTURE.md` §1.5 detect-and-report]
- Time gamed while bypassed is **still owed against the shared pool**: the platform is designed to
  **reconcile the overage (deficit / clawback)** so it comes off the child's *next* allowance, on the Deck or
  any other device. Tamper yields borrowed time the platform claws back, not free time.
  [`~/.claude` memory: server quota & deficit findings]

> **Implementation honesty (do not overstate to parents):** in the current allow2linux alpha, `logUsage`
> reconciliation and go-dark tamper-notify are **audited as not-yet-wired**
> (`docs/steamdeck-state-and-completeness.md` B "logUsage ❌ not wired", "Tamper detection ❌ parked").
> The **deficit/reconciliation capability exists server-side** but needs wiring, per the memory
> "deficit subsystem EXISTS but is dead code (wire up, don't rebuild)." The page therefore describes the
> **platform design/authority model**; it should not claim instantaneous clawback is live today. Frame as
> "Allow2 is designed to detect, alert, and reconcile", not "already clawed back within seconds."

---

## 7. Recommended parent setup order (summary)

1. **PIN-gate Desktop Mode** (§2): biggest single win.
2. **Keep the child off the Steam password and the family email** (§2 caveat), or #1 is trivially reset.
3. **Set a sudo/desktop password** (§4): speed-bump inside Desktop Mode.
4. **Add Steam Families playtime limits** (§3): independent second layer for Steam content.
5. **Add off-device controls** (router/DNS time + content): the **only** durable, tamper-resistant layer,
   because it can't be disabled on the device. [`docs/steamdeck-state-and-completeness.md` C.2, D.4]
6. Understand the **backstop** (§6): even a full OS wipe leaves the device dark, which Allow2 detects and
   reports, and the overage still costs the child via the pooled quota.

---

## 8. Confidence & `[unverified-device]` register

| Claim | Confidence | Flag |
|---|---|---|
| Steam Families replaced Family View; per-child playtime/schedule/allow-list | HIGH | none |
| A Game-Mode Security toggle "When switching to desktop mode" PIN-gates Desktop Mode | HIGH (that it exists) | exact menu path `[unverified-device]` |
| "Before showing login screen" toggle gates account switching | MED–HIGH | path `[unverified-device]` |
| Desktop Mode is **locked by default** for child accounts | MEDIUM (one source) | `[unverified-device]`; verify on Deck |
| Unlock = request 1-hour access from managing adult | MEDIUM | `[unverified-device]`; confirm wording |
| Steam Families is Steam-only, per-account not per-device; PIN resettable via password/email | HIGH | none |
| sudo unset by default; resettable with physical access | HIGH | none |
| OS wipe / replace uncovered by Steam on a stock Deck (the residual ceiling) | HIGH | none |
| Whether Family View gates the **Developer Mode** toggle specifically | LOW | `[unverified-device]` |
| Allow2 pooled-quota + deficit reconciliation **capability** exists off-device | HIGH (design) | but `logUsage`/go-dark-notify **not yet wired** in alpha; don't claim live clawback |

---

## Sources

- **S1** Steam Families & Parental Controls, Complete Setup Guide (The Rundown Today):
  https://www.therundown.today/guides/steam-families-parental-controls-complete-setup-guide
- **S2** Parental Controls Steam Deck (Impulsec):
  https://impulsec.com/parental-control-software/parental-controls-steam-deck/
- **S3** Steam parental controls (Internet Matters):
  https://www.internetmatters.org/parental-controls/gaming-consoles/steam/
- **S4** Steam Families FAQ (Valve; **returned nav chrome only to fetch; corroborated via S1/S2/S3**):
  https://help.steampowered.com/en/faqs/view/054C-3167-DD7F-49D4
- **S5** Steam Deck Parental Controls Guide (Truple):
  https://stories.truple.io/posts/1f33294e-b756-4e26-b39b-016a7e157b9b/1749176527323
- **S6** Steam Support :: Family View (Valve; **nav chrome only to fetch**):
  https://help.steampowered.com/en/faqs/view/6B1A-66BE-E911-3D98
- **S7** "Please add a proper PIN input prompt for unlocking Family View on Deck" (Steam Community):
  https://steamcommunity.com/app/1675200/discussions/0/3824160148612342475/
- **S8** Steam Deck Blocking Browsers & Parental Controls (Steam Community):
  https://steamcommunity.com/app/1675200/discussions/0/597402866249958367/
- **S9** Set/change/reset SteamOS sudo password (GamingOnLinux):
  https://www.gamingonlinux.com/guides/view/how-to-set-change-and-reset-your-steamos-steam-deck-desktop-sudo-password/
- **S10** Reset sudo via TTY / Boot Manager (Techbloat):
  https://www.techbloat.com/how-to-reset-sudo-password-on-steam-deck-2-easy-methods.html
- **S11** Boot Manager / recovery (Steam Support):
  https://help.steampowered.com/en/faqs/view/1B71-EDF2-EB6D-2BB3

Additional grounding is inline via `docs/steamdeck-state-and-completeness.md` Part C (which carries its own
Sources 1–34, including the SteamOS immutable-rootfs, gamescope, recovery-reimage, and kiosk-lockdown
citations this page relies on for the tamper ceiling).

*Source notes for the allow2linux hardening page (generated by `flatpak/gen-hardening-page.sh`,
published per channel via `scripts/publish.sh`). Exact Deck menu paths flagged
`[unverified-device]` pending on-device confirmation.*
