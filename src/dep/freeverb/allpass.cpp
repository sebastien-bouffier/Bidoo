// Allpass filter implementation
//
// Written by Jezar at Dreampoint, June 2000
// http://www.dreampoint.co.uk
// This code is public domain

#include "allpass.hpp"

allpass::allpass()
{
	buffer = 0;
	bufidx = 0;
	bufsize = 0;
	allocated = false;
};

allpass::~allpass()
{
	if (allocated) delete[] buffer;
};

void allpass::setbuffer(float *buf, int size)
{
	buffer = buf;
	bufidx = 0;
	bufsize = size;
	allocated = false;
}

void allpass::changebuffer(float *buf, int size)
	{
		if (allocated) {delete[] buffer;}
		buffer = new float[size];
		bufsize = size;
		bufidx = 0;
		allocated = true;
}

void allpass::mute()
{
	for (int i=0; i<bufsize; i++)
		buffer[i]=0;
}

void allpass::setfeedback(float val)
{
	feedback = val;
}

float allpass::getfeedback()
{
	return feedback;
}

//ends
