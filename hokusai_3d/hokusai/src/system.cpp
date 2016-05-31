/*
* Copyright 2015 Pierre-Luc Manteaux
*
*This file is part of Hokusai.
*
*Hokusai is free software: you can redistribute it and/or modify
*it under the terms of the GNU General Public License as published by
*the Free Software Foundation, either version 3 of the License, or
*(at your option) any later version.
*
*Hokusai is distributed in the hope that it will be useful,
*but WITHOUT ANY WARRANTY; without even the implied warranty of
*MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*GNU General Public License for more details.
*
*You should have received a copy of the GNU General Public License
*along with Hokusai.  If not, see <http://www.gnu.org/licenses/>.
*
* Author : Pierre-Luc Manteaux
* Contact : pierre-luc.manteaux@inria.fr
*/

#include "../include/hokusai/system.hpp"
#include "../include/hokusai/utils.hpp"
#include "../include/hokusai/rasterizer.hpp"
#include "../include/hokusai/gridUtility.hpp"
#include "../include/hokusai/HSL2RGB.hpp"
#include <algorithm>
#include <assert.h>

#ifdef HOKUSAI_USING_OPENMP
#include <omp.h>
#endif

#include <fstream>
#include <sstream>

namespace hokusai
{
//-----------------------------------------------------
//         System : creation and simulation functions
//-----------------------------------------------------

System::System()
{
    m_countExport = 0;
    m_countTime = 0;
    m_particleNumber = 0;
    m_boundaryNumber = 0;

    m_meanDensity = 0.0;
    m_densityFluctuation = 0.0;
    m_realVolume = 0.0;
    m_dt = 0.0;
    m_time = 0.0;
    m_gravity = Vec3r(0,-9.81,0);
    m_gridInfo = GridUtility();
    m_particles = std::vector<Particle>();
    m_boundaries = std::vector<Boundary>();

    m_averageDensity = 0.0;
    m_maxPressureSolveIterationNb =2;
    m_maxDensityError = 1.0;
}

System::System(int wishedParticleNumber)
{
    m_countExport = 0;
    m_countTime = 0;
    m_particleNumber = 0;
    m_boundaryNumber = 0;

    m_meanDensity = 0.0;
    m_densityFluctuation = 0.0;
    m_realVolume = 0.0;
    m_dt = 0.0;
    m_time = 0.0;

    m_gravity = Vec3r(0,-9.81,0);

    m_gridInfo = GridUtility();
    m_particles = std::vector<Particle>();
    m_boundaries = std::vector<Boundary>();

    setParameters(wishedParticleNumber, 1.0);
}

System::~System(){}

void System::computeDensity(int i)
{
    Particle& pi=m_particles[i];
    std::vector<int>& fneighbors=pi.fluidNeighbor;
    std::vector<int>& bneighbors=pi.boundaryNeighbor;
    pi.rho=0.0;
    for(int& j : fneighbors)
    {
        Particle& pj=m_particles[j];
        pi.rho += pj.fluidParams().mass()*pj.fluidParams().monaghanKernel().monaghanValue(pi.x-pj.x);
    }
    for(int& j : bneighbors)
    {
        Boundary& bj = m_boundaries[j];
        pi.rho += pi.fluidParams().monaghanKernel().monaghanValue(pi.x-bj.x)*bj.psi;
    }
}

void System::computeNormal(int i)
{
    //Compute normal
    Particle& pi=m_particles[i];
    std::vector< int > & neighbors = m_particles[i].fluidNeighbor;
    Vec3r n(0.0);
    Vec3r gradient(0.0);
    for(int& j : neighbors)
    {
        if(i!=j)
        {
            Particle& pj = m_particles[j];
            m_fluidParams.monaghanKernel().monaghanGradient(pi.x-pj.x, gradient);
            n += (m_fluidParams.mass()/pj.rho)*gradient;
        }
    }
    pi.n = m_fluidParams.smoothingRadius()*n;
}

bool System::isSurfaceParticle(int i, HReal treshold)
{
    HReal n_length = m_particles[i].n.lengthSquared();
    if( n_length > treshold )
    {
        return true;
    }
    else
    {
        return false;
    }
}

std::vector<Particle> System::getSurfaceParticle()
{
#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<m_particleNumber; ++i)
        computeDensity(i);

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<m_particleNumber; ++i)
        computeNormal(i);

    HReal treshold = 0.05;
    std::vector<Particle> surfaceParticles;
    for(int i=0; i<m_particleNumber; ++i)
    {
        if( isSurfaceParticle(i, treshold) )
            surfaceParticles.push_back(m_particles[i]);
    }
    return surfaceParticles;
}

