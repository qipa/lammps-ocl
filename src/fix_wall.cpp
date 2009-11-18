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

#include "math.h"
#include "stdlib.h"
#include "string.h"
#include "fix_wall.h"
#include "atom.h"
#include "domain.h"
#include "update.h"
#include "output.h"
#include "respa.h"
#include "error.h"

using namespace LAMMPS_NS;

enum{XLO,XHI,YLO,YHI,ZLO,ZHI};

/* ---------------------------------------------------------------------- */

FixWall::FixWall(LAMMPS *lmp, int narg, char **arg) :
  Fix(lmp, narg, arg)
{
  scalar_flag = 1;
  vector_flag = 1;
  size_vector = 6;
  scalar_vector_freq = 1;
  extscalar = 1;
  extvector = 1;

  // parse args

  for (int m = 0; m < 6; m++) wflag[m] = 0;
  vflag = 0;
  aflag = 0;

  int iarg = 3;
  if ((strcmp(arg[iarg],"xlo") == 0) || (strcmp(arg[iarg],"xhi") == 0) ||
      (strcmp(arg[iarg],"ylo") == 0) || (strcmp(arg[iarg],"yhi") == 0) ||
      (strcmp(arg[iarg],"zlo") == 0) || (strcmp(arg[iarg],"zhi") == 0)) {
    if (iarg+5 > narg) error->all("Illegal fix wall command");
    int m;
    if (strcmp(arg[iarg],"xlo") == 0) m = XLO;
    else if (strcmp(arg[iarg],"xhi") == 0) m = XHI;
    else if (strcmp(arg[iarg],"ylo") == 0) m = YLO;
    else if (strcmp(arg[iarg],"yhi") == 0) m = YHI;
    else if (strcmp(arg[iarg],"zlo") == 0) m = ZLO;
    else if (strcmp(arg[iarg],"zhi") == 0) m = ZHI;
    wflag[m] = 1;
    coord0[m] = atof(arg[iarg+1]);
    epsilon[m] = atof(arg[iarg+2]);
    sigma[m] = atof(arg[iarg+3]);
    cutoff[m] = atof(arg[iarg+4]);
    iarg += 5;
  } else if (strcmp(arg[iarg],"vel") == 0) {
    if (iarg+2 > narg) error->all("Illegal fix wall command");
    vflag = 1;
    vel = atof(arg[iarg+1]);
    iarg += 2;
  } else if (strcmp(arg[iarg],"wiggle") == 0) {
    if (iarg+3 > narg) error->all("Illegal fix wall command");
    aflag = 1;
    amplitude = atof(arg[iarg+1]);
    period = atof(arg[iarg+2]);
    iarg += 3;
  } else error->all("Illegal fix wall command");

  // error check

  int flag = 0;
  for (int m = 0; m < 6; m++) if (wflag[m]) flag;
  if (!flag) error->all("Illegal fix wall command");

  if (vflag && aflag) 
    error->all("Cannot set both vel and wiggle in fix wall command");

  if ((wflag[XLO] || wflag[XHI]) && domain->xperiodic)
    error->all("Cannot use fix wall in periodic dimension");
  if ((wflag[YLO] || wflag[YHI]) && domain->yperiodic)
    error->all("Cannot use fix wall in periodic dimension");
  if ((wflag[ZLO] || wflag[ZHI]) && domain->xperiodic)
    error->all("Cannot use fix wall in periodic zimension");

  // setup oscillations

  if (aflag) {
    double PI = 4.0 * atan(1.0);
    omega = 2.0*PI / period;
  }

  eflag = 0;
  for (int m = 0; m < 7; m++) ewall[m] = 0.0;
}

/* ---------------------------------------------------------------------- */

int FixWall::setmask()
{
  int mask = 0;
  mask |= POST_FORCE;
  mask |= THERMO_ENERGY;
  mask |= POST_FORCE_RESPA;
  mask |= MIN_POST_FORCE;
  return mask;
}

/* ---------------------------------------------------------------------- */

void FixWall::init()
{
  dt = update->dt;

  // setup coefficients

  for (int m = 0; m < 6; m++)
    if (wflag[m]) precompute(m);

  if (strcmp(update->integrate_style,"respa") == 0)
    nlevels_respa = ((Respa *) update->integrate)->nlevels;
}

/* ---------------------------------------------------------------------- */

void FixWall::setup(int vflag)
{
  if (strcmp(update->integrate_style,"verlet") == 0)
    post_force(vflag);
  else {
    ((Respa *) update->integrate)->copy_flevel_f(nlevels_respa-1);
    post_force_respa(vflag,nlevels_respa-1,0);
    ((Respa *) update->integrate)->copy_f_flevel(nlevels_respa-1);
  }
}

/* ---------------------------------------------------------------------- */

void FixWall::min_setup(int vflag)
{
  post_force(vflag);
}

/* ---------------------------------------------------------------------- */

void FixWall::post_force(int vflag)
{
  eflag = 0;
  for (int m = 0; m < 7; m++) ewall[m] = 0.0;

  // coord0 = initial position of wall
  // coord = current position of wall

  double delta = (update->ntimestep - update->beginstep) * dt;
  double coord;

  for (int m = 0; m < 6; m++) {
    if (wflag[m] == 0) continue;

    if (vflag) {
      if (m/2 == 0) coord = coord0[m] + delta*vel;
      else coord = coord0[m] - delta*vel;
    } else if (aflag) {
      if (m/2 == 0) coord = coord0[m] + amplitude*sin(omega*delta);
      else coord = coord0[m] - amplitude*sin(omega*delta);
    } else coord = coord0[m];

    wall_particle(m,coord);
  }
}

/* ---------------------------------------------------------------------- */

void FixWall::post_force_respa(int vflag, int ilevel, int iloop)
{
  if (ilevel == nlevels_respa-1) post_force(vflag);
}

/* ---------------------------------------------------------------------- */

void FixWall::min_post_force(int vflag)
{
  post_force(vflag);
}

/* ----------------------------------------------------------------------
   energy of wall interaction
------------------------------------------------------------------------- */

double FixWall::compute_scalar()
{
  // only sum across procs one time

  if (eflag == 0) {
    MPI_Allreduce(ewall,ewall_all,7,MPI_DOUBLE,MPI_SUM,world);
    eflag = 1;
  }
  return ewall_all[0];
}

/* ----------------------------------------------------------------------
   components of force on wall
------------------------------------------------------------------------- */

double FixWall::compute_vector(int n)
{
  // only sum across procs one time

  if (eflag == 0) {
    MPI_Allreduce(ewall,ewall_all,7,MPI_DOUBLE,MPI_SUM,world);
    eflag = 1;
  }
  return ewall_all[n+1];
}