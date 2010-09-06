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

#ifdef FIX_CLASS

FixStyle(temp/rescale/eff,FixTempRescaleEff)

#else

#ifndef LMP_FIX_TEMP_RESCALE_EFF_H
#define LMP_FIX_TEMP_RESCALE_EFF_H

#include "fix_temp_rescale.h"

namespace LAMMPS_NS {

class FixTempRescaleEff : public FixTempRescale {
 public:
  FixTempRescaleEff(class LAMMPS *, int, char **);
  ~FixTempRescaleEff() {}
  void end_of_step();
};

}

#endif
#endif