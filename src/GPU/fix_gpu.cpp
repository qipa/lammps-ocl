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

#include "string.h"
#include "stdlib.h"
#include "fix_gpu.h"
#include "atom.h"
#include "force.h"
#include "pair.h"
#include "respa.h"
#include "input.h"
#include "error.h"
#include "timer.h"
#include "modify.h"
#include "domain.h"
#include "universe.h"

using namespace LAMMPS_NS;

enum{GPU_FORCE, GPU_NEIGH};

extern bool lmp_init_device(MPI_Comm world, MPI_Comm replica,
                            const int first_gpu, const int last_gpu,
                            const int gpu_mode, const double particle_split,
                            const int nthreads);
extern void lmp_clear_device();
extern double lmp_gpu_forces(double **f, double **tor, double *eatom,
                             double **vatom, double *virial, double &ecoul);

/* ---------------------------------------------------------------------- */

FixGPU::FixGPU(LAMMPS *lmp, int narg, char **arg) :
  Fix(lmp, narg, arg)
{
  if (narg != 7) error->all("Illegal fix gpu command");

  if (strcmp(arg[1],"all") != 0)
    error->all("Illegal fix gpu command");

  int gpu_mode, first_gpu, last_gpu;
  double particle_split;

  if (strcmp(arg[3],"force") == 0)
    gpu_mode = GPU_FORCE;
  else if (strcmp(arg[3],"force/neigh") == 0) {
    gpu_mode = GPU_NEIGH;
    if (domain->triclinic)
      error->all("Cannot use force/neigh with triclinic box.");
  } else
    error->all("Illegal fix gpu command.");

  first_gpu = atoi(arg[4]);
  last_gpu = atoi(arg[5]);

  particle_split = force->numeric(arg[6]);
  if (particle_split==0 || particle_split>1)
    error->all("Illegal fix gpu command.");
    
  if (!lmp_init_device(universe->uworld,world,first_gpu,last_gpu,gpu_mode,
                       particle_split,1))
    error->one("Could not find or initialize a specified accelerator device.");
}

/* ---------------------------------------------------------------------- */

FixGPU::~FixGPU()
{
  lmp_clear_device();
}

/* ---------------------------------------------------------------------- */

int FixGPU::setmask()
{
  int mask = 0;
  mask |= POST_FORCE;
  mask |= MIN_POST_FORCE;
  return mask;
}

/* ---------------------------------------------------------------------- */

void FixGPU::init()
{
  // Can only have 1 gpu fix that must be the first fix for a run
  if ((void*)modify->fix[0] != (void*)this)
    error->all("GPU is not the first fix for this run.");
}

/* ---------------------------------------------------------------------- */

void FixGPU::setup(int vflag)
{
  post_force(vflag);
}

/* ---------------------------------------------------------------------- */

void FixGPU::min_setup(int vflag)
{
  post_force(vflag);
}

/* ---------------------------------------------------------------------- */

void FixGPU::post_force(int vflag)
{
  timer->stamp();
  double lvirial[6];
  for (int i = 0; i < 6; i++) lvirial[i] = 0.0;
  double my_eng = lmp_gpu_forces(atom->f, atom->torque, force->pair->eatom,
                                 force->pair->vatom, lvirial,
                                 force->pair->eng_coul);

  force->pair->eng_vdwl += my_eng;
  force->pair->virial[0] += lvirial[0];
  force->pair->virial[1] += lvirial[1];
  force->pair->virial[2] += lvirial[2];
  force->pair->virial[3] += lvirial[3];
  force->pair->virial[4] += lvirial[4];
  force->pair->virial[5] += lvirial[5];

  if (force->pair->vflag_fdotr) force->pair->virial_compute();
  timer->stamp(TIME_PAIR);
}

/* ---------------------------------------------------------------------- */

void FixGPU::min_post_force(int vflag)
{
  post_force(vflag);
}

/* ---------------------------------------------------------------------- */

double FixGPU::memory_usage()
{
  double bytes = 0.0;
  // Memory usage currently returned by pair routine
  return bytes;
}

