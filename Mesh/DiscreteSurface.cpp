// $Id: DiscreteSurface.cpp,v 1.43 2006-08-10 15:29:26 geuzaine Exp $
//
// Copyright (C) 1997-2006 C. Geuzaine, J.-F. Remacle
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA.
//
// Please report all bugs and problems to "gmsh@geuz.org".

#include "Gmsh.h"
#include "Numeric.h"
#include "Mesh.h"
#include "CAD.h"
#include "Geo.h"
#include "Create.h"
#include "Interpolation.h"
#include "Context.h"
#include "BDS.h"
#include "PartitionMesh.h"
#include "OS.h"

extern Mesh *THEM;
extern Context_T CTX;

void Mesh_To_BDS(Mesh *M)
{
  Msg(STATUS2, "Moving mesh into new structure");
  Move_SimplexBaseToSimplex(3);
  // create the structure
  if(M->bds)
    delete M->bds;
  M->bds = new BDS_Mesh;
  PhysicalGroup *p;

  M->bds->Min[0] = M->bds->Min[1] = M->bds->Min[2] = 1.e12;
  M->bds->Max[0] = M->bds->Max[1] = M->bds->Max[2] = -1.e12;

  Msg(STATUS2, "Moving nodes");
  // copy the nodes
  List_T *vertices = Tree2List(M->Vertices);
  for(int i = 0; i < List_Nbr(vertices); ++i) {
    Vertex *v;
    List_Read(vertices, i, &v);
    if(v->Pos.X < M->bds->Min[0])
      M->bds->Min[0] = v->Pos.X;
    if(v->Pos.Y < M->bds->Min[1])
      M->bds->Min[1] = v->Pos.Y;
    if(v->Pos.Z < M->bds->Min[2])
      M->bds->Min[2] = v->Pos.Z;
    if(v->Pos.X > M->bds->Max[0])
      M->bds->Max[0] = v->Pos.X;
    if(v->Pos.Y > M->bds->Max[1])
      M->bds->Max[1] = v->Pos.Y;
    if(v->Pos.Z > M->bds->Max[2])
      M->bds->Max[2] = v->Pos.Z;
    M->bds->add_point(v->Num, v->Pos.X, v->Pos.Y, v->Pos.Z);
  }
  M->bds->LC =
    sqrt((M->bds->Min[0] - M->bds->Max[0]) * (M->bds->Min[0] -
                                              M->bds->Max[0]) +
         (M->bds->Min[1] - M->bds->Max[1]) * (M->bds->Min[1] -
                                              M->bds->Max[1]) +
         (M->bds->Min[2] - M->bds->Max[2]) * (M->bds->Min[2] -
                                              M->bds->Max[2]));
  List_Delete(vertices);

  for(int i = 0; i < List_Nbr(M->PhysicalGroups); i++) {
    List_Read(M->PhysicalGroups, i, &p);
    if(p->Typ == MSH_PHYSICAL_POINT) {
      M->bds->add_geom(p->Num, 0);
      BDS_GeomEntity *g = M->bds->get_geom(p->Num, 0);
      for(int j = 0; j < List_Nbr(p->Entities); j++) {
        int Num;
        List_Read(p->Entities, j, &Num);
        BDS_Point *ppp = M->bds->find_point(Num);
        ppp->g = g;
        g->p = ppp;
      }
    }
  }

  Msg(STATUS2, "Moving curves");
  List_T *curves = Tree2List(M->Curves);
  for(int i = 0; i < List_Nbr(curves); ++i) {
    Curve *c;
    List_Read(curves, i, &c);
    M->bds->add_geom(c->Num, 1);
    BDS_GeomEntity *g = M->bds->get_geom(c->Num, 1);
    List_T *simplices = Tree2List(c->Simplexes);
    Simplex *simp;
    for(int j = 0; j < List_Nbr(simplices); ++j) {
      List_Read(simplices, j, &simp);
      BDS_Edge *edge = M->bds->add_edge(simp->V[0]->Num, simp->V[1]->Num);
      edge->g = g;
      if(!edge->p1->g)
        edge->p1->g = g;
      if(!edge->p2->g)
        edge->p2->g = g;
    }
    List_Delete(simplices);
  }
  List_Delete(curves);

  Msg(STATUS2, "Moving surfaces");
  List_T *surfaces = Tree2List(M->Surfaces);
  for(int i = 0; i < List_Nbr(surfaces); ++i) {
    Surface *s;
    List_Read(surfaces, i, &s);
    M->bds->add_geom(s->Num, 2);
    BDS_GeomEntity *g = M->bds->get_geom(s->Num, 2);

    Msg(INFO, "Created new surface %d %d", g->classif_tag, g->classif_degree);

    List_T *simplices = Tree2List(s->Simplexes);
    Simplex *simp;
    for(int j = 0; j < List_Nbr(simplices); ++j) {
      List_Read(simplices, j, &simp);
      BDS_Triangle *t =
        M->bds->add_triangle(simp->V[0]->Num, simp->V[1]->Num,
				simp->V[2]->Num);
      t->g = g;
      BDS_Point *n[3];
      t->getNodes(n);
      for(int K = 0; K < 3; K++)
        if(!n[K]->g)
          n[K]->g = g;
      if(!t->e1->g)
        t->e1->g = g;
      if(!t->e2->g)
        t->e2->g = g;
      if(!t->e3->g)
        t->e3->g = g;
    }
    List_Delete(simplices);
  }
  List_Delete(surfaces);

  Msg(STATUS2, "Moving %d volumes", Tree_Nbr(M->Volumes));
  List_T *volumes = Tree2List(M->Volumes);
  for(int i = 0; i < List_Nbr(volumes); ++i) {
    Volume *v;
    List_Read(volumes, i, &v);
    M->bds->add_geom(v->Num, 3);
    BDS_GeomEntity *g = M->bds->get_geom(v->Num, 3);
    List_T *simplices = Tree2List(v->Simplexes);
    Simplex *simp;

    for(int j = 0; j < List_Nbr(simplices); ++j) {
      List_Read(simplices, j, &simp);
      BDS_Tet *t =
        M->bds->add_tet(simp->V[0]->Num, simp->V[1]->Num, simp->V[2]->Num,
			   simp->V[3]->Num);
      t->g = g;
      BDS_Point *n[4];
      t->getNodes(n);
      for(int K = 0; K < 4; K++)
        if(!n[K]->g)
          n[K]->g = g;
      if(!t->f1->g)
        t->f1->g = g;
      if(!t->f2->g)
        t->f2->g = g;
      if(!t->f3->g)
        t->f3->g = g;
      if(!t->f4->g)
        t->f4->g = g;
      if(!t->f1->e1->g)
        t->f1->e1->g = g;
      if(!t->f2->e1->g)
        t->f2->e1->g = g;
      if(!t->f3->e1->g)
        t->f3->e1->g = g;
      if(!t->f4->e1->g)
        t->f4->e1->g = g;
      if(!t->f1->e2->g)
        t->f1->e2->g = g;
      if(!t->f2->e2->g)
        t->f2->e2->g = g;
      if(!t->f3->e2->g)
        t->f3->e2->g = g;
      if(!t->f4->e2->g)
        t->f4->e2->g = g;
      if(!t->f1->e3->g)
        t->f1->e3->g = g;
      if(!t->f2->e3->g)
        t->f2->e3->g = g;
      if(!t->f3->e3->g)
        t->f3->e3->g = g;
      if(!t->f4->e3->g)
        t->f4->e3->g = g;

    }
    List_Delete(simplices);
  }
  List_Delete(volumes);

}

