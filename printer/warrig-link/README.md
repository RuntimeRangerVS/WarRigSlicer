# WarRig Link – slicer side

WarRig Slicer speaks the WarRig screen's link contract (`WARRIG_LINK.txt` in the WarRig build, api 2).
Everything goes through the printer's Moonraker database, namespace `warrig`. Nothing extra runs on the printer.

What the slicer uses:

| Record | How |
| --- | --- |
| `warrig.info` | read: WarRig found (unlocks the WarRig options), build, firmware, macros, boxes |
| `warrig.box` | read by Sync BOX: slots, colours, materials, what is loaded |
| `warrig.events` | read: shown under "Latest on the printer" in the WarRig Link window |
| `warrig.next_print` | written when a print is sent: mesh, Z tilt, reload; waits for `next_print_ack` |
| `warrig.clients.slicer` | heartbeat every minute: `{t, ver, api: 2}` |

## Linking

On the printer screen open **Wi-Fi > Remote access & phone alerts** (or option 6 in the WarRig PC tool).
Scan the QR code with a phone and paste the link into **Device tab > WarRig Link**:

```
warrig://connect?host=<printer LAN IP>&ts=<tailscale name>&topic=...
```

The slicer uses `host` at home and the Tailscale name (`ts`) away from home. Only Tailscale names and
addresses or `https://` addresses are accepted as away addresses, so the printer is never opened to the internet.
The link is checked every minute: home first, then away.
