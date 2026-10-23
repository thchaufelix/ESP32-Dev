# Weather icons (small flash)

- Source: Iconify material-symbols (https://icon-sets.iconify.design/material-symbols/)
- Format: LVGL LV_COLOR_FORMAT_A8 @ 32x32 (~1024 B each)
- Unique assets: 19 (HA gaps: snow / snowy-rainy / hail / exceptional / partly_cloudy_night)
- Total pixel data: ~19456 bytes (+ descriptors)
- UI applies image_recolor from wx_icon_style_for_hko() / wx_icon_style_for_ha()
- Night: HA cloudy / partlycloudy use night variants when local hour <06 or >=18 (hourly uses slot HH:MM)
