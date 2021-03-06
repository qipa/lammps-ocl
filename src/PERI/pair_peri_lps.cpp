/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under 
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Contributing author: Mike Parks (SNL)
------------------------------------------------------------------------- */

#include "math.h"
#include "stdlib.h"
#include "string.h"
#include "pair_peri_lps.h"  			
#include "atom.h"
#include "domain.h"
#include "lattice.h"
#include "force.h"
#include "update.h"
#include "modify.h"
#include "fix.h"
#include "fix_peri_neigh.h"
#include "comm.h"
#include "neighbor.h"
#include "neigh_list.h"
#include "memory.h"
#include "error.h"
#include "update.h"                       

using namespace LAMMPS_NS;

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

/* ---------------------------------------------------------------------- */

PairPeriLPS::PairPeriLPS(LAMMPS *lmp) : Pair(lmp)			
{
  for (int i = 0; i < 6; i++) virial[i] = 0.0;
  no_virial_compute=1;

  ifix_peri = -1;

  nmax = 0;
  s0_new = NULL;
  theta = NULL;				

  bulkmodulus = NULL;			
  shearmodulus = NULL;			
  s00 = alpha = NULL;                    
  cut = NULL;

  // set comm size needed by this Pair
  // comm_reverse not needed

  comm_forward = 1;  // for passing dilatation (theta)
}

/* ---------------------------------------------------------------------- */

PairPeriLPS::~PairPeriLPS()						
{
  if (ifix_peri >= 0) modify->delete_fix("PERI_NEIGH");

  if (allocated) {
    memory->destroy(setflag);
    memory->destroy(cutsq);
    memory->destroy(bulkmodulus);		
    memory->destroy(shearmodulus); 		
    memory->destroy(s00); 		
    memory->destroy(alpha); 		
    memory->destroy(cut);
    memory->destroy(theta);					
    memory->destroy(s0_new);					
  }
}

/* ---------------------------------------------------------------------- */

