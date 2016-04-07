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
    countExport = 0;
    countTime = 0;
    particleNumber = 0;
    boundaryNumber = 0;

    volume = 0.0;
    restDensity = 0.0;
    mean_density = 0.0;
    density_fluctuation = 0.0;
    real_volume = 0.0;
    mass = 0.0;
    h = 0.0;
    fcohesion = 0.0;
    badhesion = 0.0;
    cs = 0.0;
    alpha = 0.0;
    boundaryH = 0.0;
    dt = 0.0;
    time = 0.0;
    rho_avg_l = 0.0;
    maxEta = 1.0;

    gravity = Vec3r(0,-9.81,0);

    a_kernel = AkinciKernel();
    p_kernel = MonaghanKernel();
    b_kernel = BoundaryKernel();

    gridInfo = GridUtility();
    particles = vector<Particle>();
    boundaries = vector<Boundary>();
}

System::System(int resolution)
{
    countExport = 0;
    countTime = 0;
    particleNumber = 0;
    boundaryNumber = 0;

    volume = 0.0;
    restDensity = 0.0;
    mean_density = 0.0;
    density_fluctuation = 0.0;
    real_volume = 0.0;
    mass = 0.0;
    h = 0.0;
    fcohesion = 0.0;
    badhesion = 0.0;
    cs = 0.0;
    alpha = 0.0;
    boundaryH = 0.0;
    dt = 0.0;
    time = 0.0;
    rho_avg_l = 0.0;
    maxEta = 1.0;

    gravity = Vec3r(0,-9.81,0);

    a_kernel = AkinciKernel();
    p_kernel = MonaghanKernel();
    b_kernel = BoundaryKernel();

    gridInfo = GridUtility();
    particles = vector<Particle>();
    boundaries = vector<Boundary>();

    setParameters(resolution, 1.0);
}

System::~System(){}

void System::computeRho(int i)
{
    Particle& pi=particles[i];
    vector<int>& fneighbors=pi.fluidNeighbor;
    vector<int>& bneighbors=pi.boundaryNeighbor;
    pi.rho=0.0;
    for(int& j : fneighbors)
    {
        Particle& pj=particles[j];
        pi.rho += mass*p_kernel.monaghanValue(pi.x-pj.x);
    }
    for(int& j : bneighbors)
    {
        Boundary& bj = boundaries[j];
        pi.rho += p_kernel.monaghanValue(pi.x-bj.x)*bj.psi;
    }
}

void System::computeNormal(int i)
{
    //Compute normal
    Particle& pi=particles[i];
    vector< int > & neighbors = particles[i].fluidNeighbor;
    Vec3r n(0.0);
    Vec3r gradient(0.0);
    for(int& j : neighbors)
    {
        if(i!=j)
        {
            Particle& pj = particles[j];
            p_kernel.monaghanGradient(pi.x-pj.x, gradient);
            n += (mass/pj.rho)*gradient;
        }
    }
    pi.n = h*n;
}

bool System::isSurfaceParticle(int i, HReal treshold)
{
    HReal n_length = particles[i].n.lengthSquared();
    if( n_length > treshold )
    {
        return true;
    }
    else
    {
        return false;
    }
}

vector<Particle> System::getSurfaceParticle()
{
#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<particleNumber; ++i)
        computeRho(i);

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<particleNumber; ++i)
        computeNormal(i);

    HReal treshold = 0.05;
    vector<Particle> surfaceParticles;
    for(int i=0; i<particleNumber; ++i)
    {
        if( isSurfaceParticle(i, treshold) )
            surfaceParticles.push_back(particles[i]);
    }
    return surfaceParticles;
}

void System::computeAdvectionForces(int i)
{
    Particle& pi=particles[i];
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
    pi.f_adv+=gravity*mass;
}

void System::predictVelocity(int i)
{
    Particle& pi=particles[i];
    pi.v_adv = pi.v + (dt/mass)*pi.f_adv;
}

