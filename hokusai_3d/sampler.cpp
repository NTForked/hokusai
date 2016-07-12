#include "./../include/sampler.hpp"

/*
   Calculate the line segment PaPb that is the shortest route between
   two lines P1P2 and P3P4. Calculate also the values of mua and mub where
      Pa = P1 + mua (P2 - P1)
      Pb = P3 + mub (P4 - P3)
   Return FALSE if no solution exists.
*/
bool LineLineIntersect(
   const Vec3r& p1, const Vec3r& p2, const Vec3r& p3, const Vec3r& p4, Vec3r& pa, Vec3r& pb,
   Real & mua, Real & mub)
{
   Vec3r p13,p43,p21;
   Real d1343,d4321,d1321,d4343,d2121;
   Real numer,denom;

   p13[0] = p1[0] - p3[0];
   p13[1] = p1[1] - p3[1];
   p13[2] = p1[2] - p3[2];
   p43[0] = p4[0] - p3[0];
   p43[1] = p4[1] - p3[1];
   p43[2] = p4[2] - p3[2];
   if (std::abs(p43[0]) < std::numeric_limits<Real>::epsilon() && std::abs(p43[1]) < std::numeric_limits<Real>::epsilon() && std::abs(p43[2]) < std::numeric_limits<Real>::epsilon())
      return false;
   p21[0] = p2[0] - p1[0];
   p21[1] = p2[1] - p1[1];
   p21[2] = p2[2] - p1[2];
   if (std::abs(p21[0]) < std::numeric_limits<Real>::epsilon() && std::abs(p21[1]) < std::numeric_limits<Real>::epsilon() && std::abs(p21[2]) < std::numeric_limits<Real>::epsilon())
      return false;

   d1343 = p13[0] * p43[0] + p13[1] * p43[1] + p13[2] * p43[2];
   d4321 = p43[0] * p21[0] + p43[1] * p21[1] + p43[2] * p21[2];
   d1321 = p13[0] * p21[0] + p13[1] * p21[1] + p13[2] * p21[2];
   d4343 = p43[0] * p43[0] + p43[1] * p43[1] + p43[2] * p43[2];
   d2121 = p21[0] * p21[0] + p21[1] * p21[1] + p21[2] * p21[2];

   denom = d2121 * d4343 - d4321 * d4321;
   if (std::abs(denom) < std::numeric_limits<Real>::epsilon())
      return false;
   numer = d1343 * d4321 - d1321 * d4343;

   mua = numer / denom;
   mub = (d1343 + d4321 * (mua)) / d4343;

   pa[0] = p1[0] + mua * p21[0];
   pa[1] = p1[1] + mua * p21[1];
   pa[2] = p1[2] + mua * p21[2];
   pb[0] = p3[0] + mub * p43[0];
   pb[1] = p3[1] + mub * p43[1];
   pb[2] = p3[2] + mub * p43[2];

   return true;
}

bool LineIntersect(const Vec2r& p1, const Vec2r& p2, const Vec2r& p3, const Vec2r& p4, Vec2r& p)
{
   Real mua,mub;
   Real denom,numera,numerb;

   denom  = (p4[1]-p3[1]) * (p2[0]-p1[0]) - (p4[0]-p3[0]) * (p2[1]-p1[1]);
   numera = (p4[0]-p3[0]) * (p1[1]-p3[1]) - (p4[1]-p3[1]) * (p1[0]-p3[0]);
   numerb = (p2[0]-p1[0]) * (p1[1]-p3[1]) - (p2[1]-p1[1]) * (p1[0]-p3[0]);

   /* Are the line coincident? */
   if (std::abs(numera) < std::numeric_limits<Real>::epsilon() && std::abs(numerb) < std::numeric_limits<Real>::epsilon() && std::abs(denom) < std::numeric_limits<Real>::epsilon()) {
      p[0] = (p1[0] + p2[0]) / 2;
      p[1] = (p1[1] + p2[1]) / 2;
      return true;
   }

   /* Are the line parallel */
   if (std::abs(denom) < std::numeric_limits<Real>::epsilon()) {
      p[0] = 0;
      p[1] = 0;
      std::cerr << "Parallel Line" << std::endl;
      return false;
   }

   /* Is the intersection along the the segments */
   mua = numera / denom;
   mub = numerb / denom;
   /*if (mua < 0 || mua > 1 || mub < 0 || mub > 1) {
       std::cerr << "Intersection out of segment" << std::endl;
      p[0] = 0;
      p[1] = 0;
      return false;
   }*/
   p[0] = p1[0] + mua * (p2[0] - p1[0]);
   p[1] = p1[1] + mua * (p2[1] - p1[1]);
   return true;
}


bool AkinciEdgeSampling( const Vec3r& p1, const Vec3r& p2, const Real& particleDiameter, std::vector< Vec3r >& samples)
{
    samples.clear();

    Vec3r edge = p2-p1;
    Real edgesL = edge.length();
    int pNumber = std::floor(edgesL/particleDiameter);
    Vec3r pe = edge/(Real)pNumber, p;

    for(int j=1; j<pNumber; ++j)
    {
        p = p1 + (Real)j*pe;
        samples.push_back(p);
    }
}

bool AkinciFullTriangleSampling( const Vec3r& p1, const Vec3r& p2, const Vec3r& p3, const Real& particleDiameter, std::vector< Vec3r >& samples)
{
    std::vector< Vec3r > tmp;

    samples.push_back(p1);
    samples.push_back(p2);
    samples.push_back(p3);

    AkinciTriangleSampling(p1,p2,p3,particleDiameter, tmp);
    for(size_t i=0; i<tmp.size(); ++i)
        samples.push_back(tmp[i]);

    AkinciEdgeSampling(p1,p2,particleDiameter, tmp);
    for(size_t i=0; i<tmp.size(); ++i)
        samples.push_back(tmp[i]);

    AkinciEdgeSampling(p1,p3,particleDiameter, tmp);
    for(size_t i=0; i<tmp.size(); ++i)
        samples.push_back(tmp[i]);

    AkinciEdgeSampling(p2,p3,particleDiameter, tmp);
    for(size_t i=0; i<tmp.size(); ++i)
        samples.push_back(tmp[i]);

}

