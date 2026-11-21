#!/bin/bash
# Builds a test folder of synthetic pictures for picsel.
#   make_testset.sh OUTDIR [COUNT] [VIDEOS]
# Creates ~24 unique large JPEGs (DSLR 3:2, phone 16:9, portrait, EXIF-rotated)
# in OUTDIR/.unique and COUNT hard links named like camera files in
# OUTDIR/pending-selection, so decode timing is realistic without using GBs.
# With VIDEOS > 0 it also creates that many short H.264/AAC clips (2-6 s,
# 1920x1080 landscape or phone-style portrait stored rotated, with
# creation_time) named PXL_20261121_VID_NNNN.mp4 / DSC_VNNNN.MOV.
set -euo pipefail
out=${1:?usage: make_testset.sh OUTDIR [COUNT] [VIDEOS]}
count=${2:-60}
videos=${3:-0}
uniq="$out/.unique"
dest="$out/pending-selection"
mkdir -p "$uniq" "$dest"

here=$(dirname "$(readlink -f "$0")")

# name WxH(upright) label exif-orientation date camera-make camera-model
# Pixels are stored rotated so that only a viewer honouring the EXIF
# orientation shows the label upright.
make_one() {
    local f="$uniq/$1.jpg" rot=0
    [[ -f $f ]] && return
    case $4 in 3) rot=180 ;; 6) rot=-90 ;; 8) rot=90 ;; esac
    magick -size 600x400 plasma:fractal -blur 0x2 -resize "$2!" \
        -gravity center -pointsize $(( ${2%x*} / 8 )) -fill white -stroke black -strokewidth 6 \
        -annotate 0 "$3" -rotate "$rot" -quality 90 "$f"
    "$here/set_exif.py" --orientation "$4" --date "$5" --make "$6" --model "$7" "$f"
}

i=0
for spec in "6000x4000 DSLR 1" "6000x4000 DSLR 1" "6000x4000 DSLR 1" \
            "4000x6000 PORTRAIT 1" "4000x6000 ROT90 6" "4000x6000 ROT270 8" \
            "4000x2250 PHONE 1" "4000x2250 PHONE 1" "2250x4000 PHONEV 1" \
            "4000x3000 4:3 1" "6000x4000 ROT180 3" "6000x4000 DSLR 1"; do
    read -r size label orient <<<"$spec"
    if [[ $label == PHONE* || $label == 4:3 ]]; then make=Google; model="Pixel 9"; else make=NIKON; model="Z 6"; fi
    date=$(printf "2026:11:21 18:%02d:%02d" $(( i * 3 / 60 )) $(( (i * 37) % 60 )))
    make_one "u$i" "$size" "$label $i" "$orient" "$date" "$make" "$model" &
    i=$((i + 1))
done
wait

uniques=("$uniq"/*.jpg)
n=${#uniques[@]}
for ((k = 1; k <= count; k++)); do
    src=${uniques[$(( (k * 7) % n ))]}
    if (( k % 3 == 0 )); then name=$(printf "PXL_20261121_%06d.jpg" "$k"); else name=$(printf "DSC_%04d.JPG" "$k"); fi
    ln -f "$src" "$dest/$name"
done
echo "$count pictures in $dest"

# name seconds portrait(0/1) creation-time(UTC) source tone-Hz
make_video() {
    local f="$dest/$1" rot=()
    [[ -f $f ]] && return
    # Phones store portrait video as landscape frames plus a display matrix
    # (90 degrees clockwise); -noautorotate keeps ffmpeg from baking it in.
    (( $3 )) && rot=(-display_rotation -90 -noautorotate)
    for codec in libx264 mpeg4; do
        local extra=()
        [[ $codec == libx264 ]] && extra=(-preset veryfast)
        ffmpeg -nostdin -v error -y "${rot[@]}" -f lavfi -i "$5=size=1920x1080:rate=30" \
            -f lavfi -i "sine=frequency=$6" -t "$2" -c:v "$codec" "${extra[@]}" -pix_fmt yuv420p \
            -c:a aac -metadata creation_time="$4" "$f" && return
    done
    echo "failed to create $f" >&2
    return 1
}

if (( videos > 0 )); then
    command -v ffmpeg >/dev/null || { echo "ffmpeg not found, no videos created" >&2; exit 1; }
    sources=(testsrc2 smptehdbars mandelbrot rgbtestsrc)
    for ((k = 1; k <= videos; k++)); do
        secs=$(( 2 + (k * 3) % 5 ))
        when=$(printf "2026-11-21T17:%02d:%02dZ" $(( (k * 7) % 60 )) $(( (k * 23) % 60 )))
        src=${sources[$(( k % ${#sources[@]} ))]}
        if (( k % 2 )); then
            make_video "$(printf "PXL_20261121_VID_%04d.mp4" "$k")" "$secs" $(( (k / 2) % 2 == 0 )) "$when" "$src" $(( 300 + k * 40 )) &
        else
            make_video "$(printf "DSC_V%04d.MOV" "$k")" "$secs" 0 "$when" "$src" $(( 300 + k * 40 )) &
        fi
        (( k % $(nproc) == 0 )) && wait
    done
    wait
    echo "$videos videos in $dest"
fi