void System::computeAdvectionForces(int i)
{
    Particle& pi=m_particles[i];
    pi.f_adv.fill(0.0);
    for(int& j : pi.fluidNeighbor)
    {
        computeViscosityForces(i, j);
        computeSurfaceTensionForces(i, j);
    }
    for(int& j : pi.boundaryNeighbor)
    {
        computeBoundaryFrictionForces(i, j);
        computeBoundaryAdhesionForces(i, j);
    }
    pi.f_adv+=m_gravity*m_fluidParams.mass();
}

void System::predictVelocity(int i)
{
    Particle& pi=m_particles[i];
    pi.v_adv = pi.v + (m_dt/m_fluidParams.mass())*pi.f_adv;
}

void System::predictDensity(int i)
{
    Particle& pi=m_particles[i];
    std::vector<int>& fneighbors=pi.fluidNeighbor;
    std::vector<int>& bneighbors=pi.boundaryNeighbor;
    HReal fdrho=0.0, bdrho=0.0;
    Vec3r gradient(0.0);

    for(int& j : fneighbors)
    {
        if(i!=j)
        {
            Particle& pj=m_particles[j];
            m_fluidParams.monaghanKernel().monaghanGradient(pi.x-pj.x, gradient);
            Vec3r vij_adv=pi.v_adv-pj.v_adv;
            fdrho+=m_fluidParams.mass()*Vec3r::dotProduct(vij_adv, gradient);
        }
    }

    for(int& j: bneighbors)
    {
        Boundary& bj=m_boundaries[j];
        Vec3r vb(0.1), v(0.0); v = pi.v_adv - vb; //vb(t+dt)
        m_fluidParams.monaghanKernel().monaghanGradient(pi.x-bj.x, gradient);
        bdrho+=bj.psi*Vec3r::dotProduct(v,gradient);
    }

    pi.rho_adv = pi.rho + m_dt*( fdrho + bdrho );
}

void System::computeSumDijPj(int i)
{
    Particle& pi=m_particles[i];
    std::vector<int>& fneighbors=pi.fluidNeighbor;
    pi.sum_dij.fill(0.0);
    for(int& j : fneighbors)
    {
        if(i!=j)
        {
            Particle& pj=m_particles[j];
            Vec3r gradient(0.0);
            m_fluidParams.monaghanKernel().monaghanGradient(pi.x-pj.x, gradient);
            pi.sum_dij+=(-m_fluidParams.mass()/pow(pj.rho,2))*pj.p_l*gradient;
        }
    }
    pi.sum_dij *= pow(m_dt,2);
}

void System::computeViscosityForces(int i, int j)
{
    Particle& pi=m_particles[i];
    Particle& pj=m_particles[j];
    Vec3r r = pi.x - pj.x;
    Vec3r vij = pi.v - pj.v;
    HReal dotVijRij = Vec3r::dotProduct(vij,r);
    if(dotVijRij < 0)
    {
        Vec3r gradient_i(0.0),gradient_j(0.0),gradient(0.0);
        pi.fluidParams().monaghanKernel().monaghanGradient(r, gradient_i);
        pj.fluidParams().monaghanKernel().monaghanGradient(r, gradient_j);
        gradient = 0.5*(gradient_i+gradient_j);

        HReal epsilon=0.01;
        HReal Hij = 0.5*(pi.fluidParams().smoothingRadius()+pj.fluidParams().smoothingRadius());
        HReal Rhoij = (pi.rho+pj.rho);
        HReal Cij = 0.5*(pi.fluidParams().soundSpeed()+pj.fluidParams().soundSpeed());
        HReal Vij = 0.5*(pi.fluidParams().viscosity()+pj.fluidParams().viscosity());
        HReal nu = (Vij*Hij*Cij)/Rhoij;
        HReal Pij = -nu * ( dotVijRij / (r.lengthSquared() + epsilon*Hij*Hij) );
        pi.f_adv += -pi.fluidParams().mass()*pj.fluidParams().mass()*Pij*gradient;
    }
}