bool AkinciTriangleSampling( const Vec3r& p1, const Vec3r& p2, const Vec3r& p3, const Real& particleDiameter, std::vector< Vec3r >& samples)
{

    std::array< Vec3r, 3> v = {{p1, p2, p3}};
    std::array< Vec3r, 3 > edgesV = {{v[1]-v[0], v[2]-v[1], v[0]-v[2]}};
    std::array< Vec2<int>, 3 > edgesI = {{Vec2<int>(0,1), Vec2<int>(1,2), Vec2<int>(2,0)}};
    std::array< Real, 3> edgesL = {{ edgesV[0].length(), edgesV[1].length(), edgesV[2].length() }};
    samples.clear(); 

    //Edges
    int pNumber=0;
    Vec3r pe(0,0,0);
    for(int i=0; i<3; ++i)
    {
        pNumber = std::floor(edgesL[i]/particleDiameter);
        pe = edgesV[i]/(Real)pNumber;
        for(int j=0; j<pNumber; ++j)
        {
            Vec3r p = v[edgesI[i][0]] + (Real)j*pe;
            //samples.push_back(p);
        }
    }

    //Triangles
    int sEdge=-1,lEdge=-1;
    Real maxL = -std::numeric_limits<Real>::max();
    Real minL = std::numeric_limits<Real>::max();
    for(int i=0; i<3; ++i)
    {
        if(edgesL[i]>maxL)
        {
            maxL = edgesL[i];
            lEdge = i;
        }
        if(edgesL[i]<minL)
        {
            minL = edgesL[i];
            sEdge = i;
        }
    }
    Vec3r cross, normal;
    cross = Vec3r::crossProduct(edgesV[lEdge], edgesV[sEdge]);
    normal = Vec3r::crossProduct(edgesV[sEdge], cross);
    normal.normalize();


    std::array<bool, 3> findVertex = {{true, true, true}};
    findVertex[edgesI[sEdge][0]] = false;
    findVertex[edgesI[sEdge][1]] = false;
    int thirdVertex = -1;
    for(int i=0; i<findVertex.size(); ++i)
        if(findVertex[i]==true)
            thirdVertex = i;
    Vec3r tmpVec = v[thirdVertex] - v[edgesI[sEdge][0]];
    Real sign = Vec3r::dotProduct(normal, tmpVec);
    if(sign<0)
        normal = -normal;

    Real triangleHeight = std::abs(Vec3r::dotProduct(normal, edgesV[lEdge]));
    int sweepSteps = triangleHeight/particleDiameter;
    bool success = false;

    Vec3r sweepA, sweepB, i1, i2, o1, o2;
    Real m1, m2;
    int edge1,edge2;
    edge1 = (sEdge+1)%3;
    edge2 = (sEdge+2)%3;

    for(int i=1; i<sweepSteps; ++i)
    {
        sweepA = v[edgesI[sEdge][0]] + (Real)i*particleDiameter*normal;
        sweepB = v[edgesI[sEdge][1]] + (Real)i*particleDiameter*normal;
        success = LineLineIntersect(v[edgesI[edge1][0]], v[edgesI[edge1][1]], sweepA, sweepB, o1, o2, m1, m2);
        i1 = o1;
        if(success == false)
        {
            std::cout << "Intersection 1 failed" << std::endl;
        }
        success = LineLineIntersect(v[edgesI[edge2][0]], v[edgesI[edge2][1]], sweepA, sweepB, o1, o2, m1, m2);
        i2 = o1;
        if(success == false)
        {
            std::cout << "Intersection 1 failed" << std::endl;
        }
        Vec3r s = i1-i2;
        int step = std::floor(s.length()/particleDiameter);
        Vec3r ps = s/((Real)step);
        for(int j=1; j<step; ++j)
        {
            Vec3r p = i2 + (Real)j*ps;
            samples.push_back(p);
        }
    }
}

bool AkinciMeshSampling(const Mesh& mesh, const Real& particleDiameter, std::vector<Vec3r>& samples)
{
    bool success = true, tmpSuccess = false;

    //Sample vertices
    for(size_t i=0; i<mesh.vertices.size(); ++i)
        samples.push_back(mesh.vertices[i]);

    //Sample edges
    std::vector< Vec3r > tmpSample;
    std::vector< std::pair<int, int> > edges;
    mesh.getEdges(mesh.triangles, edges);
    for(size_t i=0; i<edges.size(); ++i)
    {
        tmpSuccess = AkinciEdgeSampling(mesh.vertices[edges[i].first], mesh.vertices[edges[i].second], particleDiameter, tmpSample);
        for(size_t j=0; j<tmpSample.size(); ++j)
            samples.push_back(tmpSample[j]);
        success = success && tmpSuccess;
    }

    //Sample triangles
    for(size_t i=0; i<mesh.triangles.size(); ++i)
    {
        tmpSuccess = AkinciTriangleSampling(mesh.vertices[mesh.triangles[i][0]], mesh.vertices[mesh.triangles[i][1]], mesh.vertices[mesh.triangles[i][2]], particleDiameter, tmpSample);
        for(size_t j=0; j<tmpSample.size(); ++j)
            samples.push_back(tmpSample[j]);
        success = success && tmpSuccess;
    }
}