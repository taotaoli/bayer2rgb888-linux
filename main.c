/**
 * bayer2rgb: Comandline converter for bayer grid to rgb images.
 * This file is part of bayer2rgb.
 *
 * Copyright (c) 2009 Jeff Thomas
 *
 * bayer2rgb is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * bayer2rgb is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 **/

#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "bayer.h"

#define DEBUG 0
// tiff types: short = 3, int = 4
// Tags: ( 2-byte tag ) ( 2-byte type ) ( 4-byte count ) ( 4-byte data )
//    0100 0003 0000 0001 0064 0000
//       |        |    |         |
// tag --+        |    |         |
// short int -----+    |         |
// one value ----------+         |
// value of 100 -----------------+
//
#define TIFF_HDR_NUM_ENTRY 8
#define TIFF_HDR_SIZE 10+TIFF_HDR_NUM_ENTRY*12 
uint8_t tiff_header[TIFF_HDR_SIZE] = {
	// I     I     42    
	  0x49, 0x49, 0x2a, 0x00,
	// ( offset to tags, 0 )  
	  0x08, 0x00, 0x00, 0x00, 
	// ( num tags )  
	  0x08, 0x00, 
	// ( newsubfiletype, 0 full-image )
	  0xfe, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// ( image width )
	  0x00, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// ( image height )
	  0x01, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	// ( bits per sample )
	  0x02, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	// ( Photometric Interpretation, 2 = RGB )
	  0x06, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 
	// ( Strip offsets, 8 )
	  0x11, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 
	// ( samples per pixel, 3 - RGB)
	  0x15, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
	// ( Strip byte count )
	  0x17, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
};

uint8_t * put_tiff(uint8_t * rgb, uint32_t width, uint32_t height, uint16_t bpp)
{
	uint32_t ulTemp=0;
	uint16_t sTemp=0;
	memcpy(rgb, tiff_header, TIFF_HDR_SIZE);

	sTemp = TIFF_HDR_NUM_ENTRY;
	memcpy(rgb + 8, &sTemp, 2);

	memcpy(rgb + 10 + 1*12 + 8, &width, 4);
	memcpy(rgb + 10 + 2*12 + 8, &height, 4);
	memcpy(rgb + 10 + 3*12 + 8, &bpp, 2);

	// strip byte count
	ulTemp = width * height * (bpp / 8) * 3;
	memcpy(rgb + 10 + 7*12 + 8, &ulTemp, 4);

	//strip offset
	sTemp = TIFF_HDR_SIZE;
	memcpy(rgb + 10 + 5*12 + 8, &sTemp, 2);

	return rgb + TIFF_HDR_SIZE;
};


dc1394bayer_method_t
getMethod(char* m)
{
	if( strcmp(m, "NEAREST") == 0 )
		return DC1394_BAYER_METHOD_NEAREST;
	if( strcmp(m, "SIMPLE") == 0 )
		return DC1394_BAYER_METHOD_SIMPLE;
	if( strcmp(m, "BILINEAR") == 0 )
		return DC1394_BAYER_METHOD_BILINEAR;
	if( strcmp(m, "HQLINEAR") == 0 )
		return DC1394_BAYER_METHOD_HQLINEAR;
	if( strcmp(m, "DOWNSAMPLE") == 0 )
		return DC1394_BAYER_METHOD_DOWNSAMPLE;
	if( strcmp(m, "EDGESENSE") == 0 )
		return DC1394_BAYER_METHOD_EDGESENSE;
	if( strcmp(m, "VNG") == 0 )
		return DC1394_BAYER_METHOD_VNG;
	if( strcmp(m, "AHD") == 0 )
		return DC1394_BAYER_METHOD_AHD;
	if( strcmp(m, "ORI") == 0 )
		return DC1394_BAYER_METHOD_ORI;

	printf("WARNING: Unrecognized method \"%s\", defaulting to BILINEAR\n", m);
	return DC1394_BAYER_METHOD_BILINEAR;
}


dc1394color_filter_t
getFirstColor(char *f)
{
	if( strcmp(f, "RGGB") == 0 )
		return DC1394_COLOR_FILTER_RGGB;
	if( strcmp(f, "GBRG") == 0 )
		return DC1394_COLOR_FILTER_GBRG;
	if( strcmp(f, "GRBG") == 0 )
		return DC1394_COLOR_FILTER_GRBG;
	if( strcmp(f, "BGGR") == 0 )
		return DC1394_COLOR_FILTER_BGGR;

	printf("WARNING: Unrecognized first color \"%s\", defaulting to RGGB\n", f);
	return DC1394_COLOR_FILTER_RGGB;
}