void System::computeBoundaryFrictionForces(int i, int j)
{
    Particle& pi=m_particles[i];
    Boundary& bj=m_boundaries[j];
    Vec3r vij = pi.v;//-pj.v;
    Vec3r xij= pi.x - bj.x;
    HReal dotVijRij = Vec3r::dotProduct(vij,xij);
    if(dotVijRij<0)
    {
        Vec3r gradient(0.0);
        HReal epsilon=0.01;
        HReal nu = (m_boundaryParams.friction()*m_fluidParams.smoothingRadius()*m_fluidParams.soundSpeed())/(2.0*pi.rho);
        HReal Pij = -nu * ( std::min(dotVijRij,0.0) / (xij.lengthSquared() + epsilon*m_fluidParams.smoothingRadius()*m_fluidParams.smoothingRadius()) );
        m_fluidParams.monaghanKernel().monaghanGradient(xij, gradient);
        pi.f_adv += -m_fluidParams.mass()*bj.psi*Pij*gradient;
    }
}

void System::computeSurfaceTensionForces(int i, int j)
{
    if(i!=j)
    {
        Particle& pi=m_particles[i];
        Particle& pj=m_particles[j];
        if(pi.isSurface==true || pj.isSurface==true)
        {
            Vec3r r = pi.x - pj.x;
            HReal kij = (pi.fluidParams().density()+pj.fluidParams().density())/(pi.rho+pj.rho);
            HReal l = r.length();
            HReal Kernelij = 0.5*(pi.fluidParams().akinciKernel().cohesionValue(l)+pj.fluidParams().akinciKernel().cohesionValue(l));
            HReal Cij = 0.5*(pi.fluidParams().cohesion()+pj.fluidParams().cohesion());
            Vec3r cohesionForce = -(Cij*pi.fluidParams().mass()*pj.fluidParams().mass()*Kernelij/l) * r;
            Vec3r nij = pi.n-pj.n;
            Vec3r curvatureForce = -pi.fluidParams().cohesion()*pi.fluidParams().mass()*nij;
            pi.f_adv += kij*(cohesionForce+curvatureForce);
        }
    }
}

void System::computeBoundaryAdhesionForces(int i, int j)
{
    Particle& pi=m_particles[i];
    Boundary& bj=m_boundaries[j];
    Vec3r xij= pi.x - bj.x;
    HReal l = xij.length();
    pi.f_adv += -(m_boundaryParams.adhesion()*m_fluidParams.mass()*m_boundaries[j].psi*m_fluidParams.akinciKernel().adhesionValue(l)/l)*xij;
}

Vec3r System::computeDij(int i, int j)
{
    Particle& pi=m_particles[i];
    Particle& pj=m_particles[j];
    Vec3r gradient(0.0);
    m_fluidParams.monaghanKernel().monaghanGradient(pi.x-pj.x, gradient);
    Vec3r d=-(m_dt*m_dt*m_fluidParams.mass())/pow(pj.rho,2)*gradient;
    return d;
}

void System::computeWCSPHPressure(int i)
{
    Particle& pi=m_particles[i];
    HReal stiffness = 1000.0;
    pi.p = stiffness*(pi.rho-m_fluidParams.density());
}

void System::computeIISPHPressure(int i)
{
    Particle& pi=m_particles[i];
    std::vector<int>& fneighbors=pi.fluidNeighbor, bneighbors=pi.boundaryNeighbor;
    HReal fsum=0.0, bsum=0.0, omega=0.5;

    for(int& j : fneighbors)
    {
        if(i!=j)
        {
            Particle& pj=m_particles[j];
            Vec3r gradient_ij(0.0), dji=computeDij(j, i);
            m_fluidParams.monaghanKernel().monaghanGradient(pi.x-pj.x, gradient_ij);
            Vec3r aux = pi.sum_dij - (pj.dii_fluid+pj.dii_boundary)*pj.p_l - (pj.sum_dij - dji*pi.p_l);
            fsum+=m_fluidParams.mass()*Vec3r::dotProduct(aux, gradient_ij);
        }
    }

    for(int& j : bneighbors)
    {
        Boundary& bj=m_boundaries[j];
        Vec3r gradient(0.0), r(0.0); r=pi.x-bj.x;
        m_fluidParams.monaghanKernel().monaghanGradient(r, gradient);
        bsum+=bj.psi*Vec3r::dotProduct(pi.sum_dij,gradient);
    }

    HReal previousPl = pi.p_l;
    pi.rho_corr = pi.rho_adv + fsum + bsum;
    if(std::abs(pi.aii)>std::numeric_limits<HReal>::epsilon())
        pi.p_l = (1-omega)*previousPl + (omega/pi.aii)*(m_fluidParams.density() - pi.rho_corr);
    else
        pi.p_l = 0.0;
    pi.p = std::max(pi.p_l,0.0);
    pi.p_l = pi.p;
    pi.rho_corr += pi.aii*previousPl;
}

