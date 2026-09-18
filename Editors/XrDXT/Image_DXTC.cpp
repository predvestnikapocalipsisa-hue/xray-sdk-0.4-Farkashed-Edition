
#include "stdafx.h"

#include "Image_DXTC.h"

#define LOW_5 0x001F;
#define MID_6 0x07E0;
#define HIGH_5 0xF800;

#define MID_555 0x03E0;
#define HI_555 0x7C00;

WORD GetNumberOfBits(DWORD dwMask);

Image_DXTC::Image_DXTC()
{
	m_pCompBytes = NULL;
	m_pDecompBytes = NULL;
}

Image_DXTC::~Image_DXTC()
{

	if (m_pCompBytes != NULL)
	{
		free(m_pCompBytes);
		m_pCompBytes = NULL;
	}

	if (m_pDecompBytes != NULL)
	{
		free(m_pDecompBytes);
		m_pDecompBytes = NULL;
	}
}

void Image_DXTC::SaveAsRaw()
{

	FILE* pf = fopen("decom.raw", "wb");

	VERIFY(pf);

	fwrite(m_pDecompBytes, m_nHeight * m_nWidth * 4, sizeof(byte), pf);

	fclose(pf);
	pf = NULL;
}

bool Image_DXTC::LoadFromFile(LPCSTR filename)
{
	if (m_pCompBytes != NULL)
	{
		free(m_pCompBytes);
		m_pCompBytes = NULL;
	}

	char* exts[] = { ".DDS" };
	int next = 1;

	char fileupper[256];

	xr_strcpy(fileupper, filename);
	strupr(fileupper);

	int i;
	bool knownformat = false;

	for (i = 0; i < next; i++)
	{
		char* found = strstr(fileupper, exts[0]);

		if (found != NULL)
		{
			knownformat = true;
			break;
		}
	}

	if (knownformat == false)
	{
		return (false);
	}

	FILE* file = fopen(filename, "rb");

	if (file == NULL)
	{
		return (false);
	}

	DDS_HEADER ddsd;
	DWORD dwMagic;

	fread(&dwMagic, sizeof(DWORD), 1, file);

	if (dwMagic != MAKEFOURCC('D', 'D', 'S', ' '))
	{
		fclose(file);
		return (false);
	}

	fread(&ddsd, sizeof(DDS_HEADER), 1, file);

	m_bMipTexture = (ddsd.dwMipMapCount > 0) ? TRUE : FALSE;

	DecodePixelFormat(m_strFormat, &(ddsd.ddspf));

	if (m_CompFormat == PF_DXT1 ||
		m_CompFormat == PF_DXT2 ||
		m_CompFormat == PF_DXT3 ||
		m_CompFormat == PF_DXT4 ||
		m_CompFormat == PF_DXT5)
	{

	}
	else
	{
		return (false);
	}

	m_DDSD = ddsd;

	m_nHeight = ddsd.dwHeight;
	m_nWidth = ddsd.dwWidth;

	if (ddsd.dwHeaderFlags & DDSD_LINEARSIZE)
	{
		m_pCompBytes = (BYTE*)calloc(ddsd.dwPitchOrLinearSize, sizeof(BYTE));

		if (m_pCompBytes == NULL)
		{
			return (false);
		}

		fread(m_pCompBytes, ddsd.dwPitchOrLinearSize, 1, file);
	}
	else
	{

		DWORD dwBytesPerRow = ddsd.dwWidth * ddsd.ddspf.dwRGBBitCount / 8;

		m_pCompBytes = (BYTE*)calloc(ddsd.dwPitchOrLinearSize * ddsd.dwHeight, sizeof(BYTE));

		m_nCompSize = ddsd.dwPitchOrLinearSize * ddsd.dwHeight;
		m_nCompLineSz = dwBytesPerRow;

		if (m_pCompBytes == NULL)
		{
			return (false);
		}

		BYTE* pDest = m_pCompBytes;

		for (DWORD yp = 0; yp < ddsd.dwHeight; yp++)
		{
			fread(pDest, dwBytesPerRow, 1, file);
			pDest += ddsd.dwPitchOrLinearSize;
		}
	}

	fclose(file);
	file = NULL;

	return (true);
}