void PairPeriLPS::compute(int eflag, int vflag)				
{
  int i,j,ii,jj,inum,jnum,itype,jtype;
  double xtmp,ytmp,ztmp,delx,dely,delz;
  double xtmp0,ytmp0,ztmp0,delx0,dely0,delz0,rsq0;
  double rsq,r,dr,rk,evdwl,fpair,fbond;
  int *ilist,*jlist,*numneigh,**firstneigh;
  double d_ij,delta,stretch;                            

  evdwl = 0.0;
  if (eflag || vflag) ev_setup(eflag,vflag);
  else evflag = vflag_fdotr = eflag_global = eflag_atom = 0;

  double **f = atom->f;
  double **x = atom->x;
  int *type = atom->type;
  int nlocal = atom->nlocal;

  double *vfrac = atom->vfrac;
  double *s0 = atom->s0;
  double **x0 = atom->x0;
  double **r0   = ((FixPeriNeigh *) modify->fix[ifix_peri])->r0;
  int **partner = ((FixPeriNeigh *) modify->fix[ifix_peri])->partner;
  int *npartner = ((FixPeriNeigh *) modify->fix[ifix_peri])->npartner;
  double *wvolume = ((FixPeriNeigh *) modify->fix[ifix_peri])->wvolume;

  // lc = lattice constant
  // init_style guarantees it's the same in x, y, and z

  double lc = domain->lattice->xlattice;
  double half_lc = 0.5*lc;
  double vfrac_scale = 1.0;

  // short-range forces

  int nall = atom->nlocal + atom->nghost;
  int newton_pair = force->newton_pair;
  int periodic = domain->xperiodic || domain->yperiodic || domain->zperiodic;

  inum = list->inum;
  ilist = list->ilist;
  numneigh = list->numneigh;
  firstneigh = list->firstneigh;

  double dt = update->dt;                   

  // loop over neighbors of my atoms
  // need minimg() for x0 difference since not ghosted

  for (ii = 0; ii < inum; ii++) {
    i = ilist[ii];
    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    xtmp0 = x0[i][0];
    ytmp0 = x0[i][1];
    ztmp0 = x0[i][2];
    itype = type[i];
    jlist = firstneigh[i];
    jnum = numneigh[i];

    for (jj = 0; jj < jnum; jj++) {
      j = jlist[jj];
      j %= nall;
 
      delx = xtmp - x[j][0];
      dely = ytmp - x[j][1];
      delz = ztmp - x[j][2];

      rsq = delx*delx + dely*dely + delz*delz;
      delx0 = xtmp0 - x0[j][0];
      dely0 = ytmp0 - x0[j][1];
      delz0 = ztmp0 - x0[j][2];
      if (periodic) domain->minimum_image(delx0,dely0,delz0);
      rsq0 = delx0*delx0 + dely0*dely0 + delz0*delz0;
      jtype = type[j];
 
      r = sqrt(rsq);

      // short-range interaction distance based on initial particle position
      // 0.9 and 1.35 are constants

      d_ij = MIN(0.9*sqrt(rsq0),1.35*lc);

      // short-range contact forces
      // 15 is constant taken from the EMU Theory Manual
      // Silling, 12 May 2005, p 18

      if (r < d_ij) {
        dr = r - d_ij;

        // kshort based upon short-range force constant
	// of the bond-based theory used in PMB model

        double kshort = (15.0 * 18.0 * bulkmodulus[itype][itype]) /
	  (3.141592653589793 * cutsq[itype][jtype] * cutsq[itype][jtype]);
        rk = (kshort * vfrac[j]) * (dr / sqrt(cutsq[itype][jtype]));

        if (r > 0.0) fpair = -(rk/r);
        else fpair = 0.0;

        f[i][0] += delx*fpair;
        f[i][1] += dely*fpair;
        f[i][2] += delz*fpair;
        if (newton_pair || j < nlocal) {
          f[j][0] -= delx*fpair;
          f[j][1] -= dely*fpair;
          f[j][2] -= delz*fpair;
        }

        if (eflag) evdwl = 0.5*rk*dr;
	if (evflag) ev_tally(i,j,nlocal,newton_pair,evdwl,0.0,fpair*vfrac[i],delx,dely,delz);
      }
    }
  }

  // grow bond forces array if necessary

  if (atom->nmax > nmax) {
    memory->destroy(s0_new);				
    memory->destroy(theta);				
    nmax = atom->nmax;
    memory->create(s0_new,nmax,"pair:s0_new");
    memory->create(theta,nmax,"pair:theta");
  }

  // Compute the dilatation on each particle				
  compute_dilatation();						

  // communicate dilatation (theta) of each particle	
  comm->forward_comm_pair(this);
  // communicate wighted volume (wvolume) upon every reneighbor
  if (neighbor->ago == 0)
    comm->forward_comm_fix(modify->fix[ifix_peri]);

  // Volume-dependent part of the energy
  for (i = 0; i < nlocal; i++) {   
    itype = type[i];
    if (eflag) {
      if (eflag_global)
	eng_vdwl += 0.5 * bulkmodulus[itype][itype] * (theta[i] * theta[i]);
      if (eflag_atom)
	eatom[i] += 0.5 * bulkmodulus[itype][itype] * (theta[i] * theta[i]);
    }
  }

  // loop over my particles and their partners
  // partner list contains all bond partners, so I-J appears twice
  // if bond already broken, skip this partner
  // first = true if this is first neighbor of particle i

  bool first;
  double omega_minus, omega_plus;

  for (i = 0; i < nlocal; i++) {
    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    xtmp0 = x0[i][0];			
    ytmp0 = x0[i][1];		
    ztmp0 = x0[i][2];			
    itype = type[i];
    jnum = npartner[i];
    first = true;

    for (jj = 0; jj < jnum; jj++) {
      if (partner[i][jj] == 0) continue;
      j = atom->map(partner[i][jj]);

      // check if lost a partner without first breaking bond

      if (j < 0) {
        partner[i][jj] = 0;
        continue;
      }

      // compute force density, add to PD equation of motion

      delx = xtmp - x[j][0];
      dely = ytmp - x[j][1];
      delz = ztmp - x[j][2];
      if (periodic) domain->minimum_image(delx,dely,delz);
      rsq = delx*delx + dely*dely + delz*delz;
      delx0 = xtmp0 - x0[j][0];						
      dely0 = ytmp0 - x0[j][1];						
      delz0 = ztmp0 - x0[j][2];						
      if (periodic) domain->minimum_image(delx0,dely0,delz0);   
      jtype = type[j];
      delta = sqrt(cutsq[itype][jtype]);
      r = sqrt(rsq);
      dr = r - r0[i][jj];

      // avoid roundoff errors

      if (fabs(dr) < 2.2204e-016) dr = 0.0;

      // scale vfrac[j] if particle j near the horizon

      if ((fabs(r0[i][jj] - delta)) <= half_lc)
        vfrac_scale = (-1.0/(2*half_lc))*(r0[i][jj]) + 
	  (1.0 + ((delta - half_lc)/(2*half_lc) ) );
      else vfrac_scale = 1.0;

      omega_plus  = influence_function(-1.0*delx0,-1.0*dely0,-1.0*delz0);
      omega_minus = influence_function(delx0,dely0,delz0);
      rk = ( (3.0 * bulkmodulus[itype][itype]) - 
	     (5.0 * shearmodulus[itype][itype]) ) * vfrac[j] * vfrac_scale * 
	( (omega_plus * theta[i] / wvolume[i]) + 
	  ( omega_minus * theta[j] / wvolume[j] ) ) * r0[i][jj]; 
      rk +=  15.0 * ( shearmodulus[itype][itype] * vfrac[j] * vfrac_scale ) *
	( (omega_plus / wvolume[i]) + (omega_minus / wvolume[j]) ) * dr; 

      if (r > 0.0) fbond = -(rk/r);
      else fbond = 0.0;

      f[i][0] += delx*fbond;
      f[i][1] += dely*fbond;
      f[i][2] += delz*fbond;

      // since I-J is double counted, set newton off & use 1/2 factor and I,I 

      double deviatoric_extension = dr - (theta[i]* r0[i][jj] / 3.0);
      if (eflag) evdwl = 0.5 * 15 * (shearmodulus[itype][itype]/wvolume[i]) * 
		   omega_plus*(deviatoric_extension * deviatoric_extension) *
		   vfrac[j] * vfrac_scale;
      if (evflag) ev_tally(i,i,nlocal,0,0.5*evdwl,0.0,0.5*fbond*vfrac[i],delx,dely,delz);

      // find stretch in bond I-J and break if necessary
      // use s0 from previous timestep

      stretch = dr / r0[i][jj];
      if (stretch > MIN(s0[i],s0[j])) partner[i][jj] = 0;

      // update s0 for next timestep

      if (first)
         s0_new[i] = s00[itype][jtype] - (alpha[itype][jtype] * stretch);
      else
         s0_new[i] = MAX(s0_new[i],
                         s00[itype][jtype] - (alpha[itype][jtype] * stretch));

      first = false;
    }
  }

  // store new s0
  for (i = 0; i < nlocal; i++) s0[i] = s0_new[i];

}

