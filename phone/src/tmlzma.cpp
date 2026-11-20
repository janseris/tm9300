// tmlzma.cpp - the LZMA decoder (0.18): the range coder, the literal and match coders of Igor Pavlov's LZMA
// (its specification, public domain), decoding straight into the output buffer (the dictionary is the output)
#include "tmlzma.h"

enum { KBits = 11, KOne = 1 << KBits, KMove = 5, KTop = 1 << 24 };
enum { KStates = 12, KPosMax = 16, KLenLow = 8, KLenMid = 8, KLenHigh = 256 };
// the probabilities' layout
enum
    {
    OIsMatch = 0,
    OIsRep = OIsMatch + KStates * KPosMax,
    OIsRepG0 = OIsRep + KStates,
    OIsRepG1 = OIsRepG0 + KStates,
    OIsRepG2 = OIsRepG1 + KStates,
    OIsRep0Long = OIsRepG2 + KStates,
    OPosSlot = OIsRep0Long + KStates * KPosMax,
    OSpecPos = OPosSlot + 4 * 64,
    OAlign = OSpecPos + 115,
    OLen = OAlign + 16,
    KLenSize = 2 + KPosMax * KLenLow + KPosMax * KLenMid + KLenHigh,
    ORepLen = OLen + KLenSize,
    OLiteral = ORepLen + KLenSize
    };

struct TRange
    {
    const unsigned char* p; const unsigned char* end;
    unsigned int range, code;
    int bad;
    inline unsigned char Next() { if (p < end) return *p++; bad = 1; return 0; }
    inline void Norm() { if (range < KTop) { range <<= 8; code = (code << 8) | Next(); } }
    inline int Bit(unsigned short* aP)
        {
        unsigned int v = *aP, bound = (range >> KBits) * v;
        int b;
        if (code < bound) { v += (KOne - v) >> KMove; range = bound; b = 0; }
        else { v -= v >> KMove; code -= bound; range -= bound; b = 1; }
        *aP = (unsigned short) v;
        Norm();
        return b;
        }
    unsigned int Direct(int aN)
        {
        unsigned int res = 0;
        do
            {
            range >>= 1; code -= range;
            unsigned int t = 0 - (code >> 31);
            code += range & t;
            if (code == range) bad = 1;
            Norm();
            res = (res << 1) + (t + 1);
            } while (--aN);
        return res;
        }
    unsigned int Tree(unsigned short* aP, int aN) { unsigned int m = 1; for (int i = 0; i < aN; i++) m = (m << 1) + Bit(aP + m); return m - (1u << aN); }
    unsigned int Rev(unsigned short* aP, int aN) { unsigned int m = 1, s = 0; for (int i = 0; i < aN; i++) { unsigned int b = Bit(aP + m); m = (m << 1) + b; s |= b << i; } return s; }
    };

int TmLzmaProbs(int aProps)
    {
    const int lc = aProps % 9, lp = (aProps / 9) % 5;
    return OLiteral + (0x300 << (lc + lp));
    }

static unsigned int Len(TRange& r, unsigned short* aP, unsigned int aPosState)
    {
    if (!r.Bit(aP)) return r.Tree(aP + 2 + aPosState * KLenLow, 3);
    if (!r.Bit(aP + 1)) return 8 + r.Tree(aP + 2 + KPosMax * KLenLow + aPosState * KLenMid, 3);
    return 16 + r.Tree(aP + 2 + KPosMax * KLenLow + KPosMax * KLenMid, 8);
    }

int TmLzmaDecode(const unsigned char* aIn, int aInSize, unsigned char* aOut, int aOutSize, int aProps, unsigned short* aProbs)
    {
    if (aProps < 0 || aProps >= 9 * 5 * 5) return -1;
    const int lc = aProps % 9, lp = (aProps / 9) % 5, pb = aProps / 45;
    const unsigned int lpMask = (1u << lp) - 1, pbMask = (1u << pb) - 1;
    const int n = TmLzmaProbs(aProps);
    for (int i = 0; i < n; i++) aProbs[i] = KOne / 2;
    TRange r; r.p = aIn; r.end = aIn + aInSize; r.range = 0xFFFFFFFFu; r.code = 0; r.bad = 0;
    if (r.Next() != 0) return -1;
    for (int i = 0; i < 4; i++) r.code = (r.code << 8) | r.Next();
    unsigned int state = 0, rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    unsigned int pos = 0;
    const unsigned int size = (unsigned int) aOutSize;
    while (pos < size)
        {
        if (r.bad) return -1;
        const unsigned int posState = pos & pbMask;
        if (!r.Bit(aProbs + OIsMatch + state * KPosMax + posState))
            {
            // a literal
            const unsigned int prev = pos ? aOut[pos - 1] : 0;
            unsigned short* lp2 = aProbs + OLiteral + 0x300 * (((pos & lpMask) << lc) + (prev >> (8 - lc)));
            unsigned int sym = 1;
            if (state >= 7)
                {
                if (rep0 >= pos) return -1;
                unsigned int mb = aOut[pos - rep0 - 1];
                do
                    {
                    const unsigned int mbit = (mb >> 7) & 1; mb <<= 1;
                    const unsigned int b = r.Bit(lp2 + ((1 + mbit) << 8) + sym);
                    sym = (sym << 1) | b;
                    if (mbit != b) break;
                    } while (sym < 0x100);
                }
            while (sym < 0x100) sym = (sym << 1) | r.Bit(lp2 + sym);
            aOut[pos++] = (unsigned char) sym;
            state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
            continue;
            }
        unsigned int len;
        if (r.Bit(aProbs + OIsRep + state))
            {
            if (!r.Bit(aProbs + OIsRepG0 + state))
                {
                if (!r.Bit(aProbs + OIsRep0Long + state * KPosMax + posState))
                    {
                    // a single byte from rep0
                    if (rep0 >= pos) return -1;
                    state = state < 7 ? 9 : 11;
                    aOut[pos] = aOut[pos - rep0 - 1]; pos++;
                    continue;
                    }
                }
            else
                {
                unsigned int d;
                if (!r.Bit(aProbs + OIsRepG1 + state)) d = rep1;
                else { if (!r.Bit(aProbs + OIsRepG2 + state)) d = rep2; else { d = rep3; rep3 = rep2; } rep2 = rep1; }
                rep1 = rep0; rep0 = d;
                }
            len = Len(r, aProbs + ORepLen, posState);
            state = state < 7 ? 8 : 11;
            }
        else
            {
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            len = Len(r, aProbs + OLen, posState);
            state = state < 7 ? 7 : 10;
            // the distance
            const unsigned int ls = len < 3 ? len : 3;
            const unsigned int slot = r.Tree(aProbs + OPosSlot + ls * 64, 6);
            if (slot < 4) rep0 = slot;
            else
                {
                const int nd = (int) (slot >> 1) - 1;
                unsigned int d = (2 | (slot & 1)) << nd;
                if (slot < 14) d += r.Rev(aProbs + OSpecPos + d - slot, nd);
                else { d += r.Direct(nd - 4) << 4; d += r.Rev(aProbs + OAlign, 4); }
                rep0 = d;
                }
            if (rep0 == 0xFFFFFFFFu) break;             // the end marker
            }
        len += 2;
        if (rep0 >= pos) return -1;
        if (len > size - pos) len = size - pos;
        unsigned char* o = aOut + pos; const unsigned char* s = o - rep0 - 1;
        for (unsigned int k = 0; k < len; k++) o[k] = s[k];
        pos += len;
        }
    return r.bad ? -1 : (int) pos;
    }
