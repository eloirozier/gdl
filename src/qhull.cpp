/***************************************************************************
                          qhull.cpp  -  GDL library function
                             -------------------
    begin                : Jun 09 2021
    copyright            : (C) 2021 by Eloi R.de Linage

***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "includefirst.hpp"
#include "datatypes.hpp"
#include "envt.hpp"
#include "dinterpreter.hpp"

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>

//Qhull libraries

#include <libqhullcpp/PointCoordinates.h>
#include <libqhullcpp/RboxPoints.h>
#include <libqhullcpp/QhullError.h>
#include <libqhullcpp/QhullQh.h>
#include <libqhullcpp/QhullFacet.h>
#include <libqhullcpp/QhullFacetList.h>
#include <libqhullcpp/QhullFacetSet.h>
#include <libqhullcpp/QhullLinkedList.h>
#include <libqhullcpp/QhullPoint.h>
#include <libqhullcpp/QhullRidge.h>
#include <libqhullcpp/QhullUser.h>
#include <libqhullcpp/QhullVertex.h>
#include <libqhullcpp/QhullVertexSet.h>
#include <libqhullcpp/Qhull.h>

using namespace std;

using orgQhull::Qhull;
using orgQhull::QhullError;
using orgQhull::QhullFacet;
using orgQhull::QhullFacetList;
using orgQhull::QhullFacetListIterator;
using orgQhull::QhullFacetSet;
using orgQhull::QhullFacetSetIterator;
using orgQhull::QhullPoint;
using orgQhull::QhullPoints;
using orgQhull::QhullPointsIterator;
using orgQhull::QhullQh;
using orgQhull::QhullRidge;
using orgQhull::QhullRidgeSet;
using orgQhull::QhullUser;
using orgQhull::QhullVertex;
using orgQhull::QhullVertexList;
using orgQhull::QhullVertexListIterator;
using orgQhull::QhullVertexSet;
using orgQhull::QhullVertexSetIterator;
using orgQhull::RboxPoints;
using orgQhull::PointCoordinates;

using std::cerr;
using std::cin;
using std::cout;
using std::endl;
using std::string;

namespace lib {
  void qhull ( EnvT* e)
  {
    // There might be a simpler/cleaner way to do this...
    static int connIx=e->KeywordIx("CONNECTIVITY");
    bool isConn=e->KeywordPresent(connIx);

    static int boundsIx=e->KeywordIx("BOUNDS");
    bool isBounds=e->KeywordPresent(boundsIx);

    static int vnormIx=e->KeywordIx("VNORMALS");
    bool isVnorm=e->KeywordPresent(vnormIx);

    static int vdiagIx=e->KeywordIx("VDIAGRAM");
    bool isVdiag=e->KeywordPresent(vdiagIx);

    static int vvertIx=e->KeywordIx("VVERTICES");
    bool isVvert=e->KeywordPresent(vvertIx);

    bool isVoronoi= isVnorm || isVdiag || isVvert;

    static int sphereIx=e->KeywordIx("SPHERE");
    bool isSphere=e->KeywordPresent(sphereIx);

    static int delaunayIx=e->KeywordIx("DELAUNAY");
    bool isDelaunay= ( e->KeywordSet(delaunayIx) || isVoronoi); //switch to delaunay if a voronoi keyword is set

    if(isConn & !isDelaunay) e->Throw("Keyword CONNECTIVITY requires the presence of keyword DELAUNAY/SPHERE.\nSPHERE is not implemented yet.");        
    
    if(isSphere) e->Throw("SPHERE is not implemented yet.");

    int nParam = e->NParam(2);
    int outIx = nParam-1;
  
    DDoubleGDL* p0;

    if(nParam == 2){
        p0 = e->GetParAs<DDoubleGDL>(0);
    } else {
        int inDim = e->GetParAs<DDoubleGDL>(0)->Dim(0);
        p0 = new DDoubleGDL( *(new dimension(nParam-1, inDim)), BaseGDL::ZERO );

        for(int i=0; i<nParam-1; i++)
        {
          DDoubleGDL* par=e->GetParAs<DDoubleGDL>(i);
          if(par->Dim(0) != inDim || par->Dim(1) != 0 )
          {
            e->Throw("qhull input error: separated input arrays must have same length and be 1 dimensional");
          }
          for(int j=0; j<inDim; j++) (*p0)[i+j*(nParam-1)] = (*par)[j];
        }
    }

    Qhull qhull;

    int nd=p0->Dim(0);
    int np=p0->Dim(1);

    if(nd == 0 || np == 0) e->Throw("qhull input error: array must have 2 dimensions");

    //if not enough input points for qhull, throw error
    if( np <= nd){
      stringstream ssp, ss_minp;
      ssp << np;
      ss_minp << nd+1;
      string err_string="qhull input error: not enough points ("+ ssp.str() +") to construct initial simplex (need "+ ss_minp.str() +")";
      e->Throw(err_string);
    } else if ((np <= nd+1) && isDelaunay) {
      e->Throw("qhull input error: not enough points available");
    }

    PointCoordinates *mPoints = new PointCoordinates();
    mPoints->setDimension(nd);
    
    vector <double> allPoints;
    for (int i =0; i < p0->N_Elements(); i++){
      allPoints.push_back((*p0)[i]);
      if(!isfinite((*p0)[i])) e->Throw("Infinite or invalid (NaN) operands not allowed.");
    }
          

    mPoints->append(allPoints);
    int ndRes;

    if(isVoronoi)
    {
        qhull.runQhull( mPoints->comment().c_str(), nd, np, mPoints->coordinates(), "QJ v Qbb Pp");
        ndRes=nd+1;
    } else if(isDelaunay) {
        // QJ option prevents non-simplicial facets, it seems to be the option IDL uses
        // While Qt option triangulates facets afterwards, which gives somewhat different results in some cases

        // Delaunay triangulation has issues with integers (ex:
        //                                              a=[[0,0,0],[1,0,0],[0,1,0],[0,0,1],[1,1,1]] & qhull, a, b, /DELAUNAY
        //                                                    crashes...)
        qhull.runQhull( mPoints->comment().c_str(), nd, np, mPoints->coordinates(), "QJ d Pp");
        ndRes=nd+1;
    } else {
        qhull.runQhull( mPoints->comment().c_str(), nd, np, mPoints->coordinates(), "QJ Pp");
        ndRes=nd;
    }

    QhullFacetList facets = qhull.facetList();
    DLongGDL* res = new DLongGDL( *(new dimension(ndRes, qhull.facetCount())), BaseGDL::ZERO);
    
    int ix=0;
    int bad_facets=0;

    for (QhullFacetList::iterator it = facets.begin(); it != facets.end(); ++it)
    {
        if (!(*it).isGood())
        {
          bad_facets++;
          continue;
        }
        QhullFacet f = *it;
        QhullVertexSet vSet = f.vertices();
        for (QhullVertexSet::iterator vIt = vSet.begin(); vIt != vSet.end(); ++vIt)
        {
          (*res)[ix] = (*vIt).point().id();
          ix++;
        }
    }

    if(bad_facets > 0){
        if(isDelaunay){
          res->SetDim(*(new dimension(nd+1, qhull.facetCount()-bad_facets)));
        } else {
          res->SetDim(*(new dimension(nd, qhull.facetCount()-bad_facets)));
        }
    }

    if(isBounds)
    {
      vector<long> boundsV;
      for(int i=0; i<res->Dim(0)*res->Dim(1); ++i)
          if( (find(boundsV.begin(), boundsV.end(), (*res)[i]) == boundsV.end()))
            boundsV.push_back( (*res)[i] );
      DLongGDL* bounds=new DLongGDL( *(new dimension(boundsV.size())), BaseGDL::ZERO );
      for(int i=0; i <boundsV.size(); ++i){
          (*bounds)[i] = boundsV.at(i);
      }
      e->SetKW(boundsIx, bounds);
    }

    if(isConn)
    {
      vector<vector<long>>connV(qhull.vertexCount());
      qhull.defineVertexNeighborFacets();
      QhullVertexList vertices = qhull.vertexList();

      long connCount=0;

      for(QhullVertexList::iterator vIt = vertices.begin(); vIt != vertices.end(); vIt++)
      {
        QhullVertex vertex = *vIt;
        long vertexId = vertex.point().id();
        QhullFacetSet neighbors = vertex.neighborFacets();
        vector<long> neighborsV;

        for(QhullFacetSet::iterator nIt = neighbors.begin(); nIt != neighbors.end(); ++nIt)
        {
          if(!(*nIt).isGood()) continue;
          QhullFacet neighbor = *nIt;
          QhullVertexSet neighborVSet = neighbor.vertices();
          for (QhullVertexSet::iterator nVIt = neighborVSet.begin(); nVIt != neighborVSet.end(); ++nVIt)
          {
              QhullVertex v = *nVIt;
              QhullPoint p = v.point();
              if( (find(neighborsV.begin(), neighborsV.end(), (long) p.id()) == neighborsV.end()) && p.id() != vertexId)
              {
                neighborsV.push_back(p.id());
                connCount++;
              }
          }
        }
        connV.at(vertexId) = neighborsV;
      }

      DLongGDL* conn = new DLongGDL(*(new dimension( np + 1 + connCount )), BaseGDL::ZERO);
      
      int writeIx = np+1;
      (*conn)[np]=np+1+connCount;
      for(int i=0; i < connV.size(); i++)
      {
          vector<long> neighborsV = connV.at(i);
          for(int j=0; j<neighborsV.size(); j++) (*conn)[ writeIx+j ] = neighborsV.at(j);
          (*conn)[i] = writeIx;
          writeIx += neighborsV.size();
      }
      e->SetKW(connIx, conn);
    }

    if(isVoronoi)
    {
      // prepare voronoi

      bool isLower;            //not used
      int voronoiVertexCount;  //not used
      qhull.prepareVoronoi(&isLower, &voronoiVertexCount);

      // retrieve voronoi vertices

      vector<vector<double> > vVertices;
      for(QhullFacetList::iterator it = facets.begin(); it != facets.end(); ++it)
      {
        if (!(*it).isGood()) continue;
        QhullFacet f = *it;
        vVertices.push_back(f.getCenter().toStdVector());
      }

      if(isVvert)
      {
        DDoubleGDL* vvert=new DDoubleGDL( *(new dimension(nd, vVertices.size())), BaseGDL::ZERO);
        int nvVert=vVertices.size();
        for(int i=0; i < nvVert; ++i)
          for(int j=0; j<nd; ++j)
            (*vvert)[nd*i+j]=vVertices.at(i).at(j);
        e->SetKW(vvertIx, vvert);
      }

      // retrieve voronoi normals

      QhullUser results((&qhull)->qh());
      (&qhull)->outputQhull("Fo");
      vector<vector<double>> normsPars= results.doublesVector();
      vector<vector<int>> normsVertId = results.intsVector();

      if(isVnorm)
      {
        int nVnorm = normsPars.size();
        DDoubleGDL* vnorm=new DDoubleGDL(*(new dimension(nd+1, nVnorm)), BaseGDL::ZERO);
        for(int i=0; i < nVnorm; ++i)
          for(int j=0; j<nd+1; ++j)
            (*vnorm)[i*(nd+1) + j] = normsPars[i][j];

        e->SetKW(vnormIx, vnorm);
      }

      // make voronoi diagrams array (see IDL QHULL doc)

      if(isVdiag){

        // Weirdly enough, QhullUser class 'hack' (ex. in qhull user_eg3.cpp) cant be used with the option Fv (works for Fo, see above)
        // to get the voronoi diagram, so i give an ostream to qhull and reformat the data from there

        stringbuf strbuf;
        ostream os(&strbuf);
        qhull.setOutputStream( &os );
        qhull.outputQhull("Fv");
        qhull.clearQhullMessage(); // to prevent qhull printing in console
        stringstream ss;
        ss << os.rdbuf();
        
        if(nd==2) // 2D case
        {
          // Format the data
          int current_int, nVdiag, n_indices;
          int line_ix=0;

          vector<vector<int>>q_vdiag;
          vector<int> line;
          while ( ss >> current_int )
          {
              if(line_ix == 0){
                n_indices = current_int;
              } else {
                line.push_back(current_int);
              }
              line_ix++;
              if(line_ix == n_indices+1){
                q_vdiag.push_back(line);
                line.clear();
                line_ix=0;
              }
          }
          nVdiag = q_vdiag.size();

          // Handle unbounded half-spaces
          for(int i=0; i<nVdiag; i++){
            for(int j=2; j<4; ++j){
              if(q_vdiag[i][j] == 0){
                for(int k=0; k<normsVertId.size(); k++){
                  if(q_vdiag[i][0] == normsVertId[k][1] && q_vdiag[i][1] == normsVertId[k][2]){
                    q_vdiag[i][j] = -k;
                    break;
                  }
                }
                break;
              }
            }
          }
          
          DLongGDL* vdiag=new DLongGDL(*(new dimension(nd*2, nVdiag)), BaseGDL::ZERO);
          for(int i=0; i < nVdiag; ++i)
          {
              for(int j=0; j<nd; ++j)
              {
                (*vdiag)[i*2*nd]=q_vdiag[i][0];
                (*vdiag)[i*2*nd+1]=q_vdiag[i][1];
              }
              for(int k=0; k< nd; k++)
              {
                (*vdiag)[i*2*nd+2]=q_vdiag[i][2]-1;
                (*vdiag)[i*2*nd+3]=q_vdiag[i][3]-1;
              }
          }
          e->SetKW(vdiagIx, vdiag);
        }
        else // For NDimension > 2
        {
          // Format the data, a bit different from the 2D case since we keep, for each line, the number of indices
          int current_int, nVdiag, n_indices;
          int line_ix=0;
          int vdiag_length=0;

          vector<vector<int>>q_vdiag;
          vector<int>line;
          while ( ss >> current_int )
          {
              if(line_ix == 0) n_indices = current_int;
              line.push_back(current_int);
              line_ix++;
              if(line_ix == n_indices+1){
                q_vdiag.push_back(line);
                vdiag_length += n_indices+1;
                line.clear();
                line_ix=0;
              }
          }
          nVdiag = q_vdiag.size();

          // Handle unbounded half-spaces
          for(int i=0; i<nVdiag; i++){
            for(int j=2; j<q_vdiag.size(); ++j){
              if(q_vdiag[i][j] == 0){
                for(int k=0; k<normsVertId.size(); k++){
                  if(q_vdiag[i][1] == normsVertId[k][1] && q_vdiag[i][2] == normsVertId[k][2]){
                    q_vdiag[i][j] = -k;
                    break;
                  }
                }
                break;
              }
            }
          }
          
          DLongGDL* vdiag=new DLongGDL(*(new dimension(vdiag_length)), BaseGDL::ZERO);
          int writeIx=0;
          for(int i=0; i < nVdiag; ++i)
          {
              for(int j=0; j<3; ++j)
              {
                (*vdiag)[writeIx]=q_vdiag[i][j];
                writeIx++;
              }
              for(int j=3; j< q_vdiag[i].size(); j++)
              {
                (*vdiag)[writeIx]=q_vdiag[i][j]-1;
                writeIx++;
              }
          }
          e->SetKW(vdiagIx, vdiag);
        }
      }
    }

    e->SetPar(outIx, res);
  }

  //sub_functions for qgrid3

  template<typename T>
  vector<T> cross_prod(vector<T> a, vector<T> b)
  {
    return vector<T> {a[1]*b[2] - a[2]*b[1], b[0]*a[2] - a[0]*b[2] ,  a[0]*b[1] - a[1]*b[0] };
  }

  template<typename T>
  T dot_prod3d(vector<T> a, vector<T> b)
  {
    return (a[0]*b[0] + a[1]*b[1] + a[2]*b[2]);
  }

  template<typename T>
  T scalar_triple_prod(vector<T> a, vector<T> b, vector<T> c)
  {
    return dot_prod3d(a, cross_prod(b,c));
  }

  template<typename T>
  vector<T> vector_substract(vector<T> a, vector<T> b)
  {
    return vector<T> {a[0]-b[0], a[1]-b[1], a[2]-b[2]};
  }

  template<typename T>
  vector<vector<T>> box_limits(vector<vector<T>> points)
  { 
    vector<T> min_coord = points[0];
    vector<T> max_coord = points[0];
    for(int i=0; i<points.size(); i++){
        for(int j=0; j< 3; j++){
          if(points[i][j] < min_coord[j])  min_coord[j] = points[i][j];
          if(points[i][j] > max_coord[j])  max_coord[j] = points[i][j];
        }
    }
    return vector<vector<T>> {min_coord, max_coord};
  }

  template<typename T>
  vector<T> bary_tet(vector<T> a, vector<T> b, vector<T> c, vector<T> d, vector<T> p)
  {
    vector<T> vap = vector_substract(p, a);
    vector<T> vbp = vector_substract(p, b);

    vector<T> vab = vector_substract(b, a);
    vector<T> vac = vector_substract(c, a);
    vector<T> vad = vector_substract(d, a);

    vector<T> vbc = vector_substract(c, b);
    vector<T> vbd = vector_substract(d, b);

    T va6 = scalar_triple_prod(vbp, vbd, vbc);
    T vb6 = scalar_triple_prod(vap, vac, vad);
    T vc6 = scalar_triple_prod(vap, vad, vab);
    T vd6 = scalar_triple_prod(vap, vab, vac);
    T v6 = 1 / scalar_triple_prod(vab, vac, vad);

    return vector<T> {va6*v6, vb6*v6, vc6*v6, vd6*v6};
  }

  template<typename T>
  bool is_outside_of_box(vector<T> min_coord, vector<T> max_coord, vector<T> point)
  {
    return (point[0] < min_coord[0] || point[0] > max_coord[0]) || (point[1] < min_coord[1] || point[1] > max_coord[1])
    || (point[2] < min_coord[2] || point[2] > max_coord[2]);
  }

  bool is_neg(double i){ return (i < 0);};

  bool is_inside_tetra()
  {
    return false;
  }

  //IDEES : regarder si le tetrahedre du point d'avant contient le point actuel
  //PUIS chercher d'abord les voisins du point
  //fonction trouver tetrahedre qui prend: un point, la triangulation delaunay, les coordonnees de l'ancien comme départ (avant de checker les voisins,
  //puis les voisins des voisins ??)

  //optimiser les accès à la mémoire le plus possible...

  BaseGDL* qgrid3_fun ( EnvT* e)
  {
    //check and get parameters...
    int nParam = e->NParam(3);
  
    DDoubleGDL* p0;
    DDoubleGDL * func;
    DLongGDL * tetra_list;

    if(nParam == 3)
    {
        p0 = e->GetParAs<DDoubleGDL>(0);         //input points
        func = e->GetParAs<DDoubleGDL>(1);       //input function
        tetra_list = e->GetParAs<DLongGDL>(2);   //indices of tetrahedra vertices from qhull
    } else { // if input coordinates are in separate arrays
        e->NParam(5);
        if (nParam>5) e->Throw("Incorrect number of arguments.");

        func = e->GetParAs<DDoubleGDL>(3);     //input function
        tetra_list = e->GetParAs<DLongGDL>(4); //indices of tetrahedra vertices from qhull

        int inDim = e->GetParAs<DDoubleGDL>(0)->Dim(0);
        p0 = new DDoubleGDL( *(new dimension(3, inDim)), BaseGDL::ZERO ); //concatenation of the 3 separate inputs arrays

        for(int i=0; i<3; i++)
        {
          DDoubleGDL* par=e->GetParAs<DDoubleGDL>(i);
          if(par->Dim(0) != inDim || par->Dim(1) != 0 )
          {
            e->Throw("separated input arrays must have same length and be 1 dimensional");
          }
          for(int j=0; j<inDim; j++) (*p0)[i+j*3] = (*par)[j];
        }
    }

    int n_tetra = tetra_list->Dim(1);
    int nd=p0->Dim(0);
    int np=p0->Dim(1);

    // x,y,z dimensions of grid
    dimension* res_dim;
    vector<int>res_dim_vec = {25,25,25}; // array caring dims
    static int dimensionIx=e->KeywordIx("DIMENSION");
    if(e->KeywordPresent(dimensionIx)){
      DLongGDL * dimPar = e->GetKWAs<DLongGDL>(dimensionIx);
      int dimNelem = dimPar->N_Elements();
      if(dimNelem <= 3 && dimNelem >0){
        res_dim_vec={(*dimPar)[0], (*dimPar)[(dimNelem>1)], (*dimPar)[2*(dimNelem>1)]};
      } else {
        e->Throw("Keyword array parameter DIMENSION must have from 1 to 3 elements.");
      }
    }
    res_dim = new dimension(res_dim_vec[0], res_dim_vec[1], res_dim_vec[2]);

    // putting input points in a vector...
    vector <vector<double>> points(np);
    for (int i =0; i < np; i++){
          for(int j = 0; j<nd; j++){
            points.at(i).push_back(  (*p0)[3*i+j]  );
          }
    }

    // we need to get max and min x, y and z...
    vector<vector<double>> points_box_limits = box_limits(points);
    vector<double> min_coord = points_box_limits[0];
    vector<double> max_coord = points_box_limits[1];

    // start of grid
    vector<double> start_coord = min_coord;
    static int startIx=e->KeywordIx("START");
    if(e->KeywordPresent(startIx)){
      DDoubleGDL * startPar = e->GetKWAs<DDoubleGDL>(startIx);
      int startNelem = startPar->N_Elements();
      if(startNelem <= 3 && startNelem >0){
        start_coord = {(*startPar)[0], (*startPar)[ (startNelem>1) ], (*startPar)[ 2*(startNelem>2) ]};
      } else {
        e->Throw("Keyword array parameter START must have from 1 to 3 elements.");
      }
    }

    // grid spacing
    vector<double> delta;
    // default grid spacing is determined using max values of point set
    for(int i=0; i<3; i++) delta.push_back( (max_coord[i] - start_coord[i] )/ double(res_dim_vec[i]));
    static int deltaIx=e->KeywordIx("DELTA");
    if(e->KeywordPresent(deltaIx)){
      DDoubleGDL * deltaPar = e->GetKWAs<DDoubleGDL>(deltaIx);
      int deltaNelem = deltaPar->N_Elements();
      if(deltaNelem <= 3 && deltaNelem >0){
        delta = {(*deltaPar)[0], (*deltaPar)[(deltaNelem>1)], (*deltaPar)[2*(deltaNelem>2)]};
      } else {
        e->Throw("Keyword array parameter DELTA must have from 1 to 3 elements.");
      }
    }

    double missing = 0;
    static int missingIx=e->KeywordIx("MISSING");
    if(e->KeywordPresent(missingIx)) missing = (*e->GetKWAs<DDoubleGDL>(missingIx))[0];

    // delaunay data
    vector<vector<vector<double>>> tetra_data;
    for(int tIx =0; tIx<n_tetra; tIx++){
      int vertices[4] = {(*tetra_list)[4*tIx],(*tetra_list)[4*tIx+1], (*tetra_list)[4*tIx+2], (*tetra_list)[4*tIx+3]};
      vector<vector<double>> tetrahedron = { points[vertices[0]], points[vertices[1]],
                                    points[vertices[2]], points[vertices[3]]};
      tetra_data.push_back(tetrahedron);
    }

    DDoubleGDL * res = new DDoubleGDL(*res_dim, BaseGDL::ZERO);
    vector<double> coord = start_coord;

    int last_tetra=0;
    
    for(int i=0; i < res_dim_vec[0]; i++ ){
      coord[1] = start_coord[1];
      for(int j=0; j<res_dim_vec[1]; j++){
        coord[2]=start_coord[2];
        for(int k=0; k<res_dim_vec[2]; k++){
          
          bool isInHull = false;
          
          int res_index = i + j*res_dim_vec[0] + k*res_dim_vec[0]*res_dim_vec[1];

          if( is_outside_of_box(min_coord, max_coord, coord) ){
            (*res)[res_index] = missing;
            continue;
          }

          // check last tetrahedron
          int l_vertices[4] = {(*tetra_list)[4*last_tetra],(*tetra_list)[4*last_tetra+1], (*tetra_list)[4*last_tetra+2], (*tetra_list)[4*last_tetra+3]};
          vector<vector<double>> l_tetrahedron = tetra_data[last_tetra];

          vector<double> l_bary_coord = bary_tet(l_tetrahedron[0],l_tetrahedron[1],l_tetrahedron[2],l_tetrahedron[3],coord);
          if( find_if(l_bary_coord.begin(), l_bary_coord.end(), is_neg) == l_bary_coord.end() )
          {
            isInHull = true;
            double interp_value=0;
            for(int vIx=0; vIx<4; vIx++) interp_value += l_bary_coord[vIx]*(*func)[ l_vertices[vIx] ];
            (*res)[res_index] = interp_value;
            
            coord[2] += delta[2];
            continue;
          }
          
          // bruteforce everything (stupid idea really)
          for(int tIx=0; tIx<n_tetra; tIx++)
          {
            int vertices[4] = {(*tetra_list)[4*tIx],(*tetra_list)[4*tIx+1], (*tetra_list)[4*tIx+2], (*tetra_list)[4*tIx+3]};
            vector<vector<double>> tetrahedron = tetra_data[tIx];
            vector<vector<double>> tetra_box = box_limits(tetrahedron);

            if(is_outside_of_box(tetra_box[0], tetra_box[1], coord)) continue;

            vector<double> bary_coord = bary_tet(tetrahedron[0],tetrahedron[1],tetrahedron[2],tetrahedron[3],coord);

            // check if point is inside
            if( find_if(bary_coord.begin(), bary_coord.end(), is_neg) == bary_coord.end() )
            {
              isInHull = true;
              double interp_value=0;
              for(int vIx=0; vIx<4; vIx++) interp_value += bary_coord[vIx]*(*func)[ vertices[vIx] ];
              (*res)[res_index] = interp_value;
              last_tetra=tIx;
              break;
            }
          }

          if(!isInHull) (*res)[res_index] = missing;
          coord[2] += delta[2];
        }
        coord[1] += delta[1];
      }
      coord[0] += delta[0];
    }
    
    return res;
  }
}

