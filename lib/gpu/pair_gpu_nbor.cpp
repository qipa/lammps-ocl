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
   Contributing authors: Mike Brown (ORNL), brownw@ornl.gov
                         Peng Wang (Nvidia), penwang@nvidia.com
------------------------------------------------------------------------- */

#include "pair_gpu_precision.h"
#include "pair_gpu_nbor.h"
#include "math.h"

#ifdef USE_OPENCL
#include "pair_gpu_nbor_cl.h"
#else
#include "pair_gpu_nbor_ptx.h"
#include "pair_gpu_build_ptx.h"
#endif

int PairGPUNbor::bytes_per_atom(const int max_nbors) const {
  if (_gpu_nbor)
    return (max_nbors+2)*sizeof(int);
  else if (_use_packing)
    return ((max_nbors+2)*2)*sizeof(int);
  else
    return (max_nbors+3)*sizeof(int);
}

bool PairGPUNbor::init(const int inum, const int host_inum, const int max_nbors, 
                       const int maxspecial, UCL_Device &devi, 
                       const bool gpu_nbor, const int gpu_host, 
                       const bool pre_cut) {
  clear();

  dev=&devi;
  _gpu_nbor=gpu_nbor;
  if (gpu_host==0)
    _gpu_host=false;
  else if (gpu_host==1)
    _gpu_host=true;
  else 
    // Not yet implemented
    assert(0==1);
  
  if (pre_cut || gpu_nbor==false)
    _alloc_packed=true;
  else
    _alloc_packed=false;

  bool success=true;
    
  // Initialize timers for the selected GPU
  time_nbor.init(*dev);
  time_kernel.init(*dev);
  time_nbor.zero();
  time_kernel.zero();

  _max_atoms=static_cast<int>(static_cast<double>(inum)*1.10);
  if (_max_atoms==0)
    _max_atoms=1000;
    
  _max_host=static_cast<int>(static_cast<double>(host_inum)*1.10);
  _max_nbors=max_nbors;

  _maxspecial=maxspecial;
  if (gpu_nbor==false)
    _maxspecial=0;

  if (gpu_nbor==false)
    success=success && (host_packed.alloc(2*IJ_SIZE,*dev,
                                          UCL_WRITE_OPTIMIZED)==UCL_SUCCESS);
  alloc(success);
  if (_use_packing==false)
    compile_kernels(devi);

  return success;
}

void PairGPUNbor::alloc(bool &success) { 
  dev_nbor.clear();
  host_acc.clear();
  if (_use_packing==false || _gpu_nbor) 
    success=success && (dev_nbor.alloc((_max_nbors+2)*_max_atoms,*dev,
                                       UCL_READ_ONLY)==UCL_SUCCESS);
  else 
    success=success && (dev_nbor.alloc(3*_max_atoms,*dev,
                                       UCL_READ_ONLY)==UCL_SUCCESS);
  success=success && (host_acc.alloc((_max_atoms+_max_host)*2,*dev,
                                     UCL_WRITE_OPTIMIZED)==UCL_SUCCESS);

  _c_bytes=dev_nbor.row_bytes();
  if (_alloc_packed) {
    dev_packed.clear();
    success=success && (dev_packed.alloc((_max_nbors+2)*_max_atoms,*dev,
                                         UCL_READ_ONLY)==UCL_SUCCESS);
    _c_bytes+=dev_packed.row_bytes();                                         
  } 
  if (_max_host>0) {
    host_nbor.clear();
    dev_host_nbor.clear();
    success=success && (host_nbor.alloc((_max_nbors+1)*_max_host,*dev,
                                        UCL_RW_OPTIMIZED)==UCL_SUCCESS);
    success=success && (dev_host_nbor.alloc((_max_nbors+1)*_max_host,
                                            *dev,UCL_WRITE_ONLY)==UCL_SUCCESS);
    _c_bytes+=dev_host_nbor.row_bytes();
  }
  if (_maxspecial>0) {
    dev_nspecial.clear();
    dev_special.clear();
    dev_special_t.clear();
    int at=_max_atoms+_max_host;
    success=success && (dev_nspecial.alloc(3*at,*dev,
                                           UCL_READ_ONLY)==UCL_SUCCESS);
    success=success && (dev_special.alloc(_maxspecial*at,*dev,
                                          UCL_READ_ONLY)==UCL_SUCCESS);
    success=success && (dev_special_t.alloc(_maxspecial*at,*dev,
                                            UCL_READ_ONLY)==UCL_SUCCESS);
    _gpu_bytes+=dev_nspecial.row_bytes()+dev_special.row_bytes()+
                dev_special_t.row_bytes();
  }

  _allocated=true;
}
  
