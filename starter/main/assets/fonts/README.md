# Fonts

## Noto Serif TC (Traditional Chinese)

- Source: [Noto Serif CJK TC Regular](https://github.com/notofonts/noto-cjk) (`NotoSerifCJKtc-Regular.otf`)
- License: SIL Open Font License 1.1 — see `OFL.txt`
- Built: subset of ~158 glyphs (all MTR Light Rail Chinese stop names + UI labels) → `font_noto_serif_tc_16.c` (16px, bpp 4)
- Latin / digits stay on built-in **Montserrat** (fallback from Noto)

### Regenerate

```powershell
cd starter/main/assets/fonts/_work
# charset.txt already lists codepoints
python -m fontTools.subset NotoSerifTC-Regular.otf --text-file=charset.txt --output-file=NotoSerifTC-Regular-subset.ttf --no-hinting --desubroutinize
npx --yes lv_font_conv --font NotoSerifTC-Regular-subset.ttf -r (Get-Content ranges.txt -Raw) --size 16 --bpp 4 --format lvgl --lv-include lvgl.h --no-compress -o ../font_noto_serif_tc_16.c
```

`_work/` holds the raw OTF (~23 MB) and is gitignored.