/* ----------------------------------------------------------------------
   allocate all arrays
------------------------------------------------------------------------- */

void PairPeriLPS::allocate()					
{
  allocated = 1;
  int n = atom->ntypes;

  memory->create(setflag,n+1,n+1,"pair:setflag");
  for (int i = 1; i <= n; i++)
    for (int j = i; j <= n; j++)
      setflag[i][j] = 0;

  memory->create(cutsq,n+1,n+1,"pair:cutsq");
  memory->create(bulkmodulus,n+1,n+1,"pair:bulkmodulus");
  memory->create(shearmodulus,n+1,n+1,"pair:shearmodulus");
  memory->create(s00,n+1,n+1,"pair:s00");		
  memory->create(alpha,n+1,n+1,"pair:alpha");		
  memory->create(cut,n+1,n+1,"pair:cut");
}

/* ----------------------------------------------------------------------
   global settings
------------------------------------------------------------------------- */

void PairPeriLPS::settings(int narg, char **arg)		
{
  if (narg) error->all("Illegal pair_style command");
}

/* ----------------------------------------------------------------------
   set coeffs for one or more type pairs
------------------------------------------------------------------------- */

void PairPeriLPS::coeff(int narg, char **arg)			
{
  if (narg != 7) error->all("Incorrect args for pair coefficients"); 	
  if (!allocated) allocate();

  int ilo,ihi,jlo,jhi;
  force->bounds(arg[0],atom->ntypes,ilo,ihi);
  force->bounds(arg[1],atom->ntypes,jlo,jhi);

  double bulkmodulus_one = atof(arg[2]);	
  double shearmodulus_one = atof(arg[3]);		
  double cut_one = atof(arg[4]);			
  double s00_one = atof(arg[5]);			
  double alpha_one = atof(arg[6]);			

  int count = 0;
  for (int i = ilo; i <= ihi; i++) {
    for (int j = MAX(jlo,i); j <= jhi; j++) {
      bulkmodulus[i][j] = bulkmodulus_one;		
      shearmodulus[i][j] = shearmodulus_one;		
      cut[i][j] = cut_one;
      s00[i][j] = s00_one;
      alpha[i][j] = alpha_one;
      setflag[i][j] = 1;
      count++;
    }
  }

  if (count == 0) error->all("Incorrect args for pair coefficients");
}

