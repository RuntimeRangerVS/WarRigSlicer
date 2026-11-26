# WarRig Link – printer side

WarRig Link lets WarRig Slicer (and WarRig Companion) use a WarRig printer at home or away, with no cloud account.
The printer approves every new link itself, so only someone standing at the printer can add a device.

Everything is stored in the printer's Moonraker database, namespace `warrig_link`:

| Key | Value |
| --- | --- |
| `requests.<id>` | `{ app, device, code, created, expires, status }`, status `pending` → `approved` or `denied` |
| `devices.<id>` | `{ app, device, linked }`, one per linked PC or phone |
| `away` | the away address: `100.x.x.x`, `name.ts.net` or `https://…` |

`<id>` is 12 lowercase hex characters. `code` looks like `7K4-Q9M`. Requests expire after 5 minutes.

## Linking, step by step

1. **Slicer:** Device tab → select the printer → WarRig Link button → *Send link request*.
   It writes `requests.<id>` (status `pending`) and prints `WarRig Link: Link request from <PC> - code <code>` in the console.
2. **WarRig screen:** sees the pending request and shows:
   * a QR code for `http://<printer-ip>/warrig/link.html?r=<id>&c=<code>`
   * the code, "WarRig Slicer on <PC> wants to link", and *Deny* / *Allow here* buttons.
3. **Phone:** scanning the QR opens `link.html` (this folder) on the printer, or WarRig Companion handles the same address itself.
   Approving sets `status` to `approved`. *Allow here* on the screen does the same.
4. **Slicer:** sees `approved`, reads `away`, takes the printer's API key, writes `devices.<id>` and removes the request.

Removing a device on the screen or in the app (delete `devices.<id>`) unlinks it.

## Install

Copy `link.html` to the folder Fluidd is served from, under `warrig/`:

```
~/fluidd/warrig/link.html
```

so it opens at `http://<printer-ip>/warrig/link.html`. The WarRig installer does this as part of the firmware mod.

## Away address

Only these are accepted, so the printer is never opened to the internet:

* Tailscale: `100.64.0.0/10` addresses or `*.ts.net` names (plain http is fine, Tailscale encrypts it)
* any `https://` address (for example a Cloudflare Tunnel)

Through Tailscale everything works away from home: live status, BOX, camera and printing.
Through an https tunnel, printing and uploads work; live status needs the websocket to be tunnelled too.
