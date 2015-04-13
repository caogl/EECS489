/* ----------------------------------------------------------------------------
Copyright (c) 1999-2002, Lev Povalahev
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, 
      this list of conditions and the following disclaimer in the documentation 
      and/or other materials provided with the distribution.
    * The name of the author may be used to endorse or promote products 
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
THE POSSIBILITY OF SUCH DAMAGE.
------------------------------------------------------------------------------*/

#include "ltga.h"
#include <fstream>
#include <stdlib.h>

//--------------------------------------------------
// global functions
//--------------------------------------------------
static int TGAReadError = 0;

void ReadData(std::ifstream &file, char* data, uint size)
{
    if (!file.is_open())
        return;
    uint a = (uint) file.tellg();
    a+= size;
    file.read(data, size);
    if (a != uint(file.tellg()))
    {
        TGAReadError = 1;
    }
}

//--------------------------------------------------
LTGA::LTGA()
{
    m_loaded = false;
    m_width = 0;
    m_height = 0;
    m_pixelDepth = 0;
    m_alphaDepth = 0;
    m_type = itUndefined;
    m_pixels = 0;
}


//--------------------------------------------------
LTGA::LTGA(const std::string &filename)
{
    m_loaded = false;
    m_width = 0;
    m_height = 0;
    m_pixelDepth = 0;
    m_alphaDepth = 0;
    m_type = itUndefined;
    m_pixels = 0;
    LoadFromFile(filename);
}


LTGA::LTGA(uint _width, uint _height) : m_height(_height), m_width(_width) {
    //bool truecolor = true;
    m_pixelDepth = 24;

    m_alphaDepth = 0;

    m_type = itRGB;

#if 0
    m_pixels = (byte*) malloc(m_width*m_height*(m_pixelDepth/8));

    memset(m_pixels, 0, m_width*m_height*(m_pixelDepth/8));
#endif

    m_loaded = true;
}


//--------------------------------------------------
LTGA::~LTGA()
{
    Clear();
}


//--------------------------------------------------
bool LTGA::LoadFromFile(const std::string &filename)
{
    if (m_loaded)
        Clear();
    m_loaded = false;

    std::ifstream file;
    file.open(filename.c_str(), std::ios::binary);
    if (!file.is_open())
        return false;

    bool rle = false;
    bool truecolor = false;
    uint CurrentPixel = 0;
    byte ch_buf1, ch_buf2;
    byte buf1[1000];

    byte IDLength;
    byte IDColorMapType;
    byte IDImageType;

    ReadData(file, (char*)&IDLength, 1);
    ReadData(file, (char*)&IDColorMapType, 1);
    
    if (IDColorMapType == 1)
        return false;

    ReadData(file, (char*)&IDImageType, 1); 

    switch (IDImageType)
    {
    case 2:
            truecolor = true;
            break;
    case 3:
            m_type = itGreyscale;
            break;
    case 10:
            rle = true;
            truecolor = true;
            break;
    case 11:
            rle = true;
            m_type = itGreyscale;
            break;
    default:
            return false;
    }

    file.seekg(5, std::ios::cur);

    file.seekg(4, std::ios::cur);
    ReadData(file, (char*)&m_width, 2);
    ReadData(file, (char*)&m_height, 2);
    ReadData(file, (char*)&m_pixelDepth, 1);

    if (! ((m_pixelDepth == 8) || (m_pixelDepth ==  24) ||
             (m_pixelDepth == 16) || (m_pixelDepth == 32)))
        return false;

    ReadData(file, (char*)&ch_buf1, 1); 
    
    ch_buf2 = 15; //00001111;
    m_alphaDepth = ch_buf1 & ch_buf2;

    if (! ((m_alphaDepth == 0) || (m_alphaDepth == 8)))
        return false;

    if (truecolor)
    {
        m_type = itRGB;
        if (m_pixelDepth == 32)
            m_type = itRGBA;
    }

    if (m_type == itUndefined)
        return false;

    file.seekg(IDLength, std::ios::cur);

    m_pixels = (byte*) malloc(m_width*m_height*(m_pixelDepth/8));

    if (!rle)
        ReadData(file, (char*)m_pixels, m_width*m_height*(m_pixelDepth/8));
    else
    {
        while (CurrentPixel < m_width*m_height -1)
        {
            ReadData(file, (char*)&ch_buf1, 1);
            if ((ch_buf1 & 128) == 128)
            {   // this is an rle packet
                ch_buf2 = (byte)((ch_buf1 & 127) + 1);   // how many pixels are encoded using this packet
                ReadData(file, (char*)buf1, m_pixelDepth/8);
                for (uint i=CurrentPixel; i<CurrentPixel+ch_buf2; i++)
                    for (uint j=0; j<m_pixelDepth/8; j++)
                        m_pixels[i*m_pixelDepth/8+j] = buf1[j];
                CurrentPixel += ch_buf2;
            }
            else
            {   // this is a raw packet
                ch_buf2 = (byte)((ch_buf1 & 127) + 1);
                ReadData(file, (char*)buf1, m_pixelDepth/8*ch_buf2);
                for (uint i=CurrentPixel; i<CurrentPixel+ch_buf2; i++)
                    for (uint j=0; j<m_pixelDepth/8; j++)
                        m_pixels[i*m_pixelDepth/8+j] =  buf1[(i-CurrentPixel)*m_pixelDepth/8+j];
                CurrentPixel += ch_buf2;
            }
        }
    }

    if (TGAReadError != 0)
    {
        Clear();
        return false;
    }
    m_loaded = true;

    file.close(); // close file SJ

    // swap BGR(A) to RGB(A)

    byte temp;
    if ((m_type == itRGB) || (m_type == itRGBA))
        if ((m_pixelDepth == 24) || (m_pixelDepth == 32))
            for (uint i= 0; i<m_width*m_height; i++)
            {
                temp = m_pixels[i*m_pixelDepth/8];
                m_pixels[i*m_pixelDepth/8] = m_pixels[i*m_pixelDepth/8+2];
                m_pixels[i*m_pixelDepth/8+2] = temp;
            }

    return true;
}