void PairGPUNbor::clear() {
  _gpu_bytes=0.0;
  _cell_bytes=0.0;
  _c_bytes=0.0;
  if (_allocated) {
    _allocated=false;

    host_packed.clear();
    host_acc.clear();
    dev_nbor.clear();
    dev_host_nbor.clear();
    dev_packed.clear();
    host_nbor.clear();
    dev_nspecial.clear();
    dev_special.clear();
    dev_special_t.clear();

    time_kernel.clear();
    time_nbor.clear();
  }

  if (_compiled) {
    if (_gpu_nbor) {
      k_cell_id.clear();
      k_cell_counts.clear();
      k_build_nbor.clear();
      k_transpose.clear();
      k_special.clear();
      delete build_program;
    } else {
      k_nbor.clear();
      delete nbor_program;
    }
    _compiled=false;
  }
}

double PairGPUNbor::host_memory_usage() const {
  if (_gpu_nbor) {
    if (_gpu_host)
      return host_nbor.row_bytes()*host_nbor.rows();
    else
      return 0;
  } else 
    return host_packed.row_bytes()*host_packed.rows()+host_acc.row_bytes()+
           sizeof(PairGPUNbor);
}

void PairGPUNbor::get_host(const int inum, int *ilist, int *numj,
                           int **firstneigh, const int block_size) {  
  time_nbor.start();

  UCL_H_Vec<int> ilist_view;
  ilist_view.view(ilist,inum,*dev);
  ucl_copy(dev_nbor,ilist_view,true);

  UCL_D_Vec<int> nbor_offset;
  UCL_H_Vec<int> host_offset;

  int copy_count=0;
  int ij_count=0;
  int acc_count=0;
  int dev_count=0;
  int *h_ptr=host_packed.begin();
  _nbor_pitch=inum;
  
  for (int ii=0; ii<inum; ii++) {
    int i=ilist[ii];
    int nj=numj[i];
    host_acc[ii]=nj;
    host_acc[ii+inum]=acc_count;

    acc_count+=nj;
    
    int *jlist=firstneigh[i];
    for (int jj=0; jj<nj; jj++) {
      *h_ptr=jlist[jj];
      h_ptr++;
      ij_count++;
       
      if (ij_count==IJ_SIZE) {
        dev_nbor.sync();
        host_offset.view_offset(IJ_SIZE*(copy_count%2),host_packed,IJ_SIZE);
        nbor_offset.view_offset(dev_count,dev_packed,IJ_SIZE);
        ucl_copy(nbor_offset,host_offset,true);
        copy_count++;
        ij_count=0;
        dev_count+=IJ_SIZE;
        h_ptr=host_packed.begin()+(IJ_SIZE*(copy_count%2));
      }
    }
  }
  if (ij_count!=0) {
    dev_nbor.sync();
    host_offset.view_offset(IJ_SIZE*(copy_count%2),host_packed,ij_count);
    nbor_offset.view_offset(dev_count,dev_packed,ij_count);
    ucl_copy(nbor_offset,host_offset,true);
  }
  UCL_D_Vec<int> acc_view;
  acc_view.view_offset(inum,dev_nbor,inum*2);
  ucl_copy(acc_view,host_acc,true);
  time_nbor.stop();
  
  if (_use_packing==false) {
    time_kernel.start();
    int GX=static_cast<int>(ceil(static_cast<double>(inum)/block_size));
    k_nbor.set_size(GX,block_size);
    k_nbor.run(&dev_nbor.begin(), &dev_packed.begin(), &inum);
    time_kernel.stop();
  }
}