/* ----------------------------------------------------------------------
   init for one type pair i,j and corresponding j,i
------------------------------------------------------------------------- */

double PairPeriLPS::init_one(int i, int j)			
{
  if (setflag[i][j] == 0) error->all("All pair coeffs are not set");

  bulkmodulus[j][i] = bulkmodulus[i][j];		
  shearmodulus[j][i] = shearmodulus[i][j];		
  s00[j][i] = s00[i][j];		
  alpha[j][i] = alpha[i][j];		

  return cut[i][j];
}

/* ----------------------------------------------------------------------
   init specific to this pair style
------------------------------------------------------------------------- */

void PairPeriLPS::init_style() 				
{
  // error checks

  if (atom->map_style == 0) 
    error->all("Pair peri requires an atom map, see atom_modify");

  if (atom->style_match("peri") == 0)
    error->all("Pair style peri_lps requires atom style peri");		

  if (domain->lattice == NULL)
    error->all("Pair peri requires a lattice be defined");
  if (domain->lattice->xlattice != domain->lattice->ylattice || 
      domain->lattice->xlattice != domain->lattice->zlattice || 
      domain->lattice->ylattice != domain->lattice->zlattice)
    error->all("Pair peri lattice is not identical in x, y, and z");

  // if first init, create Fix needed for storing fixed neighbors

  if (ifix_peri == -1) {
    char **fixarg = new char*[3];
    fixarg[0] = (char *) "PERI_NEIGH";
    fixarg[1] = (char *) "all";
    fixarg[2] = (char *) "PERI_NEIGH";
    modify->add_fix(3,fixarg);
    delete [] fixarg;
  }

  // find associated PERI_NEIGH fix that must exist
  // could have changed locations in fix list since created

  for (int i = 0; i < modify->nfix; i++)
    if (strcmp(modify->fix[i]->style,"PERI_NEIGH") == 0) ifix_peri = i;
  if (ifix_peri == -1) error->all("Fix peri neigh does not exist");

  int irequest = neighbor->request(this);
}

/* ----------------------------------------------------------------------
  proc 0 writes to restart file
------------------------------------------------------------------------- */

void PairPeriLPS::write_restart(FILE *fp)		
{
  int i,j;
  for (i = 1; i <= atom->ntypes; i++)
    for (j = i; j <= atom->ntypes; j++) {
      fwrite(&setflag[i][j],sizeof(int),1,fp);
      if (setflag[i][j]) {
	fwrite(&bulkmodulus[i][j],sizeof(double),1,fp); 	
	fwrite(&shearmodulus[i][j],sizeof(double),1,fp);	
	fwrite(&s00[i][j],sizeof(double),1,fp);	
	fwrite(&alpha[i][j],sizeof(double),1,fp);	
	fwrite(&cut[i][j],sizeof(double),1,fp);
      }
    }
}

/* ----------------------------------------------------------------------
  proc 0 reads from restart file, bcasts
------------------------------------------------------------------------- */

void PairPeriLPS::read_restart(FILE *fp)		
{
  allocate();

  int i,j;
  int me = comm->me;
  for (i = 1; i <= atom->ntypes; i++)
    for (j = i; j <= atom->ntypes; j++) {
      if (me == 0) fread(&setflag[i][j],sizeof(int),1,fp);
      MPI_Bcast(&setflag[i][j],1,MPI_INT,0,world);
      if (setflag[i][j]) {
	if (me == 0) {
	  fread(&bulkmodulus[i][j],sizeof(double),1,fp);		
	  fread(&shearmodulus[i][j],sizeof(double),1,fp);		
	  fread(&s00[i][j],sizeof(double),1,fp);                        
	  fread(&alpha[i][j],sizeof(double),1,fp);                        
	  fread(&cut[i][j],sizeof(double),1,fp);
	}
	MPI_Bcast(&bulkmodulus[i][j],1,MPI_DOUBLE,0,world);		
	MPI_Bcast(&shearmodulus[i][j],1,MPI_DOUBLE,0,world);		
	MPI_Bcast(&s00[i][j],1,MPI_DOUBLE,0,world);                      
	MPI_Bcast(&alpha[i][j],1,MPI_DOUBLE,0,world);                      
	MPI_Bcast(&cut[i][j],1,MPI_DOUBLE,0,world);
      }
    }
}