void BDS_To_Mesh_2(Mesh *M)
{
  Msg(STATUS2, "Moving surface mesh into old structure");

  Tree_Action(M->Vertices, Free_Vertex);

  Tree_Delete(M->Vertices);
  M->Vertices = Tree_Create(sizeof(Vertex *), compareVertex);

  {
    std::set < BDS_Point *, PointLessThan >::iterator it =
      M->bds_mesh->points.begin();
    std::set < BDS_Point *, PointLessThan >::iterator ite =
      M->bds_mesh->points.end();


    while(it != ite) {
      //      double dx = 1.e-3 * M->bds_mesh->LC * (double)rand() / (double)RAND_MAX;
      //      double dy = 1.e-3 * M->bds_mesh->LC * (double)rand() / (double)RAND_MAX;
      //      double dz = 1.e-3 * M->bds_mesh->LC * (double)rand() / (double)RAND_MAX;
      double dx = 0;
      double dy = 0;
      double dz = 0;
      Vertex *vert =
        Create_Vertex((*it)->iD, (*it)->X+dx, (*it)->Y+dy, (*it)->Z+dz, (*it)->min_edge_length(), 0.0);
      Tree_Add(M->Vertices, &vert);
      ++it;
    }
  }
  {
    std::list < BDS_Edge * >::iterator it = M->bds_mesh->edges.begin();
    std::list < BDS_Edge * >::iterator ite = M->bds_mesh->edges.end();
    while(it != ite) {
      BDS_GeomEntity *g = (*it)->g;
      if(g && g->classif_degree == 1) {
        Vertex *v1 = FindVertex((*it)->p1->iD, M);
        Vertex *v2 = FindVertex((*it)->p2->iD, M);
        Simplex *simp = Create_Simplex(v1, v2, NULL, NULL);
        Curve *c = FindCurve(g->classif_tag, M);
        if(c)
          simp->iEnt = g->classif_tag;
        Tree_Insert(c->Simplexes, &simp);
      }
      ++it;
    }
  }
  {
    std::list < BDS_Triangle * >::iterator it =
      M->bds_mesh->triangles.begin();
    std::list < BDS_Triangle * >::iterator ite = M->bds_mesh->triangles.end();
    while(it != ite) {
      BDS_GeomEntity *g = (*it)->g;
      if(g && g->classif_degree == 2) {
        BDS_Point *nod[3];
        (*it)->getNodes(nod);
        Vertex *v1 = FindVertex(nod[0]->iD, M);
        Vertex *v2 = FindVertex(nod[1]->iD, M);
        Vertex *v3 = FindVertex(nod[2]->iD, M);
        Simplex *simp = Create_Simplex(v1, v2, v3, NULL);
        BDS_GeomEntity *g = (*it)->g;
        Surface *s = FindSurface(g->classif_tag, M);
        if(s) {
          simp->iEnt = g->classif_tag;
        }
        else
          Msg(GERROR, "Impossible to find surface %d", g->classif_tag);
        Tree_Add(s->Simplexes, &simp);
        Tree_Add(s->Vertices, &v1);
        Tree_Add(s->Vertices, &v2);
        Tree_Add(s->Vertices, &v3);
      }
      ++it;
    }
  }
  {
    std::list < BDS_Tet * >::iterator it = M->bds_mesh->tets.begin();
    std::list < BDS_Tet * >::iterator ite = M->bds_mesh->tets.end();
    while(it != ite) {
      BDS_Point *nod[4];
      (*it)->getNodes(nod);
      Vertex *v1 = FindVertex(nod[0]->iD, M);
      Vertex *v2 = FindVertex(nod[1]->iD, M);
      Vertex *v3 = FindVertex(nod[2]->iD, M);
      Vertex *v4 = FindVertex(nod[3]->iD, M);
      Simplex *simp = Create_Simplex(v1, v2, v3, v4);
      BDS_GeomEntity *g = (*it)->g;
      Volume *v = FindVolume(g->classif_tag, M);
      if(v) {
        simp->iEnt = g->classif_tag;
      }
      else
        Msg(GERROR, "Error in BDS");
      Tree_Add(v->Simplexes, &simp);
      ++it;
    }
  }

  Msg(STATUS2N, " ");
}