void System::predictRho(int i)
{
    Particle& pi=particles[i];
    vector<int>& fneighbors=pi.fluidNeighbor;
    vector<int>& bneighbors=pi.boundaryNeighbor;
    HReal fdrho=0.0, bdrho=0.0;
    Vec3r gradient(0.0);

    for(int& j : fneighbors)
    {
        if(i!=j)
        {
            Particle& pj=particles[j];
            p_kernel.monaghanGradient(pi.x-pj.x, gradient);
            Vec3r vij_adv=pi.v_adv-pj.v_adv;
            fdrho+=mass*Vec3r::dotProduct(vij_adv, gradient);
        }
    }

    for(int& j: bneighbors)
    {
        Boundary& bj=boundaries[j];
        Vec3r vb(0.1), v(0.0); v = pi.v_adv - vb; //vb(t+dt)
        p_kernel.monaghanGradient(pi.x-bj.x, gradient);
        bdrho+=bj.psi*Vec3r::dotProduct(v,gradient);
    }

    pi.rho_adv = pi.rho + dt*( fdrho + bdrho );
}

void System::computeSumDijPj(int i)
{
    Particle& pi=particles[i];
    vector<int>& fneighbors=pi.fluidNeighbor;
    pi.sum_dij.fill(0.0);
    for(int& j : fneighbors)
    {
        if(i!=j)
        {
            Particle& pj=particles[j];
            Vec3r gradient(0.0);
            p_kernel.monaghanGradient(pi.x-pj.x, gradient);
            pi.sum_dij+=(-mass/pow(pj.rho,2))*pj.p_l*gradient;
        }
    }
    pi.sum_dij *= pow(dt,2);
}

void System::computePressure(int i)
{
    Particle& pi=particles[i];
    vector<int>& fneighbors=pi.fluidNeighbor, bneighbors=pi.boundaryNeighbor;
    HReal fsum=0.0, bsum=0.0, omega=0.5;

    for(int& j : fneighbors)
    {
        if(i!=j)
        {
            Particle& pj=particles[j];
            Vec3r gradient_ij(0.0), dji=computeDij(j, i);
            p_kernel.monaghanGradient(pi.x-pj.x, gradient_ij);
            Vec3r aux = pi.sum_dij - (pj.dii_fluid+pj.dii_boundary)*pj.p_l - (pj.sum_dij - dji*pi.p_l);
            fsum+=mass*Vec3r::dotProduct(aux, gradient_ij);
        }
    }

    for(int& j : bneighbors)
    {
        Boundary& bj=boundaries[j];
        Vec3r gradient(0.0), r(0.0); r=pi.x-bj.x;
        p_kernel.monaghanGradient(r, gradient);
        bsum+=bj.psi*Vec3r::dotProduct(pi.sum_dij,gradient);
    }

    HReal previousPl = pi.p_l;
    pi.rho_corr = pi.rho_adv + fsum + bsum;
    if(std::abs(pi.aii)>std::numeric_limits<HReal>::epsilon())
        pi.p_l = (1-omega)*previousPl + (omega/pi.aii)*(restDensity - pi.rho_corr);
    else
        pi.p_l = 0.0;
    pi.p = std::max(pi.p_l,0.0);
    pi.p_l = pi.p;
    pi.rho_corr += pi.aii*previousPl;
}

