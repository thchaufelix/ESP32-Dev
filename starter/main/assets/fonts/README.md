# Fonts

## GenWanMin TW (this branch)

- Source: [ButTaiwan/genwan-font](https://github.com/ButTaiwan/genwan-font) `GenWanMin-R.ttc` (TW Regular face)
- License: SIL OFL 1.1 — see `GENWAN_OFL.txt`
- Built: same ~158-glyph charset as the Noto trial (all LRT Chinese stops + UI) → `font_genwan_tc_16.c` (16px, bpp 4)
- Latin / digits: built-in **Montserrat** (fallback from GenWan)
- LRT ETA minutes: **English** (`time_en`) on Montserrat; destinations: Chinese (`dest_ch`) on GenWan

### Regenerate

```powershell
cd starter/main/assets/fonts/_work
# extract TW face from TTC, then:
python -m fontTools.subset GenWanMin-R-face0.ttf --text-file=charset.txt --output-file=GenWanMin-R-subset.ttf --no-hinting --desubroutinize
npx --yes lv_font_conv --font GenWanMin-R-subset.ttf -r (Get-Content ranges.txt -Raw) --size 16 --bpp 4 --format lvgl --lv-include lvgl.h --no-compress -o ../font_genwan_tc_16.c
```

`_work/` holds raw TTC/OTF and is gitignored.

## Noto Serif TC (previous trial)

`font_noto_serif_tc_16.c` may still be present for size comparison; not compiled on `feature/genwan-font`.
