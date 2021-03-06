/*----------------------------------------------------------------------
  PuReMD - Purdue ReaxFF Molecular Dynamics Program
  
  Copyright (2010) Purdue University
  Hasan Metin Aktulga, haktulga@cs.purdue.edu
  Joseph Fogarty, jcfogart@mail.usf.edu
  Sagar Pandit, pandit@usf.edu
  Ananth Y Grama, ayg@cs.purdue.edu

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of 
  the License, or (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
  See the GNU General Public License for more details:
  <http://www.gnu.org/licenses/>.
  ----------------------------------------------------------------------*/

#include "reaxc_types.h"
#if defined(PURE_REAX)
#include "bonds.h"
#include "bond_orders.h"
#include "list.h"
#include "tool_box.h"
#include "vector.h"
#elif defined(LAMMPS_REAX)
#include "reaxc_bonds.h"
#include "reaxc_bond_orders.h"
#include "reaxc_list.h"
#include "reaxc_tool_box.h"
#include "reaxc_vector.h"
#endif


void Bonds( reax_system *system, control_params *control, 
	    simulation_data *data, storage *workspace, reax_list **lists, 
	    output_controls *out_control )
{
  int i, j, pj, natoms;
  int start_i, end_i;
  int type_i, type_j;
  real ebond, pow_BOs_be2, exp_be12, CEbo;
  real gp3, gp4, gp7, gp10, gp37;
  real exphu, exphua1, exphub1, exphuov, hulpov, estriph;
  real decobdbo, decobdboua, decobdboub;
  single_body_parameters *sbp_i, *sbp_j;
  two_body_parameters *twbp;
  bond_order_data *bo_ij;
  reax_list *bonds;

  bonds = (*lists) + BONDS;
  gp3 = system->reax_param.gp.l[3];
  gp4 = system->reax_param.gp.l[4];
  gp7 = system->reax_param.gp.l[7];
  gp10 = system->reax_param.gp.l[10];
  gp37 = (int) system->reax_param.gp.l[37];
  natoms = system->n;

  for( i = 0; i < natoms; ++i ) {
    start_i = Start_Index(i, bonds);
    end_i = End_Index(i, bonds);

    for( pj = start_i; pj < end_i; ++pj ) {
      j = bonds->select.bond_list[pj].nbr;
      
      if( system->my_atoms[i].orig_id <= system->my_atoms[j].orig_id ) {
	/* set the pointers */
	type_i = system->my_atoms[i].type;
	type_j = system->my_atoms[j].type;
	sbp_i = &( system->reax_param.sbp[type_i] );
	sbp_j = &( system->reax_param.sbp[type_j] );
	twbp = &( system->reax_param.tbp[type_i][type_j] );
	bo_ij = &( bonds->select.bond_list[pj].bo_data );
	      
	/* calculate the constants */
	pow_BOs_be2 = pow( bo_ij->BO_s, twbp->p_be2 );
	exp_be12 = exp( twbp->p_be1 * ( 1.0 - pow_BOs_be2 ) );
	CEbo = -twbp->De_s * exp_be12 * 
	  ( 1.0 - twbp->p_be1 * twbp->p_be2 * pow_BOs_be2 );
	      
	/* calculate the Bond Energy */
	data->my_en.e_bond += ebond = 
	  -twbp->De_s * bo_ij->BO_s * exp_be12 
	  -twbp->De_p * bo_ij->BO_pi 
	  -twbp->De_pp * bo_ij->BO_pi2;
	      
	/* calculate derivatives of Bond Orders */
	bo_ij->Cdbo += CEbo;
	bo_ij->Cdbopi -= (CEbo + twbp->De_p);
	bo_ij->Cdbopi2 -= (CEbo + twbp->De_pp);
	      
#ifdef TEST_ENERGY
	//fprintf( out_control->ebond, "%6d%6d%24.15e%24.15e%24.15e\n",
	fprintf( out_control->ebond, "%6d%6d%12.4f%12.4f%12.4f\n",
		 system->my_atoms[i].orig_id, 
		 system->my_atoms[j].orig_id, 
		 bo_ij->BO, ebond, data->my_en.e_bond );
#endif
#ifdef TEST_FORCES
	Add_dBO( system, lists, i, pj, CEbo, workspace->f_be );
	Add_dBOpinpi2( system, lists, i, pj, 
		       -(CEbo + twbp->De_p), -(CEbo + twbp->De_pp), 
		       workspace->f_be, workspace->f_be );
#endif
	/* Stabilisation terminal triple bond */
	if( bo_ij->BO >= 1.00 ) {
	  if( gp37 == 2 ||
	      (sbp_i->mass == 12.0000 && sbp_j->mass == 15.9990) || 
	      (sbp_j->mass == 12.0000 && sbp_i->mass == 15.9990) ) {
	    exphu = exp( -gp7 * SQR(bo_ij->BO - 2.50) );
	    exphua1 = exp(-gp3 * (workspace->total_bond_order[i]-bo_ij->BO));
	    exphub1 = exp(-gp3 * (workspace->total_bond_order[j]-bo_ij->BO));
	    exphuov = exp(gp4 * (workspace->Delta[i] + workspace->Delta[j]));
	    hulpov = 1.0 / (1.0 + 25.0 * exphuov);
	    
	    estriph = gp10 * exphu * hulpov * (exphua1 + exphub1);
	    data->my_en.e_bond += estriph;
	    
	    decobdbo = gp10 * exphu * hulpov * (exphua1 + exphub1) *
	      ( gp3 - 2.0 * gp7 * (bo_ij->BO-2.50) );
	    decobdboua = -gp10 * exphu * hulpov *
	      (gp3*exphua1 + 25.0*gp4*exphuov*hulpov*(exphua1+exphub1));
	    decobdboub = -gp10 * exphu * hulpov *
	      (gp3*exphub1 + 25.0*gp4*exphuov*hulpov*(exphua1+exphub1));
	    
	    bo_ij->Cdbo += decobdbo;
	    workspace->CdDelta[i] += decobdboua;
	    workspace->CdDelta[j] += decobdboub;
#ifdef TEST_ENERGY
	    //fprintf( out_control->ebond, 
	    //  "%6d%6d%24.15e%24.15e%24.15e%24.15e\n",
	    //  system->my_atoms[i].orig_id, system->my_atoms[j].orig_id,
	    //  estriph, decobdbo, decobdboua, decobdboub );
#endif
#ifdef TEST_FORCES
	    Add_dBO( system, lists, i, pj, decobdbo, workspace->f_be );
	    Add_dDelta( system, lists, i, decobdboua, workspace->f_be );
	    Add_dDelta( system, lists, j, decobdboub, workspace->f_be );
#endif
	  }
	}
      }
    }
  }
}