void
usage( char * name )
{
	printf("usage: %s\n", name);
	printf("   --input,-i     input file\n");
	printf("   --output,-o    output file\n");
	printf("   --width,-w     image width (pixels)\n");
	printf("   --height,-v    image height (pixels)\n");
	printf("   --bpp,-b       bits per pixel\n");
	printf("   --first,-f     first pixel color: RGGB, GBRG, GRBG, BGGR\n");
	printf("   --method,-m    interpolation method: NEAREST, SIMPLE, BILINEAR, HQLINEAR, DOWNSAMPLE, EDGESENSE, VNG, AHD, ORI\n");
	printf("   --tiff,-t      add a tiff header\n");
	printf("   --swap,-s      if bpp == 16, swap byte order before conversion\n");
	printf("   --help,-h      this helpful message\n");
}

void fwrite_spilt_channel_file_8bit(char * outfile, const uint8_t *bayer, int width, int height, int first_color, int bpp)
{
    char outfile_ch1[256]="", outfile_ch2[256]="", outfile_ch3[256]="", outfile_ch4[256]="";
    int output_ch1_fd = 0, output_ch2_fd = 0, output_ch3_fd = 0, output_ch4_fd = 0;
    uint32_t out_ch_size=0;
    uint8_t *rgb_ch1 = NULL, *rgb_start_ch1 = NULL;
    uint8_t *rgb_ch2 = NULL, *rgb_start_ch2 = NULL;
    uint8_t *rgb_ch3 = NULL, *rgb_start_ch3 = NULL;
    uint8_t *rgb_ch4 = NULL, *rgb_start_ch4 = NULL;
    int i, j, k = 0;
    int fret = 0;

    out_ch_size = width / 2 * height / 2 * (bpp / 8) * 3 + TIFF_HDR_SIZE;

    strcat(outfile_ch1,outfile);
    strcat(outfile_ch1,"_ch1.tif");
    output_ch1_fd = open(outfile_ch1, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
    if(output_ch1_fd < 0)
    {
        printf("Problem opening output: %s\n", outfile_ch1);
        return;
    }

    fret = ftruncate(output_ch1_fd, out_ch_size );
    if( fret != 0 )
    {
        perror("Faild ftruncate");
        return;
    }
    rgb_start_ch1 = rgb_ch1 = mmap(NULL, out_ch_size, PROT_READ | PROT_WRITE, MAP_SHARED /*| MAP_POPULATE*/, output_ch1_fd, 0);
    if( rgb_ch1 == MAP_FAILED )
    {
        perror("Faild mmaping output");
        return;
    }

    rgb_start_ch1 = put_tiff(rgb_ch1, width / 2, height / 2, bpp);

    for (i = 0; i < height; i+=2) {
        for (j = 0; j < width; j+=2) {
            rgb_start_ch1[0 + 3*k] = rgb_start_ch1[1 + 3*k] = rgb_start_ch1[2 + 3*k] = bayer[i * width + j];
            k++;
        }
        //printf("rgb_start_ch1[%d] = 0x%x, bayer[%d,%d] = 0x%x \n",3*k,rgb_start_ch1[0 + 3*k],i,j,bayer[i * width + j]);
    }
    if( msync(rgb_ch1, out_ch_size, MS_INVALIDATE|MS_SYNC) != 0 )
		perror("Problem msyncing");
    munmap(rgb_ch1,out_ch_size);
    if( fsync(output_ch1_fd) != 0 )
		perror("Problem fsyncing");
    close(output_ch1_fd);

    strcat(outfile_ch2,outfile);
    strcat(outfile_ch2,"_ch2.tif");
    output_ch2_fd = open(outfile_ch2, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
    if(output_ch2_fd < 0)
    {
        printf("Problem opening output: %s\n", outfile_ch2);
        return;
    }
    fret = ftruncate(output_ch2_fd, out_ch_size );
    if( fret != 0 )
    {
        perror("Faild ftruncate");
        return;
    }
    rgb_start_ch2 = rgb_ch2 = mmap(NULL, out_ch_size, PROT_READ | PROT_WRITE, MAP_SHARED /*| MAP_POPULATE*/, output_ch2_fd, 0);
    if( rgb_ch2 == MAP_FAILED )
    {
        perror("Faild mmaping output");
        return;
    }

    rgb_start_ch2 = put_tiff(rgb_ch2, width / 2, height / 2, bpp);

    k = 0;
    for (i = 0; i < height; i+=2) {
        for (j = 1; j < width; j+=2) {
            rgb_start_ch2[0 + 3*k] = rgb_start_ch2[1 + 3*k] = rgb_start_ch2[2 + 3*k] = bayer[i * width + j];
            k++;
        }
    }
    if( msync(rgb_ch2, out_ch_size, MS_INVALIDATE|MS_SYNC) != 0 )
		perror("Problem msyncing");
    munmap(rgb_ch2,out_ch_size);
    if( fsync(output_ch2_fd) != 0 )
		perror("Problem fsyncing");
    close(output_ch2_fd);

    strcat(outfile_ch3,outfile);
    strcat(outfile_ch3,"_ch3.tif");
    output_ch3_fd = open(outfile_ch3, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
    if(output_ch3_fd < 0)
    {
        printf("Problem opening output: %s\n", outfile_ch3);
        return;
    }
    fret = ftruncate(output_ch3_fd, out_ch_size );
    if( fret != 0 )
    {
        perror("Faild ftruncate");
        return;
    }
    rgb_start_ch3 = rgb_ch3 = mmap(NULL, out_ch_size, PROT_READ | PROT_WRITE, MAP_SHARED /*| MAP_POPULATE*/, output_ch3_fd, 0);
    if( rgb_ch3 == MAP_FAILED )
    {
        perror("Faild mmaping output");
        return;
    }

    rgb_start_ch3 = put_tiff(rgb_ch3, width / 2, height / 2, bpp);

    k = 0;
    for (i = 1; i < height; i+=2) {
        for (j = 0; j < width; j+=2) {
            rgb_start_ch3[0 + 3*k] = rgb_start_ch3[1 + 3*k] = rgb_start_ch3[2 + 3*k] = bayer[i * width + j];
            k++;
        }
    }
    if( msync(rgb_ch3, out_ch_size, MS_INVALIDATE|MS_SYNC) != 0 )
		perror("Problem msyncing");
    munmap(rgb_ch3,out_ch_size);
    if( fsync(output_ch3_fd) != 0 )
		perror("Problem fsyncing");
    close(output_ch3_fd);

    strcat(outfile_ch4,outfile);
    strcat(outfile_ch4,"_ch4.tif");
    output_ch4_fd = open(outfile_ch4, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
    if(output_ch4_fd < 0)
    {
        printf("Problem opening output: %s\n", outfile_ch4);
        return;
    }
    fret = ftruncate(output_ch4_fd, out_ch_size );
    if( fret != 0 )
    {
        perror("Faild ftruncate");
        return;
    }
    rgb_start_ch4 = rgb_ch4 = mmap(NULL, out_ch_size, PROT_READ | PROT_WRITE, MAP_SHARED /*| MAP_POPULATE*/, output_ch4_fd, 0);
    if( rgb_ch4 == MAP_FAILED )
    {
        perror("Faild mmaping output");
        return;
    }

    rgb_start_ch4 = put_tiff(rgb_ch4, width / 2, height / 2, bpp);

    k = 0;
    for (i = 1; i < height; i+=2) {
        for (j = 1; j < width; j+=2) {
            rgb_start_ch4[0 + 3*k] = rgb_start_ch4[1 + 3*k] = rgb_start_ch4[2 + 3*k] = bayer[i * width + j];
            k++;
        }
    }
    if( msync(rgb_ch4, out_ch_size, MS_INVALIDATE|MS_SYNC) != 0 )
		perror("Problem msyncing");
    munmap(rgb_ch4,out_ch_size);
    if( fsync(output_ch4_fd) != 0 )
		perror("Problem fsyncing");
    close(output_ch4_fd);

    return;
}

void fwrite_spilt_channel_file_16bit(char * outfile, const uint16_t *bayer, int width, int height, int first_color, int bpp)
{
	char outfile_ch1[256]="", outfile_ch2[256]="", outfile_ch3[256]="", outfile_ch4[256]="";
	int output_ch1_fd = 0, output_ch2_fd = 0, output_ch3_fd = 0, output_ch4_fd = 0;
	uint32_t out_ch_size=0;
	uint16_t *rgb_ch1 = NULL, *rgb_start_ch1 = NULL;
	uint16_t *rgb_ch2 = NULL, *rgb_start_ch2 = NULL;
	uint16_t *rgb_ch3 = NULL, *rgb_start_ch3 = NULL;
	uint16_t *rgb_ch4 = NULL, *rgb_start_ch4 = NULL;
	int i, j, k = 0;
	int fret = 0;

    out_ch_size = width / 2 * height / 2 * (bpp / 8) * 3 + TIFF_HDR_SIZE;

    strcat(outfile_ch1,outfile);
    strcat(outfile_ch1,"_ch1.tif");
    output_ch1_fd = open(outfile_ch1, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
    if(output_ch1_fd < 0)
    {
        printf("Problem opening output: %s\n", outfile_ch1);
        return;
    }

    fret = ftruncate(output_ch1_fd, out_ch_size );
    if( fret != 0 )
    {
        perror("Faild ftruncate");
        return;
    }
    rgb_start_ch1 = rgb_ch1 = mmap(NULL, out_ch_size, PROT_READ | PROT_WRITE, MAP_SHARED /*| MAP_POPULATE*/, output_ch1_fd, 0);
    if( rgb_ch1 == MAP_FAILED )
    {
        perror("Faild mmaping output");
        return;
    }

    rgb_start_ch1 = (uint16_t *)put_tiff((uint8_t *)rgb_ch1, width / 2, height / 2, bpp);

    for (i = 0; i < height; i+=2) {
        for (j = 0; j < width; j+=2) {
            rgb_start_ch1[0 + 3*k] = rgb_start_ch1[1 + 3*k] = rgb_start_ch1[2 + 3*k] = bayer[i * width + j];
            k++;
        }
        //printf("rgb_start_ch1[%d] = 0x%x, bayer[%d,%d] = 0x%x \n",3*k,rgb_start_ch1[0 + 3*k],i,j,bayer[i * width + j]);
    }
    if( msync(rgb_ch1, out_ch_size, MS_INVALIDATE|MS_SYNC) != 0 )
		perror("Problem msyncing");
    munmap(rgb_ch1,out_ch_size);
    if( fsync(output_ch1_fd) != 0 )
		perror("Problem fsyncing");
    close(output_ch1_fd);
#if 1
    strcat(outfile_ch2,outfile);
    strcat(outfile_ch2,"_ch2.tif");
    output_ch2_fd = open(outfile_ch2, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
    if(output_ch2_fd < 0)
    {
        printf("Problem opening output: %s\n", outfile_ch2);
        return;
    }
    fret = ftruncate(output_ch2_fd, out_ch_size );
    if( fret != 0 )
    {
        perror("Faild ftruncate");
        return;
    }
    rgb_start_ch2 = rgb_ch2 = mmap(NULL, out_ch_size, PROT_READ | PROT_WRITE, MAP_SHARED /*| MAP_POPULATE*/, output_ch2_fd, 0);
    if( rgb_ch2 == MAP_FAILED )
    {
        perror("Faild mmaping output");
        return;
    }

    rgb_start_ch2 = (uint16_t *)put_tiff((uint8_t *)rgb_ch2, width / 2, height / 2, bpp);

    k = 0;
    for (i = 0; i < height; i+=2) {
        for (j = 1; j < width; j+=2) {
            rgb_start_ch2[0 + 3*k] = rgb_start_ch2[1 + 3*k] = rgb_start_ch2[2 + 3*k] = bayer[i * width + j];
            k++;
        }
    }
    if( msync(rgb_ch2, out_ch_size, MS_INVALIDATE|MS_SYNC) != 0 )
		perror("Problem msyncing");
    munmap(rgb_ch2,out_ch_size);
    if( fsync(output_ch2_fd) != 0 )
		perror("Problem fsyncing");
    close(output_ch2_fd);

    strcat(outfile_ch3,outfile);
    strcat(outfile_ch3,"_ch3.tif");
    output_ch3_fd = open(outfile_ch3, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
    if(output_ch3_fd < 0)
    {
        printf("Problem opening output: %s\n", outfile_ch3);
        return;
    }
    fret = ftruncate(output_ch3_fd, out_ch_size );
    if( fret != 0 )
    {
        perror("Faild ftruncate");
        return;
    }
    rgb_start_ch3 = rgb_ch3 = mmap(NULL, out_ch_size, PROT_READ | PROT_WRITE, MAP_SHARED /*| MAP_POPULATE*/, output_ch3_fd, 0);
    if( rgb_ch3 == MAP_FAILED )
    {
        perror("Faild mmaping output");
        return;
    }

    rgb_start_ch3 = (uint16_t *)put_tiff((uint8_t *)rgb_ch3, width / 2, height / 2, bpp);

    k = 0;
    for (i = 1; i < height; i+=2) {
        for (j = 0; j < width; j+=2) {
            rgb_start_ch3[0 + 3*k] = rgb_start_ch3[1 + 3*k] = rgb_start_ch3[2 + 3*k] = bayer[i * width + j];
            k++;
        }
    }
    if( msync(rgb_ch3, out_ch_size, MS_INVALIDATE|MS_SYNC) != 0 )
		perror("Problem msyncing");
    munmap(rgb_ch3,out_ch_size);
    if( fsync(output_ch3_fd) != 0 )
		perror("Problem fsyncing");
    close(output_ch3_fd);

    strcat(outfile_ch4,outfile);
    strcat(outfile_ch4,"_ch4.tif");
    output_ch4_fd = open(outfile_ch4, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
    if(output_ch4_fd < 0)
    {
        printf("Problem opening output: %s\n", outfile_ch4);
        return;
    }
    fret = ftruncate(output_ch4_fd, out_ch_size );
    if( fret != 0 )
    {
        perror("Faild ftruncate");
        return;
    }
    rgb_start_ch4 = rgb_ch4 = mmap(NULL, out_ch_size, PROT_READ | PROT_WRITE, MAP_SHARED /*| MAP_POPULATE*/, output_ch4_fd, 0);
    if( rgb_ch4 == MAP_FAILED )
    {
        perror("Faild mmaping output");
        return;
    }

    rgb_start_ch4 = (uint16_t *)put_tiff((uint8_t *)rgb_ch4, width / 2, height / 2, bpp);

    k = 0;
    for (i = 1; i < height; i+=2) {
        for (j = 1; j < width; j+=2) {
            rgb_start_ch4[0 + 3*k] = rgb_start_ch4[1 + 3*k] = rgb_start_ch4[2 + 3*k] = bayer[i * width + j];
            k++;
        }
    }
    if( msync(rgb_ch4, out_ch_size, MS_INVALIDATE|MS_SYNC) != 0 )
		perror("Problem msyncing");
    munmap(rgb_ch4,out_ch_size);
    if( fsync(output_ch4_fd) != 0 )
		perror("Problem fsyncing");
    close(output_ch4_fd);
#endif
    return;
}

int main( int argc, char ** argv )
{
	uint32_t in_size=0, out_size=0, width=0, height=0, bpp=0;
	int first_color = DC1394_COLOR_FILTER_RGGB;
	int tiff = 0;
	int method = DC1394_BAYER_METHOD_BILINEAR;
	char *infile=NULL, *outfile=NULL;
	int input_fd = 0;
	int output_fd = 0;
	void * bayer = NULL;
	void * rgb = NULL, *rgb_start = NULL;
	char c;
	int optidx = 0;
	int swap = 0;
	int fret = 0;
	int bits = 16;
	int m = 0;

    struct option longopt[] = {
        {"input",1,NULL,'i'},
        {"output",1,NULL,'o'},
        {"width",1,NULL,'w'},
        {"height",1,NULL,'v'},
        {"help",0,NULL,'h'},
        {"bpp",1,NULL,'b'},
        {"first",1,NULL,'f'},
        {"method",1,NULL,'m'},
        {"tiff",0,NULL,'t'},
        {"swap",0,NULL,'s'},
        {0,0,0,0}
    };

    while ((c=getopt_long(argc,argv,"i:o:w:v:b:f:m:ths",longopt,&optidx)) != -1)
    {
        switch ( c )
        {
            case 'i':
                infile = strdup( optarg );
                break;
            case 'o':
                outfile = strdup( optarg );
                break;
            case 'w':
                width = strtol( optarg, NULL, 10 );
                break;
            case 'v':
                height = strtol( optarg, NULL, 10 );
                break;
            case 'b':
                bpp = strtol( optarg, NULL, 10 );
                break;
            case 'f':
                first_color = getFirstColor( optarg );
                break;
            case 'm':
                method = getMethod( optarg );
                break;
            case 's':
                swap = 1;
                break;
            case 't':
                tiff = TIFF_HDR_SIZE;
                break;
            case 'h':
                usage(argv[0]);
                return 0;
                break;
            default:
                printf("bad arg\n");
    			usage(argv[0]);
                return 1;
        }
    }
    // arguments: infile outfile width height bpp first_color
    if( infile == NULL || outfile == NULL || bpp == 0 || width == 0 || height == 0 )
    {
        printf("Bad parameter\n");
		usage(argv[0]);
        return 1;
    }

    input_fd = open(infile, O_RDONLY);
    if(input_fd < 0)
    {
        printf("Problem opening input: %s\n", infile);
        return 1;
    }

    output_fd = open(outfile, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
    if(output_fd < 0)
    {
        printf("Problem opening output: %s\n", outfile);
        return 1;
    }

    in_size = lseek(input_fd, 0, SEEK_END );
    lseek(input_fd, 0, 0);

	if(bpp == 8)
    		bits = 8;
	else
		bits = 16;

	out_size = width * height * (bits / 8) * 3 + tiff;
    fret = ftruncate(output_fd, out_size );
    if( fret != 0 )
    {
        perror("Faild ftruncate");
        return 1;
    }
    bayer = mmap(NULL, in_size, PROT_READ | PROT_WRITE, MAP_PRIVATE /*| MAP_POPULATE*/, input_fd, 0);
    if( bayer == MAP_FAILED )
    {
        perror("Faild mmaping input");
        return 1;
    }
    rgb_start = rgb = mmap(NULL, out_size, PROT_READ | PROT_WRITE, MAP_SHARED /*| MAP_POPULATE*/, output_fd, 0);
    if( rgb == MAP_FAILED )
    {
        perror("Faild mmaping output");
        return 1;
    }
#if DEBUG
    printf("%p -> %p\n", bayer, rgb);

    printf("%s: %s(%d) %s(%d) %d %d %d, %d %d\n", argv[0], infile, in_size, outfile, out_size, width, height, bpp, first_color, method );

    //memset(rgb, 0xff, out_size);//return 1;
#endif

	if(tiff)
	{
		rgb_start = put_tiff(rgb, width, height, bits);
	}
#if 1
	switch(bits)
	{
		case 8:
			dc1394_bayer_decoding_8bit((const uint8_t*)bayer, (uint8_t*)rgb_start, width, height, first_color, method);
			break;
		case 16:
		default:
			for(m = 0; m < width*height; m++){
				*(((uint16_t*)bayer)+m) = (*(((uint16_t*)bayer)+m)<<(16 - bpp));
			}
			if(swap){
			    uint8_t tmp=0;
			    uint32_t i=0;
			    for(i=0;i<in_size;i+=2){
			        tmp = *(((uint8_t*)bayer)+i);
			        *(((uint8_t*)bayer)+i) = *(((uint8_t*)bayer)+i+1);
			        *(((uint8_t*)bayer)+i+1) = tmp;
			    }
			}
			dc1394_bayer_decoding_16bit((const uint16_t*)bayer, (uint16_t*)rgb_start, width, height, first_color, method, bits);
			break;
	}
#endif

    if(method == DC1394_BAYER_METHOD_ORI){
        switch(bits)
            {
            	case 8:
            		fwrite_spilt_channel_file_8bit(outfile, (const uint8_t*)bayer, width, height, first_color, bits);
            		break;
            	case 16:
            	default:
                    fwrite_spilt_channel_file_16bit(outfile, (const uint16_t*)bayer, width, height, first_color, bits);
            		break;
            }
    }

#if DEBUG
	printf("Last few In: %x %x %x %x\n", 
			((uint32_t*)bayer)[0],
			((uint32_t*)bayer)[1],
			((uint32_t*)bayer)[2],
			((uint32_t*)bayer)[3]);

//			((int*)rgb)[2] = 0xadadadad;
	printf("Last few Out: %x %x %x %x\n", 
			((uint32_t*)rgb)[0],
			((uint32_t*)rgb)[1],
			((uint32_t*)rgb)[2],
			((uint32_t*)rgb)[3]);
#endif

    munmap(bayer,in_size);
    close(input_fd);

    if( msync(rgb, out_size, MS_INVALIDATE|MS_SYNC) != 0 )
		perror("Problem msyncing");
    munmap(rgb,out_size);
    if( fsync(output_fd) != 0 )
		perror("Problem fsyncing");
    close(output_fd);

    return 0;
}