void System::computePressureForce(int i)
{
    Particle& pi=particles[i];
    Vec3r gradient(0.0);
    pi.f_p.fill(0.0);
    vector<int>& fneighbors=pi.fluidNeighbor;
    vector<int>& bneighbors=pi.boundaryNeighbor;

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

void System::initializePressure(int i)
{
    Particle& pi=particles[i];
    pi.p_l=0.5*pi.p;
}

void System::computeError()
{
    rho_avg_l=0.0;
    for(int i=0; i<particleNumber; ++i)
        rho_avg_l+=particles[i].rho_corr;
    rho_avg_l /= particleNumber;
}


void System::computeDii_Boundary(int i)
{
    Particle& pi=particles[i];
    pi.dii_boundary.fill(0.0);
    for(int& j : pi.boundaryNeighbor)
    {
        Boundary& bj=boundaries[j];
        Vec3r gradient(0.0);
        p_kernel.monaghanGradient(pi.x-bj.x, gradient);
        pi.dii_boundary+=(-dt*dt*bj.psi/pow(pi.rho,2))*gradient;
    }
}

void System::computeDii_Fluid(int i)
{
    Particle& pi=particles[i];
    pi.dii_fluid.fill(0.0);
    pi.dii_boundary.fill(0.0);
    for(int& j : pi.fluidNeighbor)
    {
        if(i!=j)
        {
            Particle& pj=particles[j];
            Vec3r gradient(0.0);
            p_kernel.monaghanGradient(pi.x-pj.x, gradient);
            pi.dii_fluid+=(-dt*dt*mass/pow(pi.rho,2))*gradient;
        }
    }
}

void System::computeDii(int i)
{
    Particle& pi=particles[i];
    pi.dii_fluid.fill(0.0);
    pi.dii_boundary.fill(0.0);
    for(int& j : pi.fluidNeighbor)
    {
        if(i!=j)
        {
            Particle& pj=particles[j];
            Vec3r gradient(0.0);
            p_kernel.monaghanGradient(pi.x-pj.x, gradient);
            pi.dii_fluid+=(-dt*dt*mass/pow(pi.rho,2))*gradient;
        }
    }
    for(int& j : pi.boundaryNeighbor)
    {
        Boundary& bj=boundaries[j];
        Vec3r gradient(0.0);
        p_kernel.monaghanGradient(pi.x-bj.x, gradient);
        pi.dii_boundary+=(-dt*dt*bj.psi/pow(pi.rho,2))*gradient;
    }
}

void System::computeAii( int i)
{
    Particle& pi=particles[i]; pi.aii=0.0;
    for(int& j : pi.fluidNeighbor)
    {
        if(i!=j)
        {
            Particle& pj=particles[j];
            Vec3r dji=computeDij(j,i);
            Vec3r gradient_ij(0.0);
            p_kernel.monaghanGradient(pi.x-pj.x, gradient_ij);
            pi.aii+=mass*Vec3r::dotProduct((pi.dii_fluid+pi.dii_boundary)-dji,gradient_ij);
        }
    }
    for(int& j : pi.boundaryNeighbor)
    {
        Boundary& bj=boundaries[j];
        Vec3r gradient_ij(0.0);
        p_kernel.monaghanGradient(pi.x-bj.x, gradient_ij);
        pi.aii+=bj.psi*Vec3r::dotProduct(pi.dii_fluid+pi.dii_boundary,gradient_ij);
    }
}

void System::getNearestNeighbor(const int i, const HReal radius)
{
    Particle& p = particles[i];
    p.fluidNeighbor.clear();
    p.boundaryNeighbor.clear();

    std::vector<int> neighborCell;
    gridInfo.get27Neighbors(neighborCell, p.x, radius);

    for(size_t i=0; i<neighborCell.size(); ++i)
    {
        std::vector<int>& bNeighborCell = boundaryGrid[neighborCell[i]];
        for(size_t j=0; j<bNeighborCell.size(); ++j)
        {
            int bParticleId = boundaryGrid[neighborCell[i]][j];
            Boundary& bParticle = boundaries[bParticleId];
            Vec3r d = bParticle.x-p.x;
            if( d.lengthSquared()<radius*radius )
                p.boundaryNeighbor.push_back(bParticleId);
        }

        std::vector<int>& fNeighborCell = fluidGrid[neighborCell[i]];
        for(size_t j=0; j< fNeighborCell.size(); ++j)
        {
            int fParticleId = fluidGrid[neighborCell[i]][j];
            Particle& fParticle = particles[fParticleId];
            Vec3r d = fParticle.x-p.x;
            if( d.lengthSquared()<radius*radius)
                p.fluidNeighbor.push_back(fParticleId);
        }
    }
}

void System::getNearestNeighbor(vector< int >& neighbor, const vector< vector<int> >& grid, const Vec3r &x)
{
    std::vector<int> neighborCell;
    gridInfo.get27Neighbors(neighborCell, x, gridInfo.spacing());
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
    for(int i=0; i<boundaryNumber; ++i)
    {
        HReal densityNumber=0.0;
        vector<int> neighbors;
        getNearestNeighbor(neighbors, boundaryGrid, boundaries[i].x);
        for(int& j : neighbors)
            densityNumber += p_kernel.monaghanValue(boundaries[i].x-boundaries[j].x);
        boundaries[i].psi = restDensity/densityNumber;
    }
}

void System::computeMeanDensity()
{
    mean_density=0.0;
    for(int i=0; i<particleNumber; ++i)
    {
        mean_density+=particles[i].rho;
    }
    mean_density/=particleNumber;
}

void System::computeDensityFluctuation()
{
    density_fluctuation=mean_density-restDensity;
}

void System::computeVolume()
{
    real_volume=0.0;
    for(int i=0; i<particleNumber; ++i)
    {
        real_volume += mass/particles[i].rho;
    }
}

void System::setGravity(const Vec3r& _gravity)
{
    gravity = _gravity;
}

const Vec3r& System::getGravity()
{
    return gravity;
}

void System::setParameters( int _wishedNumber, HReal _volume )
{
    time = 0.0;
    countTime = 0.0;
    countExport = 0;
    particleNumber = 0;
    boundaryNumber = 0;
    volume = _volume;

    maxEta=1.0;
    restDensity = 1000;
    mass = (restDensity * volume) / _wishedNumber;
    particlePerCell = 33.8; //better
    h = 0.5*pow( HReal(3*volume*particlePerCell) / HReal(4*M_PI*_wishedNumber), 1.0/3.0);

    HReal eta = 0.01;
    HReal H = 0.1;
    HReal vf = sqrt( 2*9.81*H );
    cs = vf/(sqrt(eta));

    alpha = 0.1;
    fcohesion = 0.05;
    badhesion = 0.001;
    sigma=1.0;
    boundaryH = h/2.0; //boundaryH must be <= h (neighbor search purpose)

    dt = 0.004;

    p_kernel = MonaghanKernel( h );
    a_kernel = AkinciKernel( 2.0*h );
    b_kernel = BoundaryKernel( boundaryH, cs );
}

void System::init()
{  
    mortonSort();

    boundaryGrid.resize(gridInfo.size());
    for(size_t i=0; i<boundaries.size(); ++i)
    {
        int id = gridInfo.cellId(boundaries[i].x);
        if(gridInfo.isInside(id))
            boundaryGrid[id].push_back(i);
    }

    fluidGrid.resize(gridInfo.size());

//    for(size_t i=0; i<particles.size(); ++i)
//    {
//        int id = gridInfo.cellId(particles[i].x);
//        if(gridInfo.isInside(id))
//            fluidGrid[id].push_back(i);
//    }

    //Init simulation values
    computeBoundaryVolume();
    prepareGrid();

    for(size_t i=0; i<particles.size(); ++i)
    {
        particles[i].isSurface = true;
    }

//    for(int i=0; i<particleNumber; ++i)
//    {
//        computeRho(i);
//        particles[i].rho = restDensity;
//    }

//    for(int i=0; i<particleNumber; ++i)
//    {
//        computeDii(i);
//    }

//    for(int i=0; i<particleNumber; ++i)
//    {
//        computeAii(i);
//    }

    debugFluid();
}

void System::addBoundaryBox(const Vec3r& offset, const Vec3r& scale)
{
    int epsilon = 0;
    int widthSize = floor(scale[0]/h);
    int heightSize = floor(scale[1]/h);
    int depthSize = floor(scale[2]/h);

    //ZX plane - bottom
    for(int i = -epsilon; i <= widthSize+epsilon; ++i)
    {
        for(int j = -epsilon; j <= depthSize+epsilon; ++j)
        {
            Vec3r position(i*h, offset[1], j*h);
            boundaries.push_back(Boundary(position,Vec3r(0.0),0.0));
            boundaryNumber++;
        }
    }

    //ZX plane - top
    for(int i = -epsilon; i <= widthSize+epsilon; ++i)
    {
        for(int j = -epsilon; j <= depthSize+epsilon; ++j)
        {
            Vec3r position(i*h, offset[1]+scale[1], j*h);
            boundaries.push_back(Boundary(position,Vec3r(0.0),0.0));
            boundaryNumber++;
        }
    }

    //XY plane - back
    for(int i = -epsilon; i <= widthSize+epsilon; ++i)
    {
        for(int j = -epsilon; j <= heightSize+epsilon; ++j)
        {
            Vec3r position(i*h, j*h, offset[2]);
            boundaries.push_back(Boundary(position,Vec3r(0.0),0.0));
            boundaryNumber++;
        }
    }

    //XY plane - front
    for(int i = -epsilon; i <= widthSize+epsilon; ++i)
    {
        for(int j = -epsilon; j <= heightSize-epsilon; ++j)
        {
            Vec3r position(i*h, j*h, offset[2]+scale[2]);
            boundaries.push_back(Boundary(position,Vec3r(0.0),0.0));
            boundaryNumber++;
        }
    }

    //YZ plane - left
    for(int i = -epsilon; i <= heightSize+epsilon; ++i)
    {
        for(int j = -epsilon; j <= depthSize+epsilon; ++j)
        {
            Vec3r position(offset[0], i*h, j*h);
            boundaries.push_back(Boundary(position,Vec3r(0.0),0.0));
            boundaryNumber++;
        }
    }

    //YZ plane - right
    for(int i = -epsilon; i <= heightSize+epsilon; ++i)
    {
        for(int j = -epsilon; j <= depthSize+epsilon; ++j)
        {
            Vec3r position(offset[0]+scale[0], i*h, j*h);
            boundaries.push_back(Boundary(position,Vec3r(0.0),0.0));
            boundaryNumber++;
        }
    }

    gridInfo.update(offset-Vec3r(2.0*h), scale+Vec3r(4.0*h), 2.0*h);
}

void System::addBoundarySphere(const Vec3r& offset, const HReal& radius)
{
    std::vector<Vec3r> samples = getSphereSampling(offset, radius, h, h);
    for(size_t i=0; i<samples.size(); ++i)
    {
        boundaries.push_back(Boundary(samples[i],Vec3r(0.0),0.0));
        boundaryNumber++;
    }
}

void System::addBoundaryHemiSphere(const Vec3r& offset, const HReal& radius)
{
    std::vector<Vec3r> samples = getHemiSphereSampling(offset, radius, h, h);
    for(size_t i=0; i<samples.size(); ++i)
    {
        boundaries.push_back(Boundary(samples[i],Vec3r(0.0),0.0));
        boundaryNumber++;
    }
}

void System::addBoundaryDisk(const Vec3r& offset, const HReal& radius)
{
    std::vector<Vec3r> samples = getDiskSampling(offset, radius, h);
    for(size_t i=0; i<samples.size(); ++i)
    {
        boundaries.push_back(Boundary(samples[i],Vec3r(0.0),0.0));
        boundaryNumber++;
    }
}


void System::translateBoundaries(const Vec3r& t)
{
    for(size_t i=0; i<boundaries.size(); ++i)
    {
        boundaries[i].x += t;
    }
}

void System::translateParticles(const Vec3r& t)
{
    for(size_t i=0; i<particles.size(); ++i)
    {
        particles[i].x += t;
    }
}

void System::addParticleSphere(const Vec3r& centre, const HReal radius)
{
    Vec3r scale(2.0*radius, 2.0*radius, 2.0*radius);
    Vec3r offset = centre - Vec3r(radius, radius, radius);
    GridUtility grid(offset, scale, h);

    for(int i=0; i<grid.size(); ++i)
    {
        Vec3r _x = grid.gridToWorld(i);
        _x+=grid.spacing()/2.0;
        Vec3r _v(0,0,0);
        HReal l2 = (centre-_x).lengthSquared();
        if(l2<=(radius*radius))
        {
            particles.push_back( Particle(_x,_v) );
            particleNumber++;
        }
    }
}

void System::addParticleSource(const ParticleSource& s)
{
    p_sources.push_back(s);
}

void System::addParticleBox(const Vec3r& offset, const Vec3r& scale)
{
    int widthSize = floor(scale[0]/h);
    int heightSize = floor(scale[1]/h);
    int depthSize = floor(scale[2]/h);

    for(int i=0; i<widthSize; ++i)
    {
        for(int j=0; j<heightSize; ++j)
        {
            for(int k=0; k<depthSize; ++k)
            {
                Vec3r _x = offset + Vec3r(i*h,j*h,k*h);
                Vec3r _v(0,0,0);
                particles.push_back( Particle(_x,_v) );
                particleNumber++;
            }
        }
    }
}

void System::addParticleBox(HReal width, HReal height, HReal depth, HReal spacing)
{
    int widthSize = floor(width/spacing);
    int heightSize = floor(height/spacing);
    int depthSize = floor(depth/spacing);

    for(int i=0; i<widthSize; ++i)
    {
        for(int j=0; j<heightSize; ++j)
        {
            for(int k=0; k<depthSize; ++k)
            {
                Vec3r _x(i*spacing,j*spacing,k*spacing);
                Vec3r _v(0,0,0);
                particles.push_back( Particle(_x,_v) );
                particleNumber++;
            }
        }
    }
}


void System::createParticleVolume(Vec3r& pos, HReal width, HReal /*height*/, HReal depth, HReal spacing, int particleMax)
{
    int widthSize = floor( width/spacing );
    int depthSize = floor( depth/spacing );
    int count = 0;
    int j = 0;
    while(count < particleMax)
    {
        for(int i = 0; i <= widthSize; ++i)
        {
            for(int k = 0; k <= depthSize; ++k)
            {
                if(count < particleMax)
                {
                    Vec3r _x(pos[0]+i*spacing,pos[1]+j*spacing,pos[2]+k*spacing);
                    Vec3r _v(0,0,0);
                    particles.push_back( Particle(_x,_v) );
                    particleNumber++;
                }
                count++;
            }
        }
        j++;
    }
}

void System::addFluidParticle(const Vec3r& x, const Vec3r& v)
{
    particles.push_back( Particle(x,v) );
    particleNumber++;
}

void System::addBoundaryParticle(const Vec3r& x, const Vec3r& v)
{
    boundaries.push_back( Boundary(x,v) );
    boundaryNumber++;
}

void System::addBoundaryMesh(const char* filename)
{
    TriMesh mesh(filename);
    std::vector<Vec3r> samples;
    AkinciMeshSampling(mesh, h/2.0, samples);
    Vec3r minBB(std::numeric_limits<HReal>::max()), maxBB(-std::numeric_limits<HReal>::max());
    for(size_t i=0; i<samples.size(); ++i)
    {
        for(int j=0; j<3; ++j)
        {
            minBB[j] = std::min(samples[i][j], minBB[j]);
            maxBB[j] = std::max(samples[i][j], maxBB[j]);
        }
        boundaries.push_back(Boundary(samples[i],Vec3r(0.0),0.0));
        boundaryNumber++;
    }
    Vec3r offset = minBB;
    Vec3r scale = maxBB-minBB;
    gridInfo.update(offset-Vec3r(2.0*h), scale+Vec3r(4.0*h), 2.0*h);
}

bool pairCompare( const std::pair<int,int>& e1, const std::pair<int,int>& e2 )
{
    return (e1.second < e2.second);
}

void System::mortonSort()
{
    std::vector< std::pair<int, int> > particleZindex;

    //Fill particleZindex with particle index and Z-index
    for(int i = 0; i < particleNumber; ++i)
    {
        Vec3i _gridIndex = gridInfo.worldToGrid(particles[i].x);
        array<int,3> gridIndex;
        for(int j=0; j<3; ++j)
            gridIndex[j] = _gridIndex[j];
        int zindex = mortonNumber( gridIndex );
        std::pair<int,int> paire(i, zindex);
        particleZindex.push_back(paire);
    }

    //Sort according to z-index
    std::sort( particleZindex.begin(), particleZindex.end(), pairCompare );

    //Move particles according to z-index
    vector< Particle > oldParticles = particles;

    //HReal min = particleZindex[0].second;
    //HReal max = particleZindex[particleNumber-1].second;

    for(int i = 0; i < particleNumber; ++i)
    {
        std::pair<int,int>& paire = particleZindex[i];
        if(i != paire.first)
        {
            particles[i] = oldParticles[paire.first];
        }
        //HReal color = (paire.second-min)/(max-min);
        //particles[i].c[0] = color;
        //particles[i].c[1] = 0;
        //particles[i].c[2] = 0;
    }
}

void System::computeSurfaceParticle()
{
    for(size_t i=0; i<particles.size(); ++i)
    {
        particles[i].isSurface = false;
    }

    std::vector<int> tmpParticleStack;
    for(size_t i=0; i<particles.size(); ++i)
    {
        //Not good enough
        if( isSurfaceParticle(i, 0.2) || particles[i].fluidNeighbor.size() < 0.5*particlePerCell)
            tmpParticleStack.push_back(i);
    }

    std::set<int> surfaceParticles;
    for(size_t i=0; i<tmpParticleStack.size(); ++i)
    {
        surfaceParticles.insert( tmpParticleStack[i] );
        for(size_t j=0; j<particles[ tmpParticleStack[i] ].fluidNeighbor.size(); ++j)
        {
            surfaceParticles.insert( particles[ tmpParticleStack[i] ].fluidNeighbor[j] );
        }
    }

    for(int pId : surfaceParticles)
    {
        particles[pId].isSurface=true;
    }

    int surfaceParticle = 0;
    for(size_t i=0; i<particles.size(); ++i)
    {
        if(particles[i].isSurface == true)
        {
            surfaceParticle++;    
        }
    }
}

void System::prepareGrid()
{
    if( countTime%100 == 0 )
        mortonSort();

    for(size_t i=0; i<fluidGrid.size(); ++i)
        fluidGrid[i].clear();

    for(size_t i=0; i<particles.size(); ++i)
    {
        int id = gridInfo.cellId(particles[i].x);
        if(gridInfo.isInside(id))
            fluidGrid[id].push_back(i);
    }

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i = 0; i < particleNumber; ++i)
        getNearestNeighbor(i, 2.0*h);
}