void Image_DXTC::AllocateDecompBytes()
{

	if (m_pDecompBytes != NULL)
	{
		free(m_pDecompBytes);
		m_pDecompBytes = NULL;
	}


	m_pDecompBytes = (BYTE*)calloc(m_DDSD.dwWidth * m_DDSD.dwHeight * 4,
		sizeof(BYTE));

	if (m_pDecompBytes == NULL)
	{

	}
}

void Image_DXTC::Decompress()
{
	VERIFY(m_pCompBytes);

	AllocateDecompBytes();

	VERIFY(m_pDecompBytes); 

	switch (m_CompFormat)
	{
	case PF_DXT1:
		DecompressDXT1();
		break;

	case PF_DXT2:
		DecompressDXT2();
		break;

	case PF_DXT3:
		DecompressDXT3();
		break;

	case PF_DXT4:
		DecompressDXT4();
		break;

	case PF_DXT5:
		DecompressDXT5();
		break;

	case PF_UNKNOWN:
		break;
	}
	for (int y = 0; y < m_nHeight; y++)
	{
		for (int x = 0; x < m_nWidth; x++)
		{
			BYTE* ptr = m_pDecompBytes + (y * m_nWidth + x) * 4;
			swap(ptr[0], ptr[2]);
		}
	}
}

struct DXTColBlock
{
	WORD col0;
	WORD col1;

	BYTE row[4];
};

struct DXTAlphaBlockExplicit
{
	WORD row[4];
};

struct DXTAlphaBlock3BitLinear
{
	BYTE alpha0;
	BYTE alpha1;

	BYTE stuff[6];
};

struct Color8888
{
	BYTE r;
	BYTE g; 
	BYTE b; 
	BYTE a;
};

struct Color565
{
	unsigned nBlue : 5;	 
	unsigned nGreen : 6; 
	unsigned nRed : 5;
};

inline void GetColorBlockColors(DXTColBlock* pBlock, Color8888* col_0, Color8888* col_1,
	Color8888* col_2, Color8888* col_3,
	WORD& wrd)
{

	Color565* pCol;

	pCol = (Color565*)&(pBlock->col0);

	col_0->a = 0xff;
	col_0->r = pCol->nRed;
	col_0->r <<= 3; 
	col_0->g = pCol->nGreen;
	col_0->g <<= 2;
	col_0->b = pCol->nBlue;
	col_0->b <<= 3;

	pCol = (Color565*)&(pBlock->col1);
	col_1->a = 0xff;
	col_1->r = pCol->nRed;
	col_1->r <<= 3;
	col_1->g = pCol->nGreen;
	col_1->g <<= 2;
	col_1->b = pCol->nBlue;
	col_1->b <<= 3;

	if (pBlock->col0 > pBlock->col1)
	{

		wrd = ((WORD)col_0->r * 2 + (WORD)col_1->r) / 3;
		col_2->r = (BYTE)wrd;

		wrd = ((WORD)col_0->g * 2 + (WORD)col_1->g) / 3;
		col_2->g = (BYTE)wrd;

		wrd = ((WORD)col_0->b * 2 + (WORD)col_1->b) / 3;
		col_2->b = (BYTE)wrd;
		col_2->a = 0xff;

		wrd = ((WORD)col_0->r + (WORD)col_1->r * 2) / 3;
		col_3->r = (BYTE)wrd;

		wrd = ((WORD)col_0->g + (WORD)col_1->g * 2) / 3;
		col_3->g = (BYTE)wrd;

		wrd = ((WORD)col_0->b + (WORD)col_1->b * 2) / 3;
		col_3->b = (BYTE)wrd;
		col_3->a = 0xff;
	}
	else
	{

		wrd = ((WORD)col_0->r + (WORD)col_1->r) / 2;
		col_2->r = (BYTE)wrd;
		wrd = ((WORD)col_0->g + (WORD)col_1->g) / 2;
		col_2->g = (BYTE)wrd;
		wrd = ((WORD)col_0->b + (WORD)col_1->b) / 2;
		col_2->b = (BYTE)wrd;
		col_2->a = 0xff;

		col_3->r = 0x00;
		col_3->g = 0xff;
		col_3->b = 0xff;
		col_3->a = 0x00;
	}
} 

