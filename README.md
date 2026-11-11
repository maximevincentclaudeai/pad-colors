# Pad Colors

An AZ-MODS mod for the XDJ-AZ. Pick the color of the 8 performance pads and of the 4
pad-mode buttons in **Mod Menu > Community Mods > Pad Colors**: red, orange, yellow, green,
blue, violet or white (or "Defaut" for the deck's own colors).

The deck's dark, dimmed and animated pads keep their brightness and rhythm; only the hue is
replaced. The lime the deck shows on a secondary pad page is left alone.

Core services only (no hooks), so it loads on every firmware. It rewrites the pad LED frame
(`AZ_BUS_ERP_LED`) with `frame_filter`.

## Build

```sh
<sdk>/tools/azmod-pack build .
<sdk>/tools/azmod-pack package . --github maximevincentclaudeai/pad-colors
make test AZ_SDK=<sdk>
```
