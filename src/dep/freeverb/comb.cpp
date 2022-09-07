// Comb filter implementation
//
// Written by Jezar at Dreampoint, June 2000
// http://www.dreampoint.co.uk
// This code is public domain

#include "comb.hpp"

comb::comb()
{
	filterstore = 0;
	bufsize = 0;
	bufidx = 0;
	buffer = 0;
	allocated = false;
}

comb::~comb()
{
	filterstore = 0;
	bufidx = 0;
	if (allocated) delete[] buffer;
}


void comb::setbuffer(float *buf, int size)
{
	buffer = buf;
	bufidx = 0;
	bufsize = size;
	filterstore = 0;
	allocated = false;
}

void comb::changebuffer(float *buf, int size)
	{
		if (allocated) {delete[] buffer;}
		buffer = new float[size];
		bufsize = size;
		bufidx = 0;
		filterstore = 0;
		allocated = true;
}

void comb::mute()
{
	for (int i=0; i<bufsize; i++)
		buffer[i]=0;
	filterstore = 0;
}

void comb::setdamp(float val)
{
	damp1 = val;
	damp2 = 1-val;
}

float comb::getdamp()
{
	return damp1;
}

void comb::setfeedback(float val)
{
	feedback = val;
}

float comb::getfeedback()
{
	return feedback;
}

// ends