void BDS_To_Mesh(Mesh *M)
{
  Tree_Action(M->Points, Free_Vertex);
  Tree_Delete(M->Points);
  Tree_Action(M->Volumes, Free_Volume);
  Tree_Action(M->Surfaces, Free_Surface);
  Tree_Action(M->Curves, Free_Curve);
  Tree_Delete(M->Surfaces);
  Tree_Delete(M->Curves);
  Tree_Delete(M->Volumes);
  M->Points = Tree_Create(sizeof(Vertex *), compareVertex);
  M->Curves = Tree_Create(sizeof(Curve *), compareCurve);
  M->Surfaces = Tree_Create(sizeof(Surface *), compareSurface);
  M->Volumes = Tree_Create(sizeof(Volume *), compareVolume);

  std::set < BDS_GeomEntity *, GeomLessThan >::iterator it =
    M->bds->geom.begin();
  std::set < BDS_GeomEntity *, GeomLessThan >::iterator ite =
    M->bds->geom.end();

  while(it != ite) {
    if((*it)->classif_degree == 3) {
      Volume *_Vol = 0;
      _Vol = FindVolume((*it)->classif_tag, M);
      if(!_Vol)
        _Vol = Create_Volume((*it)->classif_tag, MSH_VOLUME_DISCRETE);
      Tree_Add(M->Volumes, &_Vol);
    }
    else if((*it)->classif_degree == 2) {
      Surface *_Surf = 0;
      _Surf = FindSurface((*it)->classif_tag, M);
      if(!_Surf)
        _Surf = Create_Surface((*it)->classif_tag, MSH_SURF_DISCRETE);
      //      _Surf->bds = M->bds;
      End_Surface(_Surf);
      Tree_Add(M->Surfaces, &_Surf);
    }
    else if((*it)->classif_degree == 1) {
      Curve *_c = 0;
      _c = FindCurve((*it)->classif_tag, M);
      if(!_c)
        _c =
          Create_Curve((*it)->classif_tag, MSH_SEGM_DISCRETE, 1, NULL, NULL,
                       -1, -1, 0., 1.);
      //      _c->bds = M->bds;
      End_Curve(_c);
      Tree_Add(M->Curves, &_c);
    }
    else if((*it)->classif_degree == 0) {
      BDS_Point *p = (*it)->p;
      if(p) {
        Vertex *_v = Create_Vertex(p->iD, p->X, p->Y, p->Z, 1, 0);
        Tree_Add(M->Points, &_v);
      }
    }
    ++it;
  }

  CTX.mesh.changed = 1;
}


