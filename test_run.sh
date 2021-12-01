#!/bin/sh

echo "Trying on 8 bit and 12 bit image"
for M in NEAREST SIMPLE BILINEAR HQLINEAR DOWNSAMPLE VNG AHD ORI; do
#raw8
	echo ./bayer2rgb -i camera_1600x1200_20080101000000_x_4.00.raw -o camera_1600x1200_20080101000000_x_4.00.$M.tif -w 1600 -v 1200 -b 8 -f RGGB -m $M -t
	./bayer2rgb -i camera_1600x1200_20080101000000_x_4.00.raw -o camera_1600x1200_20080101000000_x_4.00.$M.tif -w 1600 -v 1200 -b 8 -f RGGB -m $M -t
#raw12
	echo ./bayer2rgb -i BGGR_2560x1440_raw12_unpack.raw -o BGGR_2560x1440_raw12_unpack.$M.tif -w 2560 -v 1440 -b 12 -f BGGR -m $M -t
	./bayer2rgb -i BGGR_2560x1440_raw12_unpack.raw -o BGGR_2560x1440_raw12_unpack.$M.tif -w 2560 -v 1440 -b 12 -f BGGR -m $M -t
done