void System::computePressureForce(int i)
{
    Particle& pi=m_particles[i];
    pi.f_p.fill(0.0);
    std::vector<int>& fneighbors=pi.fluidNeighbor;
    std::vector<int>& bneighbors=pi.boundaryNeighbor;

    //Fluid Pressure Force
    for(int& j : fneighbors)
    {
        computeFluidPressureForce(i, j);
    }

    //Boundary Pressure Force [Akinci 2012]
    for(int& j : bneighbors )
    {
        computeBoundaryPressureForce(i, j);
    }
}

void System::computeFluidPressureForce(int i, int j)
{
    Vec3r gradient_i(0.0), gradient_j(0.0), gradient;
    Particle& pi=m_particles[i];
    Particle& pj=m_particles[j];
    pi.fluidParams().monaghanKernel().monaghanGradient(pi.x-pj.x, gradient_i);
    pj.fluidParams().monaghanKernel().monaghanGradient(pi.x-pj.x, gradient_j);
    gradient = 0.5*(gradient_i+gradient_j);
    if( i!=j )
    {
        pi.f_p += -pi.fluidParams().mass()*pj.fluidParams().mass()*( pi.p/pow(pi.rho,2) + pj.p/pow(pj.rho,2) ) * gradient;
    }
}

void System::computeBoundaryPressureForce(int i, int j)
{
    Vec3r gradient(0.0);
    Particle& pi=m_particles[i];
    Boundary& bj=m_boundaries[j];
    pi.fluidParams().monaghanKernel().monaghanGradient(pi.x-bj.x, gradient);
    pi.f_p += -pi.fluidParams().mass()*bj.psi*( pi.p/pow(pi.rho,2) ) * gradient;
}

void System::initializePressure(int i)
{
    Particle& pi=m_particles[i];
    pi.p_l=0.5*pi.p;
}

void System::computeError()
{
    m_averageDensity = 0.0;
    for(int i=0; i<m_particleNumber; ++i)
        m_averageDensity+=m_particles[i].rho_corr;
    m_averageDensity /= m_particleNumber;
}


void System::computeDii_Boundary(int i)
{
    Particle& pi=m_particles[i];
    pi.dii_boundary.fill(0.0);
    for(int& j : pi.boundaryNeighbor)
    {
        Boundary& bj=m_boundaries[j];
        Vec3r gradient(0.0);
        m_fluidParams.monaghanKernel().monaghanGradient(pi.x-bj.x, gradient);
        pi.dii_boundary+=(-m_dt*m_dt*bj.psi/pow(pi.rho,2))*gradient;
    }
}

void System::computeDii_Fluid(int i)
{
    Particle& pi=m_particles[i];
    pi.dii_fluid.fill(0.0);
    pi.dii_boundary.fill(0.0);
    for(int& j : pi.fluidNeighbor)
    {
        if(i!=j)
        {
            Particle& pj=m_particles[j];
            Vec3r gradient(0.0);
            m_fluidParams.monaghanKernel().monaghanGradient(pi.x-pj.x, gradient);
            pi.dii_fluid+=(-m_dt*m_dt*m_fluidParams.mass()/pow(pi.rho,2))*gradient;
        }
    }
}

void System::computeDii(int i)
{
    Particle& pi=m_particles[i];
    pi.dii_fluid.fill(0.0);
    pi.dii_boundary.fill(0.0);
    for(int& j : pi.fluidNeighbor)
    {
        if(i!=j)
        {
            Particle& pj=m_particles[j];
            Vec3r gradient(0.0);
            m_fluidParams.monaghanKernel().monaghanGradient(pi.x-pj.x, gradient);
            pi.dii_fluid+=(-m_dt*m_dt*m_fluidParams.mass()/pow(pi.rho,2))*gradient;
        }
    }
    for(int& j : pi.boundaryNeighbor)
    {
        Boundary& bj=m_boundaries[j];
        Vec3r gradient(0.0);
        m_fluidParams.monaghanKernel().monaghanGradient(pi.x-bj.x, gradient);
        pi.dii_boundary+=(-m_dt*m_dt*bj.psi/pow(pi.rho,2))*gradient;
    }
}

