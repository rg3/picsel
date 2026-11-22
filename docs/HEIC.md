# HEIC/HEIF support: analysis (not implemented)

Analysis from 2026-11-22, for when iPhone HEIC files show up. Nothing below is
implemented yet.

## What already works on Fedora 45

- **Decoding via Qt:** `kf6-kimageformats` ships `kimg_heif.so`, so
  `QImageReader` lists `heic heif hif` as readable (backed by `libheif` +
  `libheif-ffmpeg`). Non-JPEG files already go through `decodeWithQt()` in
  `src/image/JpegDecoder.cpp`, so HEIC would decode with no decoder changes.
- **Metadata:** Fedora's exiv2 0.28 is built with `enable_bmff=1`, so
  `readPhotoInfo()` (`src/core/Metadata.cpp`) should read EXIF from HEIC as-is.
- **Case:** `mediaKindForName()` lowercases the extension, so `.HEIC` (the
  iPhone default) is matched the same as `.heic`.

## Minimal change

Add `heic`, `heif`, `hif` to `mediaKindForName()` in
`src/core/DirScanner.cpp`. `DirScanner` and `DirWatcher` both go through
`isSupportedName()`, so that one change covers both.

## Things to verify or handle

- **HEVC decoder availability.** HEIC payloads are HEVC. Fedora's
  `ffmpeg-free` may not decode HEVC. This machine appears to have the full
  ffmpeg build. An undecodable HEIC must fail cleanly (placeholder), not
  break the mosaic.
- **Orientation.** libheif applies the container's `irot`/`imir` transforms
  itself. iPhone files also carry an EXIF Orientation tag. Check with a
  real, rotated iPhone photo that nothing gets rotated twice, and that
  `DecodedImage::fullSize` comes out upright.
- **Live Photos.** iPhones write `IMG_1234.HEIC` + `IMG_1234.MOV`. They show up
  as two separate items. Pairing them (selecting both together) is a possible
  later feature.
- **Test files.** Qt here can read HEIF but not write it, so the `tools/`
  generators need `heif-enc` (`sudo dnf install libheif-tools`). Real iPhone
  samples are still needed for the orientation and thumbnail checks.

## Performance: no JPEG-style fast path

JPEG thumbnails are fast because libjpeg can stop at the DCT coefficients
and output 1/2, 1/4 or 1/8 scale. HEVC can't do that: intra blocks are
predicted from neighbouring reconstructed pixels, so every pixel must be
fully decoded before anything can be scaled. No HEIF library offers scaled
decoding, and the Qt plugin doesn't honour `setScaledSize()` beyond scaling
after a full decode. Expect a full 12 MP decode per thumbnail. Measure it
before optimising.

Options, roughly in order of value:

1. **Embedded thumbnail.** HEIF files can store thumbnail items, and iPhones
   write one, probably small (around 320 px wide; to be checked on real
   files). Using libheif directly
   (`heif_image_handle_get_list_of_thumbnail_IDs()` /
   `heif_image_handle_get_thumbnail()`), take it when it's at least as large
   as the requested box, else fall back to full decode. Whether it's big
   enough depends on mosaic cell size on a 4K screen. This is the HEIF
   counterpart of the MPF-preview idea noted for JPEG in ARCHITECTURE.md.
2. **Tiled decode threads.** iPhone primary images are grids of 512×512 HEVC
   tiles. libheif can decode tiles in parallel
   (`heif_context_set_max_decoding_threads()`), but picsel already decodes
   many files in parallel, so this mainly helps the single-image full view.
3. **Hardware decode** (VAAPI via ffmpeg): possible but complex, probably not
   worth it.

Using libheif directly needs `libheif-devel` at build time and a new
`HeifDecoder` beside `JpegDecoder`, selected by the `ftyp` magic (bytes 4-11:
`ftypheic`, `ftypheix`, `ftypmif1`, ...).
