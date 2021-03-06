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

#ifdef ATOM_CLASS

AtomStyle(colloid,AtomVecColloid)

#else

#ifndef LMP_ATOM_VEC_COLLOID_H
#define LMP_ATOM_VEC_COLLOID_H

#include "atom_vec.h"

namespace LAMMPS_NS {

class AtomVecColloid : public AtomVec {
 public:
  AtomVecColloid(class LAMMPS *, int, char **);
  virtual ~AtomVecColloid() {}
  void grow(int);
  void grow_reset();
  void copy(int, int);
  int pack_comm(int, int *, double *, int, int *);
  int pack_comm_vel(int, int *, double *, int, int *);
  void unpack_comm(int, int, double *);
  void unpack_comm_vel(int, int, double *);
  int pack_reverse(int, int, double *);
  int pack_reverse_one(int, double *);
  void unpack_reverse(int, int *, double *);
  int unpack_reverse_one(int, double *);
  int pack_border(int, int *, double *, int, int *);
  int pack_border_vel(int, int *, double *, int, int *);
  void unpack_border(int, int, double *);
  void unpack_border_vel(int, int, double *);
  int pack_exchange(int, double *);
  int unpack_exchange(double *);
  int size_restart();
  int pack_restart(int, double *);
  int unpack_restart(double *);
  void create_atom(int, double *);
  void data_atom(double *, int, char **);
  void data_vel(int, char **);
  int data_vel_hybrid(int, char **);
  bigint memory_usage();

 private:
  int *tag,*type,*mask,*image;
  double **x,**v,**f;
  double **omega,**torque;
};

}

#endif
#endif