void  CreateVolumeWithAllSurfaces(Mesh *M)
{
  //  Volume *vol = Create_Volume(1, MSH_VOLUME_DISCRETE);
  //  Volume *vol = Create_Volume(1, 99999);
  Volume *vol2 = Create_Volume(2, MSH_VOLUME);
  List_T *surfaces = Tree2List(M->Surfaces); 
  for(int i = 0; i < List_Nbr(surfaces); ++i) {
    Surface *s;
    List_Read(surfaces, i, &s);
    //List_Add (vol->Surfaces,&s);
    List_Add (vol2->Surfaces,&s);
    int ori = 1;
    Move_SimplexBaseToSimplex(&s->SimplexesBase, s->Simplexes);
    List_Add (vol2->SurfacesOrientations,&ori);
  }
  //  Tree_Add(M->Volumes, &vol);
  Tree_Add(M->Volumes, &vol2);
}

int ReMesh(Mesh *M)
{
  if(M->status != 2)
    return 0;

  if(!M->bds) {
    Mesh_To_BDS(M);
    M->bds->classify(CTX.mesh.dihedral_angle_tol * M_PI / 180);
    BDS_To_Mesh(M);
  }

  DeleteMesh(M);

  if(M->bds_mesh) {
    delete M->bds_mesh;
    M->bds_mesh = 0;
  }


  MeshDiscreteSurface((Surface *) 0);
  CreateVolumeWithAllSurfaces(M);
  CTX.mesh.changed = 1;
  return 1;
}

// Public interface for discrete surface/curve mesh algo

int MeshDiscreteSurface(Surface * s)
{
  static  int NITER = 10;
  if(THEM->bds) {
    BDS_Metric metric(THEM->bds->LC / CTX.mesh.target_elem_size_fact,
                      THEM->bds->LC / CTX.mesh.min_elem_size_fact,
                      THEM->bds->LC,
                      CTX.mesh.beta_smooth_metric, CTX.mesh.nb_elem_per_rc);
    if(!THEM->bds_mesh) {
      Msg(STATUS1, "Remesh 2D...");      
      double t1 = Cpu();

      THEM->bds_mesh = new BDS_Mesh(*(THEM->bds));
      int iter = 0;
      while(iter < NITER && THEM->bds_mesh->adapt_mesh(metric, true, THEM->bds)) {
        Msg(STATUS2, "Iter=%d/%d Tri=%d", iter, NITER, THEM->bds_mesh->triangles.size());
        iter++;
      }
      BDS_To_Mesh_2(THEM);
      Msg(INFO, "Mesh has %d vertices (%d)", Tree_Nbr(THEM->Vertices),
          THEM->bds->points.size());
      // THEM->bds_mesh->save_gmsh_format("3.msh");

      double t2 = Cpu();
      Msg(STATUS1, "Remesh 2D complete (%g s)", t2 - t1);
      //      NITER++;
      return 1;
    }
    return 2;
  }
  else if(s->Typ == MSH_SURF_DISCRETE) {
    // nothing else to do: we assume that the surface is represented
    // by a mesh that will not be modified
    return 1;
  }
  else
    return 0;
}

int MeshDiscreteCurve(Curve * c)
{
  if(c->Typ == MSH_SEGM_DISCRETE) {
    // nothing else to do: we assume that the curve is represented by
    // a mesh that will not be modified
    return 1;
  }
  else
    return 0;
}
