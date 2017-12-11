/*****************************************************************************

        MipMapFlt.h
        Copyright (c) 2003 Laurent de Soras

This is the sample data container. It parses original data, makes MIP-maps
with the help of the provided filter and store them into memory.

How to use this class:

1. Build an instance of it.

2. Call init_sample()

3. Call fill_sample() as many times it is needed to complete the sample data.

4. You can now use the other functions.

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



#if ! defined (rspl_MipMapFlt_HEADER_INCLUDED)
#define	rspl_MipMapFlt_HEADER_INCLUDED

#if defined (_MSC_VER)
	#pragma once
	#pragma warning (4 : 4250) // "Inherits via dominance."
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include	<vector>



namespace rspl
{



class MipMapFlt
{

/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

public:

						MipMapFlt ();
	virtual			~MipMapFlt () {}

	bool				init_sample (long len, long add_len_pre, long add_len_post, int nbr_tables, const double imp_ptr [], int nbr_taps);
	bool				fill_sample (const float data_ptr [], long nbr_spl);
	void				clear_sample ();
	bool				is_ready () const;

	inline long		get_sample_len () const;
	inline long		get_lev_len (int level) const;
	inline const int
						get_nbr_tables () const;
	inline const float *
						use_table (int table) const;



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

protected:



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

	class TableData
	{
	public:
		typedef	std::vector <float>	SplData;
		SplData			_data;
		float *			_data_ptr;
	};

	typedef	TableData::SplData	SplData;
	typedef	std::vector <TableData>	TableArr;

	void				resize_and_clear_tables ();
	bool				check_sample_and_build_mip_map ();
	void				build_mip_map_level (int level);
	float				filter_sample (const TableData::SplData &table, long pos) const;

	TableArr			_table_arr;
	SplData			_filter;			// First stored coef is the "center".
	long				_len;				// >= 0. < 0: not initialised
	long				_add_len_pre;	// Length of additional data before sample. >= 0
	long				_add_len_post;	// Length of additional data after sample. >= 0
	long				_filled_len;	// Number of samples already submitted. >= 0.
	int				_nbr_tables;	// > 0. <= 0: not initialised.



/*\\\ FORBIDDEN MEMBER FUNCTIONS \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

private:

						MipMapFlt (const MipMapFlt &other);
	MipMapFlt &		operator = (const MipMapFlt &other);
	bool				operator == (const MipMapFlt &other);
	bool				operator != (const MipMapFlt &other);

};	// class MipMapFlt



}	// namespace rspl



#include	"MipMapFlt.hpp"



#endif	// rspl_MipMapFlt_HEADER_INCLUDED



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