void LTGA::SwapRB() {
    byte temp;
    if ((m_type == itRGB) || (m_type == itRGBA))
        if ((m_pixelDepth == 24) || (m_pixelDepth == 32))
            for (uint i= 0; i<m_width*m_height; i++)
            {
                temp = m_pixels[i*m_pixelDepth/8];
                m_pixels[i*m_pixelDepth/8] = m_pixels[i*m_pixelDepth/8+2];
                m_pixels[i*m_pixelDepth/8+2] = temp;
            }
}

struct TGA_HEADER
{
    byte  identsize;          // size of ID field that follows 18 byte header (0 usually)
    byte  colourmaptype;      // type of colour map 0=none, 1=has palette
    byte  imagetype;          // type of image 0=none,1=indexed,2=rgb,3=grey,+8=rle packed

    short colourmapstart;     // first colour map entry in palette
    short colourmaplength;    // number of colours in palette
    byte  colourmapbits;      // number of bits per palette entry 15,16,24,32

    short xstart;             // image x origin
    short ystart;             // image y origin
    short width;              // image width in pixels
    short height;             // image height in pixels
    byte  bits;               // image bits per pixel 8,16,24,32
    byte  descriptor;         // image descriptor bits (vh flip bits)
};


using namespace std;
void LTGA::WriteToFile(const std::string& name) {
  LTGA* pltga = this;
  TGA_HEADER th;

  th.identsize = 0;
  th.colourmaptype = 0;
  th.imagetype = 2;
  th.colourmapstart = 0;
  th.colourmaplength = 0;
  th.colourmapbits = 0;
  th.xstart = 0;
  th.ystart = 0;
  th.width = pltga->GetImageWidth();
  th.height = pltga->GetImageHeight();
  th.bits = pltga->GetPixelDepth();
  th.descriptor = 0x0F & pltga->GetAlphaDepth();

  ofstream os(name.c_str(), ios_base::binary);

  os.write((char*)&th, 3);
  os.write((char*)&th.colourmapstart, 5);
  os.write((char*)&th.xstart, 10);

  //cerr << "wrote header with " << sizeof(th) << " bytes." << endl;

  SwapRB();

  os.write((char*)pltga->GetPixels(), pltga->GetImageWidth()*pltga->GetImageHeight()*(pltga->GetPixelDepth()/8));

  os.close();

  SwapRB();
}



//--------------------------------------------------
void LTGA::Clear()
{
    if (m_pixels)
      free(m_pixels);
    m_pixels = 0;
    m_loaded = false;
    m_width = 0;
    m_height = 0;
    m_pixelDepth = 0;
    m_alphaDepth = 0;
    m_type = itUndefined;
}


//--------------------------------------------------
uint LTGA::GetAlphaDepth()
{
    return m_alphaDepth;
}


//--------------------------------------------------
uint LTGA::GetImageWidth()
{
    return m_width;
}


//--------------------------------------------------
uint LTGA::GetImageHeight()
{
    return m_height;
}


//--------------------------------------------------
uint LTGA::GetPixelDepth()
{
    return m_pixelDepth;
}


//--------------------------------------------------
byte* LTGA::GetPixels()
{
    return m_pixels;
}


//--------------------------------------------------
LImageType LTGA::GetImageType()
{
    return m_type;
}