void System::computeAii( int i)
{
    Particle& pi=m_particles[i]; pi.aii=0.0;
    for(int& j : pi.fluidNeighbor)
    {
        if(i!=j)
        {
            Particle& pj=m_particles[j];
            Vec3r dji=computeDij(j,i);
            Vec3r gradient_ij(0.0);
            m_fluidParams.monaghanKernel().monaghanGradient(pi.x-pj.x, gradient_ij);
            pi.aii+=m_fluidParams.mass()*Vec3r::dotProduct((pi.dii_fluid+pi.dii_boundary)-dji,gradient_ij);
        }
    }
    for(int& j : pi.boundaryNeighbor)
    {
        Boundary& bj=m_boundaries[j];
        Vec3r gradient_ij(0.0);
        m_fluidParams.monaghanKernel().monaghanGradient(pi.x-bj.x, gradient_ij);
        pi.aii+=bj.psi*Vec3r::dotProduct(pi.dii_fluid+pi.dii_boundary,gradient_ij);
    }
}

void System::getNearestNeighbor(const int i, const HReal radius)
{
    Particle& p = m_particles[i];
    p.fluidNeighbor.clear();
    p.boundaryNeighbor.clear();

    std::vector<int> neighborCell;
    m_gridInfo.get27Neighbors(neighborCell, p.x, radius);

    for(size_t i=0; i<neighborCell.size(); ++i)
    {
        std::vector<int>& bNeighborCell = m_boundaryGrid[neighborCell[i]];
        for(size_t j=0; j<bNeighborCell.size(); ++j)
        {
            int bParticleId = m_boundaryGrid[neighborCell[i]][j];
            Boundary& bParticle = m_boundaries[bParticleId];
            Vec3r d = bParticle.x-p.x;
            if( d.lengthSquared()<radius*radius )
                p.boundaryNeighbor.push_back(bParticleId);
        }

        std::vector<int>& fNeighborCell = m_fluidGrid[neighborCell[i]];
        for(size_t j=0; j< fNeighborCell.size(); ++j)
        {
            int fParticleId = m_fluidGrid[neighborCell[i]][j];
            Particle& fParticle = m_particles[fParticleId];
            Vec3r d = fParticle.x-p.x;
            if( d.lengthSquared()<radius*radius)
                p.fluidNeighbor.push_back(fParticleId);
        }
    }
}

void System::getNearestNeighbor(std::vector< int >& neighbor, const std::vector< std::vector<int> >& grid, const Vec3r &x)
{
    std::vector<int> neighborCell;
    m_gridInfo.get27Neighbors(neighborCell, x, m_gridInfo.spacing());
    neighbor.clear();
    for(size_t i=0; i<neighborCell.size(); ++i)
    {
        for(size_t j=0; j<grid[neighborCell[i]].size(); ++j)
        {
            neighbor.push_back(grid[neighborCell[i]][j]);
        }
    }
}

void System::computeBoundaryVolume()
{
    for(int i=0; i<m_boundaryNumber; ++i)
    {
        HReal densityNumber=0.0;
        std::vector<int> neighbors;
        getNearestNeighbor(neighbors, m_boundaryGrid, m_boundaries[i].x);
        for(int& j : neighbors)
            densityNumber += m_fluidParams.monaghanKernel().monaghanValue(m_boundaries[i].x-m_boundaries[j].x);
        m_boundaries[i].psi = m_fluidParams.density()/densityNumber;
    }
}

void System::computeMeanDensity()
{
    m_meanDensity=0.0;
    for(int i=0; i<m_particleNumber; ++i)
    {
        m_meanDensity+=m_particles[i].rho;
    }
    m_meanDensity/=m_particleNumber;
}

void System::computeDensityFluctuation()
{
    m_densityFluctuation=m_meanDensity-m_fluidParams.density();
}

void System::computeVolume()
{
    m_realVolume=0.0;
    for(int i=0; i<m_particleNumber; ++i)
    {
        m_realVolume += m_particles[i].fluidParams().mass()/m_particles[i].rho;
    }
}

std::vector< Vec3r > System::getPosition()
{
    std::vector<Vec3r > pos; for(int i=0; i<m_particleNumber; ++i){pos.push_back(m_particles[i].x);} return pos;
}

std::vector< Vec3r > System::getVelocity()
{
    std::vector<Vec3r > vel; for(int i=0; i<m_particleNumber; ++i){vel.push_back(m_particles[i].v);} return vel;
}

std::vector< Vec3r > System::getNormal()
{
    std::vector<Vec3r > normal; for(int i=0; i<m_particleNumber; ++i){normal.push_back(m_particles[i].n);} return normal;
}

std::vector< HReal > System::getDensity()
{
    std::vector<HReal> density; for(int i=0; i<m_particleNumber; ++i){density.push_back(m_particles[i].rho);} return density;
}

std::vector< HReal > System::getMass()
{
    std::vector<HReal> o_mass; for(int i=0; i<m_particleNumber; ++i){o_mass.push_back(m_particles[i].fluidParams().mass());} return o_mass;
}