void System::predictAdvection()
{
#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<particleNumber; ++i)
        computeRho(i);

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<particleNumber; ++i)
        computeNormal(i);

    computeSurfaceParticle();

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<particleNumber; ++i)
    {
        computeAdvectionForces(i);
        predictVelocity(i);
        computeDii(i);
    }

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<particleNumber; ++i)
    {
        predictRho(i);
        initializePressure(i);
        computeAii(i);
    }
}

void System::pressureSolve()
{
    int l=0; rho_avg_l = 0.0;

    while( ( (rho_avg_l-restDensity)>maxEta ) || (l<2) )
    {
#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
        for(int i=0; i<particleNumber; ++i)
            computeSumDijPj(i);

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
        for(int i=0; i<particleNumber; ++i)
        {
            computePressure(i);
        }

        computeError();

        ++l;

        //debugIteration(l);
    }

}

void System::integration()
{
    countTime++; time+=dt;

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<particleNumber; ++i)
        computePressureForce(i);

#ifdef HOKUSAI_USING_OPENMP
#pragma omp parallel for
#endif
    for(int i=0; i<particleNumber; ++i)
    {
        Particle& pi=particles[i];
        pi.v = pi.v_adv + (dt*pi.f_p)/mass;
        pi.x += dt*pi.v;
    }
}

