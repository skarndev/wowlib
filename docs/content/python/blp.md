# BLP format

Blizzard's texture format ([wowdev.wiki/BLP](https://wowdev.wiki/BLP)). BLP2 is
version-stable — every client release from vanilla through The War Within reads
the same layout — so unlike the chunked formats there is one unversioned `BLP`
class and no `for_version` factory.

A file is a fixed header + 256-entry palette followed by up to 16 mipmap
payloads. `read()`/`write()` move the file whole and round-trip byte-perfectly
while the payloads are unmodified (unusual layouts — inter-mip gaps, trailing
bytes, shared placement — are recorded and replayed). `decode(level)` produces
an RGBA8 [`Image`][wowlib.formats.blp.Image] from any stored level — palettized,
DXT1/3/5, BC5 and raw BGRA all decode. `encode(image, settings)` rebuilds the
whole texture from one image: palette quantization (median cut), DXT compression
and box-filtered mip generation.

```python
import numpy as np
import wowlib
from wowlib.formats import blp

with wowlib.fs.FileSystem.open(settings) as fs:
    texture = blp.BLP()
    texture.read(fs, wowlib.FileKey("Interface/Icons/INV_Misc_QuestionMark.blp"))

    image = texture.decode()                    # level 0, RGBA8
    pixels = np.asarray(image.pixels).reshape(image.height, image.width, 4)

    pixels[..., 3] = 255                        # edit in place (zero-copy view)
    edited = blp.Image()
    edited.width, edited.height = image.width, image.height
    edited.pixels.extend(pixels.reshape(-1).tolist())
    texture.encode(edited, blp.EncodeSettings(format=blp.PixelFormat.Dxt5))
    texture.write(fs, wowlib.FileKey("Interface/Icons/INV_Misc_QuestionMark.blp"))
```

::: wowlib.formats.blp
    options:
      show_root_heading: false
      show_root_toc_entry: false
      heading_level: 2