inline void DecodeColorBlock(DWORD* pImPos, DXTColBlock* pColorBlock, int width,
	DWORD* col_0,
	DWORD* col_1, DWORD* col_2, DWORD* col_3)
{

	DWORD bits;
	int r, n;

	const DWORD masks[] = { 3, 12, 3 << 4, 3 << 6 };
	const int shift[] = { 0, 2, 4, 6 };

	for (r = 0; r < 4; r++, pImPos += width - 4)
	{
		for (n = 0; n < 4; n++)
		{
			bits = pColorBlock->row[r] & masks[n];
			bits >>= shift[n];

			switch (bits)
			{
			case 0:
				*pImPos = *col_0;
				pImPos++;
				break;
			case 1:
				*pImPos = *col_1;
				pImPos++;
				break;
			case 2:
				*pImPos = *col_2;
				pImPos++;
				break;
			case 3:
				*pImPos = *col_3;
				pImPos++;
				break;
			default:
				pImPos++;
				break;
			}
		}
	}
}

inline void DecodeAlphaExplicit(DWORD* pImPos, DXTAlphaBlockExplicit* pAlphaBlock,
	int width, DWORD alphazero)
{
	int row, pix;

	WORD wrd;

	Color8888 col;
	col.r = col.g = col.b = 0;

	for (row = 0; row < 4; row++, pImPos += width - 4)
	{

		wrd = pAlphaBlock->row[row];


		for (pix = 0; pix < 4; pix++)
		{
			*pImPos &= alphazero;

			col.a = wrd & 0x000f;		  
			col.a = col.a | (col.a << 4); 

			*pImPos |= *((DWORD*)&col); 

			wrd >>= 4; 

			pImPos++; 
		}
	}
}

BYTE gBits[4][4];
WORD gAlphas[8];
Color8888 gACol[4][4];

inline void DecodeAlpha3BitLinear(DWORD* pImPos, DXTAlphaBlock3BitLinear* pAlphaBlock,
	int width, DWORD alphazero)
{

	gAlphas[0] = pAlphaBlock->alpha0;
	gAlphas[1] = pAlphaBlock->alpha1;

	if (gAlphas[0] > gAlphas[1])
	{

		gAlphas[2] = (6 * gAlphas[0] + gAlphas[1]) / 7;		
		gAlphas[3] = (5 * gAlphas[0] + 2 * gAlphas[1]) / 7; 
		gAlphas[4] = (4 * gAlphas[0] + 3 * gAlphas[1]) / 7; 
		gAlphas[5] = (3 * gAlphas[0] + 4 * gAlphas[1]) / 7; 
		gAlphas[6] = (2 * gAlphas[0] + 5 * gAlphas[1]) / 7; 
		gAlphas[7] = (gAlphas[0] + 6 * gAlphas[1]) / 7;		
	}
	else
	{

		gAlphas[2] = (4 * gAlphas[0] + gAlphas[1]) / 5;		
		gAlphas[3] = (3 * gAlphas[0] + 2 * gAlphas[1]) / 5;
		gAlphas[4] = (2 * gAlphas[0] + 3 * gAlphas[1]) / 5;
		gAlphas[5] = (gAlphas[0] + 4 * gAlphas[1]) / 5;		
		gAlphas[6] = 0;										
		gAlphas[7] = 255;									
	}

	const DWORD mask = 0x00000007; 

	DWORD bits = *((DWORD*)&(pAlphaBlock->stuff[0]));

	gBits[0][0] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[0][1] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[0][2] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[0][3] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[1][0] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[1][1] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[1][2] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[1][3] = (BYTE)(bits & mask);

	bits = *((DWORD*)&(pAlphaBlock->stuff[3])); 

	gBits[2][0] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[2][1] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[2][2] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[2][3] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[3][0] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[3][1] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[3][2] = (BYTE)(bits & mask);
	bits >>= 3;
	gBits[3][3] = (BYTE)(bits & mask);

	int row, pix;

	for (row = 0; row < 4; row++)
	{
		for (pix = 0; pix < 4; pix++)
		{
			gACol[row][pix].a = (BYTE)gAlphas[gBits[row][pix]];

			VERIFY(gACol[row][pix].r == 0);
			VERIFY(gACol[row][pix].g == 0);
			VERIFY(gACol[row][pix].b == 0);
		}
	}

	for (row = 0; row < 4; row++, pImPos += width - 4)
	{

		for (pix = 0; pix < 4; pix++)
		{
			*pImPos &= alphazero;

			*pImPos |= *((DWORD*)&(gACol[row][pix])); 
			pImPos++;
		}
	}
}