/* ---------------------------------------------------------------------- */

double PairPeriLPS::single(int i, int j, int itype, int jtype,
			   double rsq, double factor_coul, double factor_lj,
			   double &fforce)
{
  double delx0,dely0,delz0,rsq0;
  double d_ij,r,dr,rk,vfrac_scale;

  double *vfrac = atom->vfrac;
  double **x0 = atom->x0;
  double **r0   = ((FixPeriNeigh *) modify->fix[ifix_peri])->r0;
  int **partner = ((FixPeriNeigh *) modify->fix[ifix_peri])->partner;
  int *npartner = ((FixPeriNeigh *) modify->fix[ifix_peri])->npartner;
  double *wvolume = ((FixPeriNeigh *) modify->fix[ifix_peri])->wvolume;

  double lc = domain->lattice->xlattice;
  double half_lc = 0.5*lc;

  double kshort;				
 
  delx0 = x0[i][0] - x0[j][0];
  dely0 = x0[i][1] - x0[j][1];
  delz0 = x0[i][2] - x0[j][2];
  int periodic = domain->xperiodic || domain->yperiodic || domain->zperiodic;
  if (periodic) domain->minimum_image(delx0,dely0,delz0);
  rsq0 = delx0*delx0 + dely0*dely0 + delz0*delz0;

  d_ij = MIN(0.9*sqrt(rsq0),1.35*lc);
  r = sqrt(rsq);

  double energy = 0.0;
  fforce = 0.0;

  if (r < d_ij) {
    dr = r - d_ij;
    // kshort resembles short-range force constant of bond-based theory in 3d
    kshort = (15.0 * 18.0 * bulkmodulus[itype][itype]) /
             ( 3.141592653589793 * cutsq[itype][jtype] * cutsq[itype][jtype]);
    rk = ( kshort * vfrac[j]) * (dr / sqrt(cutsq[itype][jtype]));
    if (r > 0.0) fforce += -(rk/r);
    energy += 0.5*rk*dr;	
  }
 
   if (atom->nmax > nmax) {
    memory->destroy(theta);
    nmax = atom->nmax;
    memory->create(theta,nmax,"pair:theta");
  }

  // Compute the dilatation on each particle			
  compute_dilatation();					

  // communicate dilatation (theta) of each particle
  comm->forward_comm_pair(this);
  // communicate wighted volume (wvolume) upon every reneighbor
  if (neighbor->ago == 0)
    comm->forward_comm_fix(modify->fix[ifix_peri]);

  double omega_plus, omega_minus;               

  int jnum = npartner[i];
  for (int jj = 0; jj < jnum; jj++) {
    if (partner[i][jj] == 0) continue;
    if (j < 0) continue;
    if (j == atom->map(partner[i][jj])) {
      dr = r - r0[i][jj];
      if (fabs(dr) < 2.2204e-016) dr = 0.0;

      // scale vfrac[j] if particle j near the horizon

      if ( (fabs(r0[i][jj] - sqrt(cutsq[itype][jtype]))) <= half_lc)
	vfrac_scale = (-1.0/(2*half_lc))*(r0[i][jj]) + 
	  (1.0 + ((sqrt(cutsq[itype][jtype]) - half_lc)/(2*half_lc)));
      else vfrac_scale = 1.0;

      omega_plus  = influence_function(-1.0*delx0,-1.0*dely0,-1.0*delz0);
      omega_minus = influence_function(delx0,dely0,delz0);	
      rk = (3.0* bulkmodulus[itype][itype] -5.0 * shearmodulus[itype][itype]) *
	vfrac[j] * vfrac_scale  * ( (omega_plus * theta[i] / wvolume[i]) + 
				    (omega_minus * theta[j] / wvolume[j])) * 
	r0[i][jj];
      rk +=  15.0 * ( shearmodulus[itype][itype] * vfrac[j] * vfrac_scale ) * 
	( (omega_plus / wvolume[i]) + (omega_minus / wvolume[j]) ) * dr; 

      if (r > 0.0) fforce += -(rk/r);
        energy += 0.5 * 15 * (shearmodulus[itype][itype]/wvolume[i]) * 
	  omega_plus * (  dr - theta[i]* r0[i][jj] / 3.0 ) * 
	  (  dr - theta[i]* r0[i][jj] / 3.0 ) * vfrac[j] * vfrac_scale;

     }
  }

  return energy;
}

/* ----------------------------------------------------------------------
   memory usage of local atom-based arrays 
------------------------------------------------------------------------- */