void PairGPUNbor::compile_kernels(UCL_Device &dev) {
  std::string flags="-cl-fast-relaxed-math -cl-mad-enable";

  if (_gpu_nbor==false) {
    nbor_program=new UCL_Program(dev);
    nbor_program->load_string(pair_gpu_nbor_kernel,flags.c_str());
    k_nbor.set_function(*nbor_program,"kernel_unpack");
  } else {
    build_program=new UCL_Program(dev);
    #ifdef USE_OPENCL
    std::cerr << "CANNOT CURRENTLY USE GPU NEIGHBORING WITH OPENCL\n";
    exit(1);
    #else
    build_program->load_string(pair_gpu_build_kernel,flags.c_str());
    #endif
    k_cell_id.set_function(*build_program,"calc_cell_id");
    k_cell_counts.set_function(*build_program,"kernel_calc_cell_counts");
    k_build_nbor.set_function(*build_program,"calc_neigh_list_cell");
    k_transpose.set_function(*build_program,"transpose");
    k_special.set_function(*build_program,"kernel_special");
    neigh_tex.get_texture(*build_program,"neigh_tex");
  }
  _compiled=true;
}

template <class numtyp, class acctyp>
void PairGPUNbor::build_nbor_list(const int inum, const int host_inum,
                                  const int nall, 
                                  PairGPUAtom<numtyp,acctyp> &atom, 
                                  double *boxlo, double *boxhi, int *tag, 
                                  int **nspecial, int **special, bool &success,
                                  int &mn) {
  const int nt=inum+host_inum;

  if (_maxspecial>0) {
    time_nbor.start();
    UCL_H_Vec<int> view_nspecial, view_special, view_tag;
    view_nspecial.view(nspecial[0],nt*3,*dev);
    view_special.view(special[0],nt*_maxspecial,*dev);
    view_tag.view(tag,nall,*dev);
    ucl_copy(dev_nspecial,view_nspecial,nt*3,false);
    ucl_copy(dev_special_t,view_special,nt*_maxspecial,false);
    ucl_copy(atom.dev_tag,view_tag,nall,false);
    time_nbor.stop();
    time_nbor.add_to_total();
    time_kernel.start();
    const int b2x=8;
    const int b2y=8;
    const int g2x=static_cast<int>(ceil(static_cast<double>(_maxspecial)/b2x));
    const int g2y=static_cast<int>(ceil(static_cast<double>(nt)/b2y));
    k_transpose.set_size(g2x,g2y,b2x,b2y);
    k_transpose.run(&dev_special.begin(),&dev_special_t.begin(),&_maxspecial,
                    &nt);        
  } else
    time_kernel.start();

  _nbor_pitch=inum;
  neigh_tex.bind_float(atom.dev_x,4);

  int ncellx, ncelly, ncellz, ncell_3d;
  ncellx = static_cast<int>(ceil(((boxhi[0] - boxlo[0]) +
                                  2.0*_cell_size)/_cell_size));
  ncelly = static_cast<int>(ceil(((boxhi[1] - boxlo[1]) +
                                  2.0*_cell_size)/_cell_size));
  ncellz = static_cast<int>(ceil(((boxhi[2] - boxlo[2]) +
                                  2.0*_cell_size)/_cell_size));
  ncell_3d = ncellx * ncelly * ncellz;
  UCL_D_Vec<int> cell_counts;
  cell_counts.alloc(ncell_3d+1,dev_nbor);
  _cell_bytes=cell_counts.row_bytes();

  /* build cell list on GPU */
  const int neigh_block=128;
  const int GX=(int)ceil((float)nall/neigh_block);
  const numtyp boxlo0=static_cast<numtyp>(boxlo[0]);
  const numtyp boxlo1=static_cast<numtyp>(boxlo[1]);
  const numtyp boxlo2=static_cast<numtyp>(boxlo[2]);
  const numtyp boxhi0=static_cast<numtyp>(boxhi[0]);
  const numtyp boxhi1=static_cast<numtyp>(boxhi[1]);
  const numtyp boxhi2=static_cast<numtyp>(boxhi[2]);
  const numtyp cell_size_cast=static_cast<numtyp>(_cell_size);
  k_cell_id.set_size(GX,neigh_block);
  k_cell_id.run(&atom.dev_x.begin(), &atom.dev_cell_id.begin(), 
                &atom.dev_particle_id.begin(),
  				      &boxlo0, &boxlo1, &boxlo2, &boxhi0, &boxhi1, 
  				      &boxhi2, &cell_size_cast, &ncellx, &ncelly, &nall);

  atom.sort_neighbor(nall);

  /* calculate cell count */
  k_cell_counts.set_size(GX,neigh_block);
  k_cell_counts.run(&atom.dev_cell_id.begin(), &cell_counts.begin(), &nall, 
                    &ncell_3d);

  /* build the neighbor list */
  const int cell_block=64;
  k_build_nbor.set_size(ncellx, ncelly*ncellz, cell_block, 1);
  k_build_nbor.run(&atom.dev_x.begin(), &atom.dev_particle_id.begin(),
                   &cell_counts.begin(), &dev_nbor.begin(),
                   &dev_host_nbor.begin(), &_max_nbors, &cell_size_cast,
                   &ncellx, &ncelly, &ncellz, &inum, &nt, &nall);

  /* Get the maximum number of nbors and realloc if necessary */
  UCL_D_Vec<int> numj;
  numj.view_offset(inum,dev_nbor,inum);
  ucl_copy(host_acc,numj,inum,false);
  if (nt>inum) {
    UCL_H_Vec<int> host_offset;
    host_offset.view_offset(inum,host_acc,nt-inum);
    ucl_copy(host_offset,dev_host_nbor,nt-inum,false);
  }
  mn=host_acc[0];
  for (int i=1; i<nt; i++)
    mn=std::max(mn,host_acc[i]);

  if (mn>_max_nbors) {  
    mn=static_cast<int>(static_cast<double>(mn)*1.10);
    dev_nbor.clear();
    success=success && (dev_nbor.alloc((mn+1)*_max_atoms,atom.dev_cell_id,
                        UCL_READ_ONLY)==UCL_SUCCESS);
    _gpu_bytes=dev_nbor.row_bytes();
    if (_max_host>0) {
      host_nbor.clear();
      dev_host_nbor.clear();
      success=success && (host_nbor.alloc((mn+1)*_max_host,dev_nbor,
                                          UCL_RW_OPTIMIZED)==UCL_SUCCESS);
      success=success && (dev_host_nbor.alloc((mn+1)*_max_host,
                                        dev_nbor,UCL_WRITE_ONLY)==UCL_SUCCESS);
      _gpu_bytes+=dev_host_nbor.row_bytes();
    }
    if (_alloc_packed) {
      dev_packed.clear();
      success=success && (dev_packed.alloc((mn+2)*_max_atoms,*dev,
                                           UCL_READ_ONLY)==UCL_SUCCESS);
      _gpu_bytes+=dev_packed.row_bytes();
    }
    if (!success)
      return;
    _max_nbors=mn;
    time_kernel.stop();
    time_kernel.add_to_total();
    build_nbor_list(inum, host_inum, nall, atom, boxlo, boxhi, tag, nspecial,
                    special, success, mn);
    return;
  }
  
  if (_maxspecial>0) {
    const int GX2=static_cast<int>(ceil(static_cast<double>(nt)/cell_block));
    k_special.set_size(GX2,cell_block);
    k_special.run(&dev_nbor.begin(), &dev_host_nbor.begin(), 
                  &atom.dev_tag.begin(), &dev_nspecial.begin(), 
                  &dev_special.begin(), &inum, &nt, &nall);
  }
  time_kernel.stop();

  time_nbor.start();
  if (_gpu_host)
    ucl_copy(host_nbor,dev_host_nbor,host_inum*(mn+1),false);
  time_nbor.stop();
}

template void PairGPUNbor::build_nbor_list<PRECISION,ACC_PRECISION>
     (const int inum, const int host_inum, const int nall, 
      PairGPUAtom<PRECISION,ACC_PRECISION> &atom, double *boxlo, double *boxhi,
      int *, int **, int **, bool &success, int &mn);

