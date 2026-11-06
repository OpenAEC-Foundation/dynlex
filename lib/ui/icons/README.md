SVG sources from Lucide 0.468.0: https://github.com/lucide-icons/lucide/tree/0.468.0/icons

Edit the SVG files and run `python3 scripts/generate_ui_icons.py`. The generated DynLex data retains cubic curves; FreeType rasterizes these outlines at the displayed pixel size. No bitmap sizes are baked in. The importer accepts the stroked SVG shapes used by this catalog and rejects unsupported attributes.
