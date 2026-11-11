# Pad Colors

An AZ-MODS mod for the XDJ-AZ. Pick the color of the 8 performance pads and of the 4
pad-mode buttons in **Mod Menu > Community Mods > Pad Colors**, among 14 colors.

A Mod Menu choice row holds at most 8 entries, so each target has two rows:

| Row | Entries |
|---|---|
| Palette 1 | Defaut (the deck's own colors), Rouge, Orange, Jaune, Vert, Bleu, Violet, Blanc |
| Palette 2 | `---` (use palette 1), Cyan, Magenta, Rose, Turquoise, Bleu ciel, Lavande, Ambre |

Palette 2 wins when it is set; put it back on `---` to use palette 1 again.

Your choices are saved on the USB stick (`MODS/azmods.cfg`), so they are still there at the
next power-on.

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

## Release

```sh
git tag mvcai-pad-colors-1.1.0 && git push origin mvcai-pad-colors-1.1.0
```