void Image_DXTC::DecompressDXT1()
{

	int xblocks, yblocks;

	xblocks = m_DDSD.dwWidth / 4;
	yblocks = m_DDSD.dwHeight / 4;

	int i, j;

	DWORD* pBase = (DWORD*)m_pDecompBytes;
	DWORD* pImPos = (DWORD*)pBase;	   

	DXTColBlock* pBlock;

	Color8888 col_0, col_1, col_2, col_3;

	WORD wrd;

	for (j = 0; j < yblocks; j++)
	{
		pBlock = (DXTColBlock*)((DWORD)m_pCompBytes + j * xblocks * 8);

		for (i = 0; i < xblocks; i++, pBlock++)
		{

			GetColorBlockColors(pBlock, &col_0, &col_1, &col_2, &col_3, wrd);


			pImPos = reinterpret_cast<DWORD*>(
				reinterpret_cast<BYTE*>(pBase) +
				static_cast<size_t>(i) * 16 +
				static_cast<size_t>(j * 4) * m_nWidth * 4
				);

			DecodeColorBlock(pImPos, pBlock, m_nWidth, (DWORD*)&col_0, (DWORD*)&col_1,
				(DWORD*)&col_2, (DWORD*)&col_3);
		}
	}
}

void Image_DXTC::DecompressDXT2()
{

	VERIFY(false);
}

void Image_DXTC::DecompressDXT3()
{
	int xblocks, yblocks;

	xblocks = m_DDSD.dwWidth / 4;
	yblocks = m_DDSD.dwHeight / 4;

	int i, j;

	DWORD* pBase = (DWORD*)m_pDecompBytes;
	DWORD* pImPos = (DWORD*)pBase;	  

	DXTColBlock* pBlock;
	DXTAlphaBlockExplicit* pAlphaBlock;

	Color8888 col_0, col_1, col_2, col_3;

	WORD wrd;

	col_0.a = 0;
	col_0.r = col_0.g = col_0.b = 0xff;
	DWORD alphazero = *((DWORD*)&col_0);


	for (j = 0; j < yblocks; j++)
	{

		pBlock = reinterpret_cast<DXTColBlock*>(
			m_pCompBytes + static_cast<size_t>(j) * xblocks * 16
			);

		for (i = 0; i < xblocks; i++, pBlock++)
		{

			pAlphaBlock = (DXTAlphaBlockExplicit*)pBlock;

			pBlock++;
			GetColorBlockColors(pBlock, &col_0, &col_1, &col_2, &col_3, wrd);

			pImPos = reinterpret_cast<DWORD*>(
				reinterpret_cast<BYTE*>(pBase) +
				static_cast<size_t>(i) * 16 +
				static_cast<size_t>(j * 4) * m_nWidth * 4
				);

			DecodeColorBlock(pImPos, pBlock, m_nWidth, (DWORD*)&col_0, (DWORD*)&col_1,
				(DWORD*)&col_2, (DWORD*)&col_3);

			DecodeAlphaExplicit(pImPos, pAlphaBlock, m_nWidth, alphazero);
		}
	}
}

