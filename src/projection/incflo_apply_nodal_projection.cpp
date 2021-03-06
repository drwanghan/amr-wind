#include <AMReX_BC_TYPES.H>
#include <AMReX_Box.H>

#include <incflo.H>

//
// Computes the following decomposition:
//
//    u + dt grad(phi) / ro = u*,     where div(u) = 0
//
// where u* is a non-div-free velocity field, stored
// by components in u, v, and w. The resulting div-free
// velocity field, u, overwrites the value of u* in u, v, and w.
//
// phi is an auxiliary function related to the pressure p by the relation:
//
//     new p  = phi
//
// except in the initial projection when
//
//     new p  = old p + phi     (nstep has its initial value -1)
//
// Note: scaling_factor equals dt except when called during initial projection, when it is 1.0
//
void incflo::ApplyProjection(Real time, Real scaling_factor, bool incremental)
{
    BL_PROFILE("incflo::ApplyProjection");

    bool proj_for_small_dt      = false;

    // If we have dropped the dt substantially for whatever reason, use a different form of the approximate
    // projection that projects (U^*-U^n + dt Gp) rather than (U^* + dt Gp)

    if (time > 0 && dt < 0.1 * prev_dt) 
       proj_for_small_dt      = true;

    if (incflo_verbose > 2)
    {
        if (proj_for_small_dt)
           amrex::Print() << "Before projection (with small dt modification):" << std::endl;
        else
           amrex::Print() << "Before projection:" << std::endl;
        PrintMaxValues(time);
    }

    // Add the ( grad p /ro ) back to u* (note the +dt)
    if (!incremental)
    {
        for(int lev = 0; lev <= finest_level; lev++)
        {
            // Convert velocities to momenta
            for(int dir = 0; dir < AMREX_SPACEDIM; dir++)
            {
                MultiFab::Multiply(*vel[lev], *density[lev], 0, dir, 1, vel[lev]->nGrow());
            }

            MultiFab::Saxpy(*vel[lev], scaling_factor, *gp[lev], 0, 0, AMREX_SPACEDIM, vel[lev]->nGrow());

            // Convert momenta back to velocities
            for(int dir = 0; dir < AMREX_SPACEDIM; dir++)
            {
                MultiFab::Divide(*vel[lev], *density[lev], 0, dir, 1, vel[lev]->nGrow());
            }
        }
    }

    // Set velocity BCs before projection
    int extrap_dir_bcs(0);
    incflo_set_velocity_bcs(time, vel    , extrap_dir_bcs);

    // Define "vel" to be U^* - U^n rather than U^*
    if (proj_for_small_dt)
    {
       incflo_set_velocity_bcs(time, vel_o, extrap_dir_bcs);

       for(int lev = 0; lev <= finest_level; lev++)
          MultiFab::Saxpy(*vel[lev], -1.0, *vel_o[lev], 0, 0, AMREX_SPACEDIM, vel[lev]->nGrow());
    }

    // Create sigma
    Vector< std::unique_ptr< amrex::MultiFab > >  sigma(nlev);
    for (int lev(0); lev < nlev; ++lev )
    {
#ifdef AMREX_USE_EB
        sigma[lev].reset(new MultiFab(grids[lev], dmap[lev], 1, 1, MFInfo(), *ebfactory[lev]));
#else
        sigma[lev].reset(new MultiFab(grids[lev], dmap[lev], 1, 1, MFInfo()));
#endif
        sigma[lev] -> setVal(scaling_factor);
        MultiFab::Divide(*sigma[lev],*density[lev],0,0,1,0);
    }

    // Perform projection
    nodal_projector -> project(GetVecOfPtrs(vel), GetVecOfConstPtrs(sigma));

    // Define "vel" to be U^{n+1} rather than (U^{n+1}-U^n)
    if (proj_for_small_dt)
    {
       for(int lev = 0; lev <= finest_level; lev++)
          MultiFab::Saxpy(*vel[lev], 1.0, *vel_o[lev], 0, 0, AMREX_SPACEDIM, vel[lev]->nGrow());
    }

    // Get phi and fluxes
    Vector< const amrex::MultiFab* >  phi(nlev);
    Vector< const amrex::MultiFab* >  gradphi(nlev);

    phi     = nodal_projector -> getPhi();
    gradphi = nodal_projector -> getGradPhi();

    for(int lev = 0; lev <= finest_level; lev++)
    {
        if (incremental)
        {
            // p := p + phi
            MultiFab::Add(*p[lev], *phi[lev], 0, 0, 1, phi[lev]->nGrow());
            MultiFab::Add(*gp[lev], *gradphi[lev], 0, 0, AMREX_SPACEDIM, gradphi[lev]->nGrow());
        }
        else
        {
            // p := phi
            MultiFab::Copy(*p[lev], *phi[lev], 0, 0, 1, phi[lev]->nGrow());
            MultiFab::Copy(*gp[lev], *gradphi[lev], 0, 0, AMREX_SPACEDIM, gradphi[lev]->nGrow());
        }

    }

    AverageDown();

    if(incflo_verbose > 2)
    {
        if (proj_for_small_dt)
           amrex::Print() << "After  projection (with small dt modification):" << std::endl;
        else
           amrex::Print() << "After  projection:" << std::endl;
        PrintMaxValues(time);
    }
}