double PairPeriLPS::memory_usage()
{
  double bytes = 2 * nmax * sizeof(double);
  return bytes;
}

/* ----------------------------------------------------------------------
   influence function definition
------------------------------------------------------------------------- */

double PairPeriLPS::influence_function(double xi_x, double xi_y, double xi_z)
{
  double r = sqrt(xi_x*xi_x + xi_y*xi_y + xi_z*xi_z);
  double omega;

  if (fabs(r) < 2.2204e-016)
    error->one("Divide by 0 in influence function of pair peri/lps");
  omega = 1.0/r;
  return omega;
}

/* ---------------------------------------------------------------------- */

void PairPeriLPS::compute_dilatation()
{
  int i,j,jj,jnum,itype,jtype;
  double xtmp,ytmp,ztmp,delx,dely,delz;
  double xtmp0,ytmp0,ztmp0,delx0,dely0,delz0;
  double rsq,r,dr;
  double delta;

  double **x = atom->x;
  int *type = atom->type;
  double **x0 = atom->x0;
  int nlocal = atom->nlocal;
  double *vfrac = atom->vfrac;
  double vfrac_scale = 1.0;
  
  double lc = domain->lattice->xlattice;
  double half_lc = 0.5*lc;

  double **r0   = ((FixPeriNeigh *) modify->fix[ifix_peri])->r0;
  int **partner = ((FixPeriNeigh *) modify->fix[ifix_peri])->partner;
  int *npartner = ((FixPeriNeigh *) modify->fix[ifix_peri])->npartner;
  double *wvolume = ((FixPeriNeigh *) modify->fix[ifix_peri])->wvolume;

  int periodic = domain->xperiodic || domain->yperiodic || domain->zperiodic;

  // compute the dilatation theta

  for (i = 0; i < nlocal; i++) {
    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    xtmp0 = x0[i][0];
    ytmp0 = x0[i][1];
    ztmp0 = x0[i][2];
    jnum = npartner[i];
    theta[i] = 0.0;
    itype = type[i];

    for (jj = 0; jj < jnum; jj++) {

      // if bond already broken, skip this partner
      if (partner[i][jj] == 0) continue;

      // Look up local index of this partner particle
      j = atom->map(partner[i][jj]);

      // Skip if particle is "lost"
      if (j < 0) continue;

      // Compute force density and add to PD equation of motion
      delx = xtmp - x[j][0];
      dely = ytmp - x[j][1];
      delz = ztmp - x[j][2];
      if (periodic) domain->minimum_image(delx,dely,delz);
      rsq = delx*delx + dely*dely + delz*delz;
      delx0 = xtmp0 - x0[j][0];
      dely0 = ytmp0 - x0[j][1];
      delz0 = ztmp0 - x0[j][2];
      if (periodic) domain->minimum_image(delx0,dely0,delz0);

      r = sqrt(rsq);
      dr = r - r0[i][jj];
      if (fabs(dr) < 2.2204e-016) dr = 0.0;

      jtype = type[j];
      delta = sqrt(cutsq[itype][jtype]);

      // scale vfrac[j] if particle j near the horizon

      if ((fabs(r0[i][jj] - delta)) <= half_lc)
        vfrac_scale = (-1.0/(2*half_lc))*(r0[i][jj]) +
          (1.0 + ((delta - half_lc)/(2*half_lc) ) );
      else vfrac_scale = 1.0;

      theta[i] += influence_function(delx0, dely0, delz0) * r0[i][jj] * dr *
	vfrac[j] * vfrac_scale;

    }

    // if wvolume[i] is zero, then particle i has no bonds
    // therefore, the dilatation is set to 

    if (wvolume[i] != 0.0) theta[i] = (3.0/wvolume[i]) * theta[i];
    else theta[i] = 0;
  }
}

/* ----------------------------------------------------------------------
   communication routines
   ---------------------------------------------------------------------- */
 
int PairPeriLPS::pack_comm(int n, int *list, double *buf,
			   int pbc_flag, int *pbc)
{

  int i,j,m;
 
  m = 0;
  for (i = 0; i < n; i++) {
    j = list[i];
    buf[m++] = theta[j];
  }
  return 1;
}
 
/* ---------------------------------------------------------------------- */
 
void PairPeriLPS::unpack_comm(int n, int first, double *buf)
{
  int i,m,last;
 
  m = 0;
  last = first + n;
  for (i = first; i < last; i++) {
    theta[i] = buf[m++];
  }
}


