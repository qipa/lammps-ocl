# Install/unInstall package files in LAMMPS

if (test $1 = 1) then

  cp -p atom_vec_electron.cpp ..
  cp -p pair_eff_cut.cpp ..
  cp -p compute_ke_eff.cpp ..
  cp -p compute_ke_atom_eff.cpp ..
  cp -p compute_temp_deform_eff.cpp ..
  cp -p compute_temp_eff.cpp ..
  cp -p compute_temp_region_eff.cpp ..
  cp -p fix_langevin_eff.cpp ..
  cp -p fix_nh_eff.cpp ..
  cp -p fix_nve_eff.cpp ..
  cp -p fix_nvt_eff.cpp ..
  cp -p fix_nvt_sllod_eff.cpp ..
  cp -p fix_npt_eff.cpp ..
  cp -p fix_nph_eff.cpp ..
  cp -p fix_temp_rescale_eff.cpp ..

  cp -p atom_vec_electron.h ..
  cp -p pair_eff_cut.h ..
  cp -p pair_eff_inline.h ..
  cp -p compute_ke_eff.h ..
  cp -p compute_ke_atom_eff.h ..
  cp -p compute_temp_deform_eff.h ..
  cp -p compute_temp_eff.h ..
  cp -p compute_temp_region_eff.h ..
  cp -p fix_langevin_eff.h ..
  cp -p fix_nh_eff.h ..
  cp -p fix_nve_eff.h ..
  cp -p fix_nvt_eff.h ..
  cp -p fix_nvt_sllod_eff.h ..
  cp -p fix_npt_eff.h ..
  cp -p fix_nph_eff.h ..
  cp -p fix_temp_rescale_eff.h ..

elif (test $1 = 0) then

  rm ../atom_vec_electron.cpp
  rm ../pair_eff_cut.cpp
  rm ../compute_ke_eff.cpp
  rm ../compute_ke_atom_eff.cpp
  rm ../compute_temp_deform_eff.cpp
  rm ../compute_temp_eff.cpp
  rm ../compute_temp_region_eff.cpp
  rm ../fix_langevin_eff.cpp ..
  rm ../fix_nh_eff.cpp
  rm ../fix_nve_eff.cpp
  rm ../fix_nvt_eff.cpp
  rm ../fix_nvt_sllod_eff.cpp
  rm ../fix_npt_eff.cpp
  rm ../fix_nph_eff.cpp
  rm ../fix_temp_rescale_eff.cpp

  rm ../atom_vec_electron.h
  rm ../pair_eff_cut.h
  rm ../pair_eff_inline.h
  rm ../compute_ke_eff.h
  rm ../compute_ke_atom_eff.h
  rm ../compute_temp_deform_eff.h
  rm ../compute_temp_eff.h
  rm ../compute_temp_region_eff.h
  rm ../fix_langevin_eff.h ..
  rm ../fix_nh_eff.h
  rm ../fix_nve_eff.h
  rm ../fix_nvt_eff.h
  rm ../fix_nvt_sllod_eff.h
  rm ../fix_npt_eff.h
  rm ../fix_nph_eff.h
  rm ../fix_temp_rescale_eff.h

fi