void System::simulate()
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
    for(ParticleSource& s : p_sources)
    {
        std::vector<Particle> p_new = s.apply(this->time);
        for(const Particle& p : p_new)
        {
            particles.push_back(p);
            particleNumber++;
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
    std::cout.precision(10);
    std::cout << "rest density " << restDensity << std::endl;
    std::cout << "rho avg : " << rho_avg_l << std::endl;
    std::cout << "l : " << l << std::endl;
}

void System::debugFluid()
{
    std::cout << "Particle Number : " << particleNumber << std::endl;
    std::cout << "Boundary Number : " << boundaryNumber << std::endl;
    std::cout << "Smoothing Radius : " << h << std::endl;
    std::cout << "Radius : " << pow( 3.0*volume/(4*M_PI*particleNumber), 1.0/3.0 ) << std::endl;
    std::cout << "Speed sound : " << cs << std::endl;
    std::cout << "Timestep : " << dt << std::endl;

    std::cout << std::endl;
    gridInfo.info();
}

void System::write(const char * filename, vector<HReal> data)
{
    ofstream outputFile;
    outputFile.open(filename);
    outputFile.precision(16);
    for(unsigned int i=0; i <data.size(); ++i)
    {
        outputFile << data[i] << "\n";
    }
    outputFile.close();
}

void System::write(const char * filename, vector<Vec3r > data)
{
    ofstream outputFile;
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
    vector< Vec3r > x = getPosition();
    vector< Vec3r > v = getVelocity();
    vector< HReal > d = getDensity();
    vector< HReal > m = getMass();

    stringstream ss_padding;
    ss_padding.fill('0');
    ss_padding.width(5);
    ss_padding << countExport++;
    std::string padding = ss_padding.str();

    stringstream posFilename, velFilename, densFilename, massFilename;
    posFilename << baseName << "/position/position" << padding << ".txt";
    velFilename << baseName << "/velocity/velocity" << padding << ".txt";
    densFilename << baseName << "/density/density" << padding << ".txt";
    massFilename << baseName << "/mass/mass" << padding << ".txt";

    write(  posFilename.str().c_str(), x );
    write(  velFilename.str().c_str(), v );
    write(  densFilename.str().c_str(), d );
    write(  massFilename.str().c_str(), m );
}

}