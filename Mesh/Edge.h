#ifndef _EDGE_H_
#define _EDGE_H_

#include "List.h"
#include "Tree.h"
#include "Vertex.h"
#include "Simplex.h"

class Edge {
public :
  int Num;
  Vertex *V[2];
  List_T *Simplexes;
  Vertex *newv;
  Vertex *O[2];
  double a;
  List_T *Liste;
  List_T *Points;
  Edge()
    {
      Num = 0;
      Liste = NULL;
      Simplexes = NULL;
      Points = NULL;
    }
  // MEMORY LEAK (JF)
  ~Edge()
    {
      //if(Liste)List_Delete(Liste);
      //if(Simplexes)List_Delete(Simplexes);
      //if(Points)List_Delete(Points);
    }
};

class EdgesContainer
{
  public :
    Tree_T * AllEdges;
    EdgesContainer (Tree_T *Simplexes, bool i = false);
    EdgesContainer (List_T *Surfaces);
    ~EdgesContainer();
    void AddTree (Tree_T *Simplexes, bool EdgesInVolume);
    void AddEdges(Simplex *s, bool i);
    void GetEdges(Simplex *s, bool i, Edge *ed[4],int *ori);
    void RemoveEdge(Edge *e);
    void SwapEdge (Vertex *v[2]);
    bool Search(Vertex *v1, Vertex *v2);
};

int compareedge (const void *a, const void *b);
int compareedge_angle (const void *a, const void *b);

#endif