void Image_DXTC::DecompressDXT4()
{


	VERIFY(false);
}

void Image_DXTC::DecompressDXT5()
{

	int xblocks, yblocks;

	xblocks = m_DDSD.dwWidth / 4;
	yblocks = m_DDSD.dwHeight / 4;

	int i, j;

	DWORD* pBase = (DWORD*)m_pDecompBytes;
	DWORD* pImPos = (DWORD*)pBase;	   

	DXTColBlock* pBlock;
	DXTAlphaBlock3BitLinear* pAlphaBlock;

	Color8888 col_0, col_1, col_2, col_3;
	WORD wrd;

	col_0.a = 0;
	col_0.r = col_0.g = col_0.b = 0xff;
	DWORD alphazero = *((DWORD*)&col_0);

	for (j = 0; j < yblocks; j++)
	{

		pBlock = reinterpret_cast<DXTColBlock*>(
			m_pCompBytes + static_cast<size_t>(j) * xblocks * 16
			);		

		for (i = 0; i < xblocks; i++, pBlock++)
		{

			pAlphaBlock = (DXTAlphaBlock3BitLinear*)pBlock;

			pBlock++;

			GetColorBlockColors(pBlock, &col_0, &col_1, &col_2, &col_3, wrd);

			pImPos = reinterpret_cast<DWORD*>(
				reinterpret_cast<BYTE*>(pBase) +
				static_cast<size_t>(i) * 16 +
				static_cast<size_t>(j * 4) * m_nWidth * 4
				);

			DecodeColorBlock(pImPos, pBlock, m_nWidth, (DWORD*)&col_0, (DWORD*)&col_1,
				(DWORD*)&col_2, (DWORD*)&col_3);


			DecodeAlpha3BitLinear(pImPos, pAlphaBlock, m_nWidth, alphazero);
		}
	}
} 
VOID Image_DXTC::DecodePixelFormat(CHAR* strPixelFormat, DDS_PIXELFORMAT* pddpf)
{
	switch (pddpf->dwFourCC)
	{
	case 0:
		xr_sprintf(strPixelFormat, sizeof(string256), "ARGB-%d%d%d%d%s",
			GetNumberOfBits(pddpf->dwRGBAlphaBitMask),
			GetNumberOfBits(pddpf->dwRBitMask),
			GetNumberOfBits(pddpf->dwGBitMask),
			GetNumberOfBits(pddpf->dwBBitMask),
			pddpf->dwBBitMask & DDPF_ALPHAPREMULT ? "-premul" : "");
		m_CompFormat = PF_ARGB;
		break;

	case MAKEFOURCC('D', 'X', 'T', '1'):
		strcpy(strPixelFormat, "DXT1");
		m_CompFormat = PF_DXT1;
		break;

	case MAKEFOURCC('D', 'X', 'T', '2'):
		strcpy(strPixelFormat, "DXT2");
		m_CompFormat = PF_DXT2;
		break;

	case MAKEFOURCC('D', 'X', 'T', '3'):
		strcpy(strPixelFormat, "DXT3");
		m_CompFormat = PF_DXT3;
		break;

	case MAKEFOURCC('D', 'X', 'T', '4'):
		strcpy(strPixelFormat, "DXT4");
		m_CompFormat = PF_DXT4;
		break;

	case MAKEFOURCC('D', 'X', 'T', '5'):
		strcpy(strPixelFormat, "DXT5");
		m_CompFormat = PF_DXT5;
		break;
	default:
		strcpy(strPixelFormat, "Format Unknown");
		m_CompFormat = PF_UNKNOWN;
		break;
	}
}

WORD GetNumberOfBits(DWORD dwMask)
{
	WORD wBits = 0;
	for (; dwMask; wBits++)
		dwMask = dwMask & (dwMask - 1);

	return wBits;
}