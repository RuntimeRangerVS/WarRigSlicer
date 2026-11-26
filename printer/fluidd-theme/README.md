# WarRig theme for Fluidd

Makes the printer's Fluidd page (the Device tab in WarRig Slicer, and Fluidd in any browser) look WarRig:
WR logo, steel panels with chamfered corners and orange header lines, Barlow lettering, orange accents.

Fluidd's layout stays the same; the theme only changes colours, lettering, borders and the logo.

## Install

The WarRig installer copies this folder to the printer. To do it by hand:

1. In Fluidd open **Configuration**, create a folder named `.fluidd-theme` in the config root.
2. Upload `custom.css` and `logo.svg` into it.
3. In Fluidd **Settings > General**, set the printer name to `Q2 – WarRig` (replaces "QIDI Tech").
4. In **Settings > Theme**, set the primary colour to `#FF7A12` and dark mode on.
5. Refresh the page (Ctrl+F5 if the old look is cached).

To remove it, delete the `.fluidd-theme` folder.
