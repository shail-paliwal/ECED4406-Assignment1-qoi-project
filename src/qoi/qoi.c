 #include "qoi/qoi.h"

#include "qoi/details/unused.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef union {
	struct { unsigned char r, g, b, a; } rgba;
} qoi_rgba_t;

/*this function reads in the next 4 bytes in big endian format*/
static unsigned int big_endian_read(const unsigned char *bytes, int *ptr) {
	unsigned int byte1 = bytes[*ptr];//read in first byte
	*ptr = *ptr+1;//increase pointer to next byte
	unsigned int byte2 = bytes[*ptr];//read in second byte
	*ptr = *ptr+1;
	unsigned int byte3 = bytes[*ptr];//read in third byte
	*ptr = *ptr+1;
	unsigned int byte4 = bytes[*ptr];//read in fourth byte
	*ptr = *ptr+1;
	return byte1 << 24 | byte2 << 16 | byte3 << 8 | byte4;//return bytes in big endian format
}

/*this funtion reads in the qoi magic bytes, checking to see if they are correct*/
static unsigned int magic_bytes_read(const unsigned char *bytes, int *ptr) {
	unsigned int q = bytes[*ptr];
	*ptr = *ptr+1;
	unsigned int o = bytes[*ptr];
	*ptr = *ptr+1;
	unsigned int i = bytes[*ptr];
	*ptr = *ptr+1;
	unsigned int f = bytes[*ptr];
	*ptr = *ptr+1;
	if(q!='q'||o!='o'||i!='i'||f!='f'){//if the file does not start with "qoif"
		return 0;//return 0 for invalid
	}
	return 1;//return 1 for valid
}

void *qoi_decode(void const *(data), uint64_t (size), qoi_desc_t *(out_desc)) {
	int image_size, data_chunks, i, ptr = 0, run_length = 0;
	unsigned int magic;
	unsigned char *output_pixels;
	const unsigned char *input_bytes;
	qoi_rgba_t color_index_arr[64];//values from 0-63
	qoi_rgba_t rgba_val;//pointer to struct containing rgba values
	qoi_desc_t image_desc;//pointer to image description struct

	/*assign the input data to variable input_bytes*/
	input_bytes = (unsigned char *)data;

	/*read in the "qoif" magic header*/
	magic = magic_bytes_read(input_bytes, &ptr);//read in magic bytes and check validity; 0 for invalid, 1 for valid
	if(magic == 0){//check to see if input file is invalid 
		printf("\nINVALID FILE TYPE\n");
		return NULL;//terminate function if
	}

	/*assign the width of image using big endian read function*/
	out_desc->width = big_endian_read(input_bytes, &ptr);

	/*assign the height of image using big endian read function*/
	out_desc->height = big_endian_read(input_bytes, &ptr);

	/*assign the channel value by reading in the next byte*/
	out_desc->channels = input_bytes[ptr++];

	/*assign the colorspace value by reading in the next byte*/
	out_desc->colorspace = input_bytes[ptr++];

	/*if the channel is initalized to zero, get data from output struct*/
	if (image_desc.channels == 0) {
		image_desc.channels = out_desc->channels;
	}

	/*find size of image: width x height x # of channels*/
	image_size = out_desc->width * out_desc->height * image_desc.channels;

	/*inital memory allocation for output pixels, based on the size of image calculated above*/
	output_pixels = (unsigned char *) malloc(image_size);

	/*initalize rgba values according to qoi documentation*/
	rgba_val.rgba.r = 0;
	rgba_val.rgba.g = 0;
	rgba_val.rgba.b = 0;
	rgba_val.rgba.a = 255;

	data_chunks = size;//the length of chunks must not be greater than size of input file
	for (i = 0; i < image_size; i += image_desc.channels) {
		if (run_length > 0) {
			run_length--;//run length is stored with a bias of -1
		}
		else if (ptr < data_chunks) {
			int byte1 = input_bytes[ptr++];//assigning byte value for analysis
			switch (byte1){
				case 0xfe://QOI_OP_RGB : 11111110 - 8 bit tag
				{
					rgba_val.rgba.r = input_bytes[ptr++];//8 bit red channel value
					rgba_val.rgba.g = input_bytes[ptr++];//8 bit green channel value
					rgba_val.rgba.b = input_bytes[ptr++];//8 bit blue channel value
					break; //alpha value remains unchanged
				}
				case 0xff://QOI_OP_RGBA : 11111111 - 8 bit tag
				{
					rgba_val.rgba.r = input_bytes[ptr++];//8 bit red channel value
					rgba_val.rgba.g = input_bytes[ptr++];//8 bit green channel value
					rgba_val.rgba.b = input_bytes[ptr++];//8 bit blue channel value
					rgba_val.rgba.a = input_bytes[ptr++];//8 bit alpha channel value
					break;
				}
				default://if not QOI_OP_RGB nor QOI_OP_RGBA, then move to next bits
					switch (byte1 & 0xc0){//b1 AND 11000000
						case 0x00://QOI_OP_INDEX : 00xxxxxx - two bit tag
						{
							rgba_val = color_index_arr[byte1];//6 bit index into color index array
							break;
						}
						case 0x40://QOI_OP_DIFF : 01xxxxxx - two bit tag
						{	//difference to current channel values are using wraparound operation (((b1 >> xx) AND 00000011) - 2)
							rgba_val.rgba.r += ((byte1 >> 4) & 0x03) - 2;//red channel difference from previous pixel
							rgba_val.rgba.g += ((byte1 >> 2) & 0x03) - 2;//green channel difference from previous pixel
							rgba_val.rgba.b += ( byte1 & 0x03) - 2;//blue channel difference from previous pixel
							break;//alpha channel value remains unchanged
						}
						case 0x80://QOI_OP_LUMA : 10xxxxxx - two bit tag
						{	//difference to current channel values are using wraparound operation (& 0x0F for red and blue) (& 0x3f for green)
							int next_byte = input_bytes[ptr++];//store next byte for dr and db values
							int green_diff = (byte1 & 0x3f) - 32;//green channel difference from previous pixel -32..31
							rgba_val.rgba.r += green_diff - 8 + ((next_byte >> 4) & 0x0f);//red channel difference minus green channel difference -8..7
							rgba_val.rgba.g += green_diff;
							rgba_val.rgba.b += green_diff - 8 + (next_byte & 0x0f);//blue channel difference minus green channel difference -8..7
							break;//alpha value remains unchanged
						}
						case 0xc0://QOI_OP_RUN : 11xxxxxx - two bit tag
						{//6 bit run-length repeating previous pixel
							run_length = (byte1 & 0x3f);//ANDing with 00111111 to get run-length value
							break;
						}
						default:
							break;
					}
					break;
			}
			/*each pixel seen by decoder is put into the index array at the hash function position:
				position = (red*3 + green*5 + blue*7 + alpha*11) % 64*/
			color_index_arr[(rgba_val.rgba.r*3 + rgba_val.rgba.g*5 + rgba_val.rgba.b*7 + rgba_val.rgba.a*11) % 64] = rgba_val;
		}
		/*inserting red, green, blue, and alpha values into the pixel array at the correct positions*/
		output_pixels[i + 0] = rgba_val.rgba.r;
		output_pixels[i + 1] = rgba_val.rgba.g;
		output_pixels[i + 2] = rgba_val.rgba.b;
		output_pixels[i + 3] = rgba_val.rgba.a;
		
	}
	return output_pixels;
}