void System::setGravity(const Vec3r& _gravity)
{
    m_gravity = _gravity;
}

const Vec3r& System::getGravity()
{
    return m_gravity;
}

void System::setParameters( int _wishedNumber, HReal _volume, HReal _density )
{
    m_time = 0.0;
    m_countTime = 0.0;
    m_countExport = 0;
    m_particleNumber = 0;
    m_boundaryNumber = 0;

    m_fluidParams = FluidParams(_wishedNumber, _volume, _density, 0.1, 0.05);
    m_boundaryParams = BoundaryParams( m_fluidParams.smoothingRadius()/2.0, 0.0001, 1.0);

    m_dt = 0.0005;
    m_averageDensity = 0.0;
    m_maxPressureSolveIterationNb =2;
    m_maxDensityError = 1.0;
}

void System::init()
{  
    mortonSort();

    m_boundaryGrid.resize(m_gridInfo.size());
    for(size_t i=0; i<m_boundaries.size(); ++i)
    {
        int id = m_gridInfo.cellId(m_boundaries[i].x);
        if(m_gridInfo.isInside(id))
            m_boundaryGrid[id].push_back(i);
    }

    m_fluidGrid.resize(m_gridInfo.size());

    //Init simulation values
    computeBoundaryVolume();
    prepareGrid();

    for(size_t i=0; i<m_particles.size(); ++i)
    {
        m_particles[i].isSurface = true;
    }

    debugFluid();
}

void System::addBoundaryBox(const Vec3r& offset, const Vec3r& scale, const BoundaryParams &boundaryParams)
{
    std::vector<Vec3r> positions = getBoxSampling(offset, scale, m_fluidParams.smoothingRadius());
    for(const Vec3r& x : positions)
    {
        m_boundaries.push_back(Boundary(x,Vec3r(0.0),0.0,boundaryParams));
        m_boundaryNumber++;
    }
    m_gridInfo.update(offset-Vec3r(2.0*m_fluidParams.smoothingRadius()), scale+Vec3r(4.0*m_fluidParams.smoothingRadius()), 2.0*m_fluidParams.smoothingRadius());
}

void System::addBoundarySphere(const Vec3r& offset, const HReal& radius, const BoundaryParams &boundaryParams)
{
    std::vector<Vec3r> samples = getSphereSampling(offset, radius, m_fluidParams.smoothingRadius(), m_fluidParams.smoothingRadius());
    for(size_t i=0; i<samples.size(); ++i)
    {
        m_boundaries.push_back(Boundary(samples[i],Vec3r(0.0),0.0,boundaryParams));
        m_boundaryNumber++;
    }
}

void System::addBoundaryHemiSphere(const Vec3r& offset, const HReal& radius, const BoundaryParams &boundaryParams)
{
    std::vector<Vec3r> samples = getHemiSphereSampling(offset, radius, m_fluidParams.smoothingRadius(), m_fluidParams.smoothingRadius());
    for(size_t i=0; i<samples.size(); ++i)
    {
        m_boundaries.push_back(Boundary(samples[i],Vec3r(0.0),0.0,boundaryParams));
        m_boundaryNumber++;
    }
}

void System::addBoundaryDisk(const Vec3r& offset, const HReal& radius, const BoundaryParams &boundaryParams)
{
    std::vector<Vec3r> samples = getDiskSampling(offset, radius, m_fluidParams.smoothingRadius());
    for(size_t i=0; i<samples.size(); ++i)
    {
        m_boundaries.push_back(Boundary(samples[i],Vec3r(0.0),0.0,boundaryParams));
        m_boundaryNumber++;
    }
}

void System::translateBoundaries(const Vec3r& t)
{
    for(size_t i=0; i<m_boundaries.size(); ++i)
    {
        m_boundaries[i].x += t;
    }
}

void System::translateParticles(const Vec3r& t)
{
    for(size_t i=0; i<m_particles.size(); ++i)
    {
        m_particles[i].x += t;
    }
}

void System::addParticleSphere(const Vec3r& centre, const HReal radius, const FluidParams &fluidParams)
{
    std::vector<Vec3r> positions = getBallSampling(centre, radius, m_fluidParams.smoothingRadius());
    for(Vec3r& x : positions)
    {
        m_particles.push_back( Particle(x,Vec3r(0.0,0.0,0.0), fluidParams) );
        m_particleNumber++;
    }
}

void System::addParticleSource(const ParticleSource& s)
{
    m_pSources.push_back(s);
}

