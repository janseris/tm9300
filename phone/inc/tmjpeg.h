// A baseline JPEG decoder (0.19), for the maps' thumbnails in the Gbx files (256 x 256, 4:2:0). Own code after the
// JPEG standard (ITU T.81): Huffman, the integer inverse DCT, YCbCr to RGB.
#ifndef TMJPEG_H
#define TMJPEG_H

// the picture, its middle cut to aW:aH and scaled to aW x aH into aOut (RGB565); aFlipY: upside down (the game
// stores its thumbnails so); false on a JPEG it cannot read (progressive, 12-bit, cut short)
bool TmJpegDecode(const unsigned char* aData, int aLen, unsigned short* aOut, int aW, int aH, bool aFlipY);

#endif
