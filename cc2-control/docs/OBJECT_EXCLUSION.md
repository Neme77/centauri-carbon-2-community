# Object exclusion map

During a supported print, the Job page shows one top-down 256 × 256 mm bed map.
The front of the printer is at the bottom (Y = 0). Numbered markers follow the
same order as the compact name list. G-code outlines are drawn when available;
otherwise the map uses the reported center without inventing a shape.

Select a marker or a name to see its full label and coordinates. Cyan marks the
selection, amber identifies the object currently printing, and excluded objects
are marked in red and crossed out. The printing indicator never changes your
manual selection. Objects without a usable position remain accessible in the
list; no marker is placed at a fabricated location.

**Expand bed map** enlarges the same view. Close it or press Escape to return to
the inline map. Selecting an object sends no command. **Exclude Selected Object**
still requires confirmation and uses the original G-code identifier, not its
display label or marker number. Exclusion cannot be undone for that print.

The map reuses the existing object data and polling. Drawing runs in the browser,
with no additional printer requests or continuously animated effects.

Thanks to **DamiBFryta** for suggesting a single clearer view, and to
**Nick Plateringen-Paul** for the original bed-map prototype.
