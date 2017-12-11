/*****************************************************************************

        InterpFltPhase.hpp
        Copyright (c) 2003 Laurent de Soras

*Tab=3***********************************************************************/



#if defined (rspl_InterpFltPhase_CURRENT_CODEHEADER)
	#error Recursive inclusion of InterpFltPhase code header.
#endif
#define	rspl_InterpFltPhase_CURRENT_CODEHEADER

#if ! defined (rspl_InterpFltPhase_CODEHEADER_INCLUDED)
#define	rspl_InterpFltPhase_CODEHEADER_INCLUDED



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	<cassert>



namespace rspl
{



/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



template <int SC>
InterpFltPhase <SC>::InterpFltPhase ()
{
	_imp [0] = CHK_IMPULSE_NOT_SET;
}



template <int SC>
rspl_FORCEINLINE float	InterpFltPhase <SC>::convolve (const float data_ptr [], float q) const
{
	assert (false);

	return (0);
}



template <>
rspl_FORCEINLINE float	InterpFltPhase <1>::convolve (const float data_ptr [], float q) const
{
	assert (_imp [0] != CHK_IMPULSE_NOT_SET);

	// This way of reordering the convolution operations seems to give the best
	// performances. Actually it may be highly compiler- and architecture-
	// dependent.
	float				c_0;
	float				c_1;
	c_0  = (_imp [0+0] + _dif [0+0] * q) * data_ptr [0+0];
	c_1  = (_imp [0+1] + _dif [0+1] * q) * data_ptr [0+1];
	c_0 += (_imp [0+2] + _dif [0+2] * q) * data_ptr [0+2];
	c_1 += (_imp [0+3] + _dif [0+3] * q) * data_ptr [0+3];
	c_0 += (_imp [4+0] + _dif [4+0] * q) * data_ptr [4+0];
	c_1 += (_imp [4+1] + _dif [4+1] * q) * data_ptr [4+1];
	c_0 += (_imp [4+2] + _dif [4+2] * q) * data_ptr [4+2];
	c_1 += (_imp [4+3] + _dif [4+3] * q) * data_ptr [4+3];
	c_0 += (_imp [8+0] + _dif [8+0] * q) * data_ptr [8+0];
	c_1 += (_imp [8+1] + _dif [8+1] * q) * data_ptr [8+1];
	c_0 += (_imp [8+2] + _dif [8+2] * q) * data_ptr [8+2];
	c_1 += (_imp [8+3] + _dif [8+3] * q) * data_ptr [8+3];
	assert (FIR_LEN == 12);

	const float		sum = c_0 + c_1;

	return (sum);
}



template <>
rspl_FORCEINLINE float	InterpFltPhase <2>::convolve (const float data_ptr [], float q) const
{
	assert (_imp [0] != CHK_IMPULSE_NOT_SET);

	// This way of reordering the convolution operations seems to give the best
	// performances. Actually it may be highly compiler- and architecture-
	// dependent.
	float				c_0;
	float				c_1;
	c_0  = (_imp [ 0+0] + _dif [ 0+0] * q) * data_ptr [ 0+0];
	c_1  = (_imp [ 0+1] + _dif [ 0+1] * q) * data_ptr [ 0+1];
	c_0 += (_imp [ 0+2] + _dif [ 0+2] * q) * data_ptr [ 0+2];
	c_1 += (_imp [ 0+3] + _dif [ 0+3] * q) * data_ptr [ 0+3];
	c_0 += (_imp [ 4+0] + _dif [ 4+0] * q) * data_ptr [ 4+0];
	c_1 += (_imp [ 4+1] + _dif [ 4+1] * q) * data_ptr [ 4+1];
	c_0 += (_imp [ 4+2] + _dif [ 4+2] * q) * data_ptr [ 4+2];
	c_1 += (_imp [ 4+3] + _dif [ 4+3] * q) * data_ptr [ 4+3];
	c_0 += (_imp [ 8+0] + _dif [ 8+0] * q) * data_ptr [ 8+0];
	c_1 += (_imp [ 8+1] + _dif [ 8+1] * q) * data_ptr [ 8+1];
	c_0 += (_imp [ 8+2] + _dif [ 8+2] * q) * data_ptr [ 8+2];
	c_1 += (_imp [ 8+3] + _dif [ 8+3] * q) * data_ptr [ 8+3];
	c_0 += (_imp [12+0] + _dif [12+0] * q) * data_ptr [12+0];
	c_1 += (_imp [12+1] + _dif [12+1] * q) * data_ptr [12+1];
	c_0 += (_imp [12+2] + _dif [12+2] * q) * data_ptr [12+2];
	c_1 += (_imp [12+3] + _dif [12+3] * q) * data_ptr [12+3];
	c_0 += (_imp [16+0] + _dif [16+0] * q) * data_ptr [16+0];
	c_1 += (_imp [16+1] + _dif [16+1] * q) * data_ptr [16+1];
	c_0 += (_imp [16+2] + _dif [16+2] * q) * data_ptr [16+2];
	c_1 += (_imp [16+3] + _dif [16+3] * q) * data_ptr [16+3];
	c_0 += (_imp [20+0] + _dif [20+0] * q) * data_ptr [20+0];
	c_1 += (_imp [20+1] + _dif [20+1] * q) * data_ptr [20+1];
	c_0 += (_imp [20+2] + _dif [20+2] * q) * data_ptr [20+2];
	c_1 += (_imp [20+3] + _dif [20+3] * q) * data_ptr [20+3];
	assert (FIR_LEN == 24);

	const float		sum = c_0 + c_1;

	return (sum);
}



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



}	// namespace rspl



#endif	// rspl_InterpFltPhase_CODEHEADER_INCLUDED

#undef rspl_InterpFltPhase_CURRENT_CODEHEADER



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
