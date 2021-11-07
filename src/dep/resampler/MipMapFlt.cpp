/*****************************************************************************

        MipMapFlt.cpp
        Copyright (c) 2003 Laurent de Soras

--- Legal stuff ---

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*Tab=3***********************************************************************/



#if defined (_MSC_VER)
	#pragma warning (1 : 4130) // "'operator' : logical operation on address of string constant"
	#pragma warning (1 : 4223) // "nonstandard extension used : non-lvalue array converted to pointer"
	#pragma warning (1 : 4705) // "statement has no effect"
	#pragma warning (1 : 4706) // "assignment within conditional expression"
	#pragma warning (4 : 4786) // "identifier was truncated to '255' characters in the debug information"
	#pragma warning (4 : 4800) // "forcing value to bool 'true' or 'false' (performance warning)"
	#pragma warning (4 : 4355) // "'this' : used in base member initializer list"
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	"fnc.h"
#include	"MipMapFlt.h"

#include	<cassert>



namespace rspl
{



/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



MipMapFlt::MipMapFlt ()
:	_table_arr ()
,	_filter ()
,	_len (-1)
,	_add_len_pre (0)
,	_add_len_post (0)
,	_filled_len (0)
,	_nbr_tables (0)
{
	// Nothing
}



/*
==============================================================================
Name: init_sample
Description:
	Provides object with sample information.
	Sets the FIR impulse for the half-band filter. Its length must be odd and
	the impulse should be centered. It is supposed to be symetric.
	This function must be called before anything else. If sample length is not
	0, fill_sample() must be called before using the sample.
Input parameters:
	- len: Length of the sample, in samples. >= 0
	- add_len_pre: length of the data required before a particular interger
		sample position for the interpolation. >= 0. 0 is for "drop-sample"
	- add_len_post: Same as add_len_pre, buf after sample. >= 0.
	- nbr_tables: Number of desired mip-map levels. 1 is just provided data,
		no additionnal level. > 0.
	- imp_ptr: Pointer on impulse data.
	- nbr_taps: Number of taps. > 0 and odd.
Returns: true if more data are needed to fill the sample.
Throws: std::vector related exceptions
==============================================================================
*/

bool	MipMapFlt::init_sample (long len, long add_len_pre, long add_len_post, int nbr_tables, const double imp_ptr [], int nbr_taps)
{
	assert (len >= 0);
	assert (add_len_pre >= 0);
	assert (add_len_post >= 0);
	assert (nbr_tables > 0);
	assert (imp_ptr != 0);
	assert (nbr_taps > 0);
	assert ((nbr_taps & 1) == 1);

	// Store the FIR data
	const int		half_fir_len = (nbr_taps - 1) / 2;
	_filter.resize (half_fir_len + 1);
	for (int pos = 0; pos <= half_fir_len; ++pos)
	{
		_filter [pos] = static_cast <float> (imp_ptr [half_fir_len + pos]);
	}

	const long		filter_sup = static_cast <long> (half_fir_len * 2);

	// Misc variables
	_len = len;
	_add_len_pre = max (add_len_pre, filter_sup);
	_add_len_post = max (add_len_post, filter_sup);
	_filled_len = 0;
	_nbr_tables = nbr_tables;

	// Resize tables
	resize_and_clear_tables ();

	// Just in case of 0 length...
	return (check_sample_and_build_mip_map ());
}



/*
==============================================================================
Name: fill_sample
Description:
	Provide sample data. Must be called after init_sample() and before using
	data. No need to call the function if the sample length was 0. There may
	be multiple calls to this function to fill data by sucessive blocks. The
	total length must be the announced sample length, before the sample is
	considered as "ready".
Input parameters:
	- data_ptr: Pointer on sample data.
	- nbr_spl: Number of samples to load.
Returns: true if more data are needed.
Throws: Nothing.
==============================================================================
*/

bool	MipMapFlt::fill_sample (const float data_ptr [], long nbr_spl)
{
	assert (_len >= 0);
	assert (_nbr_tables > 0);
	assert (_table_arr.size () > 0);
	assert (data_ptr != 0);
	assert (nbr_spl > 0);
	assert (nbr_spl <= _len - _filled_len);

	TableData::SplData &	sample = _table_arr [0]._data;
	const long		offset = _add_len_pre + _filled_len;
	const long		work_len = min (nbr_spl, _len - _filled_len);

	for (long pos = 0; pos < work_len; ++pos)
	{
		sample [offset + pos] = data_ptr [pos];
	}
	_filled_len += work_len;

	return (check_sample_and_build_mip_map ());
}



/*
==============================================================================
Name: clear_sample
Description:
	Remove the loaded sample and free the allocated memory. Can be called at
	any time.
Throws: std::vector related exceptions
==============================================================================
*/

void	MipMapFlt::clear_sample ()
{
	_len = -1;
	_add_len_pre = 0;
	_add_len_post = 0;
	_filled_len = 0;
	_nbr_tables = 0;

	// Free allocated memory
	TableArr ().swap (_table_arr);
	SplData ().swap (_filter);
}



/*
==============================================================================
Name: is_ready
Description:
	Indicates whether the object is ready to use (sample fully loaded) or not.
Returns:
	true if object is ready, false otherwise.
Throws: Nothing
==============================================================================
*/

bool	MipMapFlt::is_ready () const
{
	bool				ready_flag = (_len >= 0);
	ready_flag &= (_nbr_tables > 0);
	ready_flag &= (_filled_len == _len);

	return (ready_flag);
}



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



void	MipMapFlt::resize_and_clear_tables ()
{
	_table_arr.resize (_nbr_tables);
	for (int table_cnt = 0; table_cnt < _nbr_tables; ++table_cnt)
	{
		const long		lev_len = get_lev_len (table_cnt);
		const long		table_len = _add_len_pre + lev_len + _add_len_post;

		TableData &		table = _table_arr [table_cnt];
		TableData::SplData (table_len, 0).swap (table._data);
		table._data_ptr = &table._data [_add_len_pre];
	}
}



bool	MipMapFlt::check_sample_and_build_mip_map ()
{
	if (_filled_len == _len)
	{
		for (int level = 1; level < _nbr_tables; ++level)
		{
			build_mip_map_level (level);
		}

		// Release the filter, we don't need it anymore.
		SplData ().swap (_filter);
	}

	return (_filled_len < _len);
}



void	MipMapFlt::build_mip_map_level (int level)
{
	assert (level > 0);
	assert (level < _nbr_tables);
	assert (_table_arr.size () > 0);

	TableData::SplData &	ref_spl = _table_arr [level - 1]._data;
	TableData::SplData &	new_spl = _table_arr [level    ]._data;

	const long		lev_len = get_lev_len (level);

	// We need to care about residual side ripples from previous level
	// filtering, at the end and at the beginning of the sample. Their
	// maximum size is filter_half_len.
	const long		filter_half_len = _filter.size () - 1;
	const long		filter_quarter_len = filter_half_len / 2;	// Rounds toward -oo
	const long		end_pos = lev_len + filter_quarter_len;

	for (long pos = -filter_quarter_len; pos < end_pos; ++pos)
	{
		// Convolution
		const long		pos_ref = _add_len_pre + pos * 2;
		const float		val = filter_sample (ref_spl, pos_ref);

		// Store the result
		const long		pos_new = _add_len_pre + pos;
		assert (pos_new >= 0);
		assert (pos_new < static_cast <long> (new_spl.size ()));
		new_spl [pos_new] = val;
	}
}



float	MipMapFlt::filter_sample (const TableData::SplData &table, long pos) const
{
	assert (&table != 0);
	const long		filter_half_len = _filter.size () - 1;

	assert (pos - filter_half_len>= 0);
	assert (pos + filter_half_len < static_cast <long> (table.size ())); 

	float				sum = table [pos] * _filter [0];
	for (long fir_pos = 1; fir_pos <= filter_half_len; ++fir_pos)
	{
		const float		two_spl =   table [pos - fir_pos]
							          + table [pos + fir_pos];
		sum += two_spl * _filter [fir_pos];
	}

	return (sum);
}



}	// namespace rspl



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
