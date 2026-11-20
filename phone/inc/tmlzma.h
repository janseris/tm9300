// tmlzma.h - an LZMA decoder (0.18) for the packed tracks: a raw LZMA stream from memory into a buffer of the known
// size (the whole output is the dictionary). Portable C++ for GCC 2.9: no libc, no floats, no writable static data.
#ifndef TMLZMA_H
#define TMLZMA_H

// aProps: (pb * 5 + lp) * 9 + lc. aProbs: TmLzmaProbs(aProps) unsigned shorts of scratch. Returns the bytes written
// (aOutSize when all went well), -1 on a broken stream
int TmLzmaProbs(int aProps);
int TmLzmaDecode(const unsigned char* aIn, int aInSize, unsigned char* aOut, int aOutSize, int aProps, unsigned short* aProbs);

// 0.18: undoes tools/tmbz.py's filter ("TMBF") in the decoded track, in place: the corners' and the normals' byte
// planes back to x, y, z shorts, the corner indices back from "the highest so far + 1 minus it". aTmp: a scratch of
// 393216 bytes (the biggest array: 65535 corners). false: the track's layout does not fit its size
bool TmTmbUnfilter(unsigned char* aD, int aSize, unsigned char* aTmp);

#endif