void System::addParticleBox(const Vec3r& offset, const Vec3r& scale, const FluidParams &fluidParams)
{
    std::vector<Vec3r> positions = getCubeSampling(offset, scale, m_fluidParams.smoothingRadius());
    for(Vec3r & x : positions)
    {
        m_particles.push_back( Particle(x, Vec3r(0.0,0.0,0.0), fluidParams) );
        m_particleNumber++;
    }
}

void System::addFluidParticle(const Vec3r& x, const Vec3r& v, const FluidParams &fluidParams)
{
    m_particles.push_back( Particle(x,v,fluidParams) );
    m_particleNumber++;
}

void System::addBoundaryParticle(const Vec3r& x, const Vec3r& v, const BoundaryParams &boundaryParams)
{
    m_boundaries.push_back( Boundary(x,v,0.0,boundaryParams) );
    m_boundaryNumber++;
}

void System::addBoundaryMesh(const char* filename, const BoundaryParams &boundaryParams)
{
    TriMesh mesh(filename);
    std::vector<Vec3r> samples;
    AkinciMeshSampling(mesh, m_fluidParams.smoothingRadius()/2.0, samples);
    Vec3r minBB(std::numeric_limits<HReal>::max()), maxBB(-std::numeric_limits<HReal>::max());
    for(size_t i=0; i<samples.size(); ++i)
    {
        for(int j=0; j<3; ++j)
        {
            minBB[j] = std::min(samples[i][j], minBB[j]);
            maxBB[j] = std::max(samples[i][j], maxBB[j]);
        }
        m_boundaries.push_back(Boundary(samples[i],Vec3r(0.0),0.0, boundaryParams));
        m_boundaryNumber++;
    }
    Vec3r offset = minBB;
    Vec3r scale = maxBB-minBB;
    m_gridInfo.update(offset-Vec3r(2.0*m_fluidParams.smoothingRadius()), scale+Vec3r(4.0*m_fluidParams.smoothingRadius()), 2.0*m_fluidParams.smoothingRadius());
}

bool pairCompare( const std::pair<int,int>& e1, const std::pair<int,int>& e2 )
{
    return (e1.second < e2.second);
}

void System::mortonSort()
{
    std::vector< std::pair<int, int> > particleZindex;

    //Fill particleZindex with particle index and Z-index
    for(int i = 0; i < m_particleNumber; ++i)
    {
        Vec3i _gridIndex = m_gridInfo.worldToGrid(m_particles[i].x);
        std::array<int,3> gridIndex;
        for(int j=0; j<3; ++j)
            gridIndex[j] = _gridIndex[j];
        int zindex = mortonNumber( gridIndex );
        std::pair<int,int> paire(i, zindex);
        particleZindex.push_back(paire);
    }

    //Sort according to z-index
    std::sort( particleZindex.begin(), particleZindex.end(), pairCompare );

    //Move particles according to z-index
    std::vector< Particle > oldParticles = m_particles;

    //HReal min = particleZindex[0].second;
    //HReal max = particleZindex[particleNumber-1].second;

    for(int i = 0; i < m_particleNumber; ++i)
    {
        std::pair<int,int>& paire = particleZindex[i];
        if(i != paire.first)
        {
            m_particles[i] = oldParticles[paire.first];
        }
        //HReal color = (paire.second-min)/(max-min);
        //particles[i].c[0] = color;
        //particles[i].c[1] = 0;
        //particles[i].c[2] = 0;
    }
}

void System::prepareGrid()
{
    if( m_countTime%100 == 0 )
        mortonSort();

    for(size_t i=0; i<m_fluidGrid.size(); ++i)
        m_fluidGrid[i].clear();

    for(size_t i=0; i<m_particles.size(); ++i)
    {
        int id = m_gridInfo.cellId(m_particles[i].x);
        if(m_gridInfo.isInside(id))
            m_fluidGrid[id].push_back(i);
    }

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i = 0; i < m_particleNumber; ++i)
        getNearestNeighbor(i, 2.0*m_particles[i].fluidParams().smoothingRadius());
}

void System::predictAdvection()
{
#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<m_particleNumber; ++i)
        computeDensity(i);

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<m_particleNumber; ++i)
        computeNormal(i);

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<m_particleNumber; ++i)
    {
        computeAdvectionForces(i);
        predictVelocity(i);
        computeDii(i);
    }

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<m_particleNumber; ++i)
    {
        predictDensity(i);
        initializePressure(i);
        computeAii(i);
    }
}

void System::pressureSolve()
{
    int l=0; m_averageDensity = 0.0;

    while(((m_averageDensity-m_fluidParams.density())>m_maxDensityError ) ||
          (l<m_maxPressureSolveIterationNb) )
    {
#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
        for(int i=0; i<m_particleNumber; ++i)
            computeSumDijPj(i);

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
        for(int i=0; i<m_particleNumber; ++i)
        {
            computeWCSPHPressure(i);
            //computeIISPHPressure(i);
        }

        computeError();

        ++l;

        //debugIteration(l);
    }
}

void System::integration()
{
    m_countTime++; m_time+=m_dt;

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<m_particleNumber; ++i)
        computePressureForce(i);

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<m_particleNumber; ++i)
    {
        Particle& pi=m_particles[i];
        pi.v = pi.v_adv + (m_dt*pi.f_p)/pi.fluidParams().mass();
        pi.x += m_dt*pi.v;
    }
}

void System::computeSimulationStep()
{

    prepareGrid();
    predictAdvection();
    pressureSolve();
    integration();

    applySources();
    applySinks();
    computeStats();
}

void System::applySources()
{
    for(ParticleSource& s : m_pSources)
    {
        std::vector<Particle> p_new = s.apply(this->m_time);
        for(const Particle& p : p_new)
        {
            m_particles.push_back(p);
            m_particleNumber++;
        }
    }
}

void System::applySinks()
{
}

void System::computeStats()
{
    computeMeanDensity();
    computeVolume();
    computeDensityFluctuation();
}

void System::debugIteration(int l)
{
    std::cout << "Do nothing for now" << std::endl;
//    std::cout.precision(10);
//    std::cout << "rest density " << m_fluidParams.density() << std::endl;
//    std::cout << "rho avg : " << m_averageDensity << std::endl;
//    std::cout << "l : " << l << std::endl;
}

void System::debugFluid()
{
    std::cout << "Do nothing for now" << std::endl;
//    std::cout << "Particle Number : " << m_particleNumber << std::endl;
//    std::cout << "Boundary Number : " << m_boundaryNumber << std::endl;
//    std::cout << "Smoothing Radius : " << m_fluidParams.smoothingRadius() << std::endl;
//    std::cout << "Radius : " << m_fluidParams.radius() << std::endl;
//    std::cout << "Speed sound : " << m_fluidParams.soundSpeed() << std::endl;
//    std::cout << "Timestep : " << m_dt << std::endl;

//    std::cout << std::endl;
//    m_gridInfo.info();
}

void System::write(const char * filename, std::vector<HReal> data)
{
    std::ofstream outputFile;
    outputFile.open(filename);
    outputFile.precision(16);
    for(unsigned int i=0; i <data.size(); ++i)
    {
        outputFile << data[i] << "\n";
    }
    outputFile.close();
}

void System::write(const char * filename, std::vector<Vec3r > data)
{
    std::ofstream outputFile;
    outputFile.open(filename);
    outputFile.precision(16);
    for(unsigned int i=0; i <data.size(); ++i)
    {
        outputFile << data[i][0] << " " << data[i][1] <<" " <<  data[i][2] << "\n";
    }
    outputFile.close();
}

void System::exportState(const char * baseName)
{
    std::vector< Vec3r > x = getPosition();
    std::vector< Vec3r > v = getVelocity();
    std::vector< HReal > d = getDensity();
    std::vector< HReal > m = getMass();

    std::stringstream ss_padding;
    ss_padding.fill('0');
    ss_padding.width(5);
    ss_padding << m_countExport++;
    std::string padding = ss_padding.str();

    std::stringstream posFilename, velFilename, densFilename, massFilename;
    posFilename << baseName << "/position/position" << padding << ".txt";
    velFilename << baseName << "/velocity/velocity" << padding << ".txt";
    densFilename << baseName << "/density/density" << padding << ".txt";
    massFilename << baseName << "/mass/mass" << padding << ".txt";

    write(  posFilename.str().c_str(), x );
    write(  velFilename.str().c_str(), v );
    write(  densFilename.str().c_str(), d );
    write(  massFilename.str().c_str(), m );
}

HReal System::getTime()
{
    return m_time;
}

HReal & System::getTimeStepValue()
{
    return m_dt;
}

HReal & System::getMeanDensityValue()
{
    return m_meanDensity;
}

HReal & System::getDensityFluctuationValue()
{
    return m_densityFluctuation;
}

HReal & System::getRealVolumeValue()
{
    return m_realVolume;
}

int & System::getParticleNumber()
{
    return m_particleNumber;
}

HReal & System::getTimeStep()
{
    return m_dt;
}

const HReal& System::getTimeStep() const
{
    return m_dt;
}

}//namespace hokusai
