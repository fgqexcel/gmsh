// Gmsh - Copyright (C) 1997-2017 C. Geuzaine, J.-F. Remacle
//
// See the LICENSE.txt file for license information. Please report all
// bugs and problems to the public mailing list <gmsh@onelab.info>.
//
// Contributed by Anthony Royer

#include <cstdio>
#include <fstream>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>
#include <string>

#include "GmshDefines.h"
#include "OS.h"
#include "Context.h"
#include "GModel.h"
#include "GEntity.h"
#include "partitionRegion.h"
#include "partitionFace.h"
#include "partitionEdge.h"
#include "partitionVertex.h"
#include "MPoint.h"
#include "MLine.h"
#include "MTriangle.h"
#include "MQuadrangle.h"
#include "MTetrahedron.h"
#include "MHexahedron.h"
#include "MPrism.h"
#include "MPyramid.h"
#include "MTrihedron.h"
#include "meshPartition.h"
#include "StringUtils.h"
#include "Options.h"

static void readMSH4Physicals(GModel *const model, FILE* fp, GEntity *const entity,
                              bool binary, char *str, bool swap)
{
  unsigned long numPhy = 0;
  if(binary){
    if(fread(&numPhy, sizeof(unsigned long), 1, fp) != 1){
      fclose(fp);
      return;
    }
    if(swap) SwapBytes((char*)&numPhy, sizeof(unsigned long), 1);

    int *phyTags = new int[numPhy];
    if(fread(phyTags, sizeof(int), numPhy, fp) != numPhy){
      fclose(fp);
      return;
    }
    if(swap) SwapBytes((char*)phyTags, sizeof(int), numPhy);

    for(unsigned int i = 0; i < numPhy; i++){
      entity->addPhysicalEntity(phyTags[i]);
    }
    delete[] phyTags;
  }
  else
  {
    sscanf(str, "%lu %[0-9- ]", &numPhy, str);
    for(unsigned int i = 0; i < numPhy; i++){
      int phyTag = 0;

      if(sscanf(str, "%d %[0-9- ]", &phyTag, str) != 2){
        return;
      }

      entity->addPhysicalEntity(phyTag);
    }
  }
}

static void readMSH4BoundingEntities(GModel *const model, FILE* fp,
                                     GEntity *const entity, bool binary,
                                     char *str, bool swap)
{
  unsigned long numBrep = 0;
  std::vector<GEntity*> boundingEntities;
  std::vector<int> boundingSign;

  if(binary){
    if(fread(&numBrep, sizeof(unsigned long), 1, fp) != 1){
      fclose(fp);
      return;
    }
    if(swap) SwapBytes((char*)&numBrep, sizeof(unsigned long), 1);

    int *brepTags = new int[numBrep];
    if(fread(brepTags, sizeof(int), numBrep, fp) != numBrep){
      fclose(fp);
      return;
    }
    if(swap) SwapBytes((char*)brepTags, sizeof(int), numBrep);

    for(unsigned int i = 0; i < numBrep; i++){
      GEntity *brep = model->getEntityByTag(entity->dim()-1, std::abs(brepTags[i]));
      if(!brep){
        Msg::Warning("Entity %d not found in the Brep of entity %d",
                     brepTags[i], entity->tag());
        return;
      }
      boundingEntities.push_back(brep);
      boundingSign.push_back((std::abs(brepTags[i]) == brepTags[i] ? 1 : -1));
    }
    delete[] brepTags;
  }
  else{
    sscanf(str, "%lu %[0-9- ]", &numBrep, str);
    for(unsigned int i = 0; i < numBrep; i++){
      int entityTag = 0;

      if(i != numBrep - 1){
        if(sscanf(str, "%d %[0-9- ]", &entityTag, str) != 2){
          return;
        }
      }
      else{
        if(sscanf(str, "%d", &entityTag) != 1){
          return;
        }
      }

      GEntity *brep = model->getEntityByTag(entity->dim()-1, std::abs(entityTag));
      if(!brep){
        Msg::Warning("Entity %d not found in the Brep of entity %d",
                     entityTag, entity->tag());
        return;
      }
      boundingEntities.push_back(brep);
      boundingSign.push_back((std::abs(entityTag) == entityTag ? 1 : -1));
    }
  }

  switch (entity->dim()) {
    case 1:
      if(boundingEntities.size() == 2){
        reinterpret_cast<discreteEdge*>(entity)->setBeginVertex
          (reinterpret_cast<GVertex*>(boundingEntities[0]));
        reinterpret_cast<discreteEdge*>(entity)->setEndVertex
          (reinterpret_cast<GVertex*>(boundingEntities[1]));
      }
      else if(boundingEntities.size() == 1){
        if(boundingSign[0] == 1){
          reinterpret_cast<discreteEdge*>(entity)->setBeginVertex
            (reinterpret_cast<GVertex*>(boundingEntities[0]));
        }
        else{
          reinterpret_cast<discreteEdge*>(entity)->setEndVertex
            (reinterpret_cast<GVertex*>(boundingEntities[0]));
        }
      }
      break;
    case 2:
    {
      std::vector<int> tags(boundingEntities.size());
      for(unsigned int i = 0; i < boundingEntities.size(); i++)
        tags[i] = std::abs(boundingEntities[i]->tag());
      reinterpret_cast<discreteFace*>(entity)->setBoundEdges(tags, boundingSign);
    }
      break;
    case 3:
    {
      std::vector<int> tags(boundingEntities.size());
      for(unsigned int i = 0; i < boundingEntities.size(); i++)
        tags[i] = std::abs(boundingEntities[i]->tag());
      reinterpret_cast<discreteRegion*>(entity)->setBoundFaces(tags, boundingSign);
    }
      break;
    default:
      break;
  }
}

std::vector<unsigned int> getPartition(GModel* model, GEntity* entity)
{
  std::vector<int> tags = entity->getPhysicalEntities();
  std::vector<unsigned int> partitions;
  for(unsigned int i = 0; i < tags.size(); i++){
    std::string name = model->getPhysicalName(entity->dim(),tags[i]);

    std::string integer;
    if(name[0] == '_'){
      for(unsigned int j = 0; j < name.length(); j++){
        if(name[j] == '{'){
          integer.clear();
        }
        else if(name[j] == ','){
#if __cplusplus >= 201103L
          partitions.push_back(stoi(integer));
#else
          partitions.push_back(atoi(integer.c_str()));
#endif
          integer.clear();
        }
        else if(name[j] == ' ') continue;
        else if(name[j] == '}'){
#if __cplusplus >= 201103L
          partitions.push_back(stoi(integer));
#else
          partitions.push_back(atoi(integer.c_str()));
#endif
          break;
        }
        else{
          integer += name[j];
        }
      }
    }
  }

  return partitions;
}

static void readMSH4Entities(GModel *const model, FILE* fp, bool partition,
                             bool binary, bool swap)
{
  char str[256];

  if(partition){
    int numPartitions = 0;
    if(binary){
      if(fread(&numPartitions, sizeof(int), 1, fp) != 1){
        fclose(fp);
        return;
      }
      if(swap) SwapBytes((char*)&numPartitions, sizeof(int), 1);
    }
    else{
      if(fscanf(fp, "%d", &numPartitions) != 1){
        fclose(fp);
        return;
      }
    }
    opt_mesh_partition_num(0, GMSH_SET, numPartitions);
  }
  unsigned long numVert = 0, numEdges = 0, numFaces = 0, numReg = 0;
  if(binary){
    unsigned long data[4];
    if(fread(data, sizeof(unsigned long), 4, fp) != 4){
      fclose(fp);
      return;
    }
    if(swap) SwapBytes((char*)data, sizeof(unsigned long), 4);
    numVert = data[0];
    numEdges = data[1];
    numFaces = data[2];
    numReg = data[3];
  }
  else{
    if(fscanf(fp, "%lu %lu %lu %lu", &numVert, &numEdges, &numFaces, &numReg) != 4){
      fclose(fp);
      return;
    }
  }
  //Vertices
  for(unsigned int i = 0; i < numVert; i++){
    int tag = 0;
    double minX = 0., minY = 0., minZ = 0., maxX = 0., maxY = 0., maxZ = 0.;
    int parentTag = 0;
    if(partition){
      if(binary){
        int dataInt[2];
        double dataDouble[6];
        if(fread(dataInt, sizeof(int), 2, fp) != 2){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)dataInt, sizeof(int), 2);
        if(fread(dataDouble, sizeof(double), 6, fp) != 6){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)dataDouble, sizeof(double), 6);
        tag = dataInt[0];
        parentTag = dataInt[1];
        minX = dataDouble[0];
        minY = dataDouble[1];
        minZ = dataDouble[2];
        maxX = dataDouble[3];
        maxY = dataDouble[4];
        maxZ = dataDouble[5];
      }
      else{
        if(fscanf(fp, "%d %d %lf %lf %lf %lf %lf %lf %[0-9- ]",
                  &tag, &parentTag, &minX, &minY, &minZ, &maxX, &maxY, &maxZ, str) != 9){
          fclose(fp);
          return;
        }
      }
    }
    else{
      if(binary){
        int dataInt;
        double dataDouble[6];
        if(fread(&dataInt, sizeof(int), 1, fp) != 1){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)&dataInt, sizeof(int), 1);
        if(fread(dataDouble, sizeof(double), 6, fp) != 6){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)dataDouble, sizeof(double), 6);
        tag = dataInt;
        minX = dataDouble[0];
        minY = dataDouble[1];
        minZ = dataDouble[2];
        maxX = dataDouble[3];
        maxY = dataDouble[4];
        maxZ = dataDouble[5];
      }
      else{
        if(fscanf(fp, "%d %lf %lf %lf %lf %lf %lf %[0-9- ]",
                  &tag, &minX, &minY, &minZ, &maxX, &maxY, &maxZ, str) != 8){
          fclose(fp);
          return;
        }
      }
    }

    GVertex *gv = model->getVertexByTag(tag);
    if (!gv){
      if(partition){
        gv = new partitionVertex(model, tag);
        static_cast<partitionVertex*>(gv)->setParentEntity
          ((parentTag != 0) ? model->getVertexByTag(parentTag) : 0);
      }
      else{
        gv = new discreteVertex(model, tag);
      }
      model->add(gv);
    }
    readMSH4Physicals(model, fp, gv, binary, str, swap);

    if(partition){
      std::vector<unsigned int> partitions = getPartition(model, gv);
      static_cast<partitionVertex*>(gv)->setPartition(partitions);
    }
  }

  // Edges
  for(unsigned int i = 0; i < numEdges; i++){
    int tag = 0;
    double minX = 0., minY = 0., minZ = 0., maxX = 0., maxY = 0., maxZ = 0.;
    int parentTag = 0;
    if(partition){
      if(binary){
        int dataInt[2];
        double dataDouble[6];
        if(fread(dataInt, sizeof(int), 2, fp) != 2){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)dataInt, sizeof(int), 2);
        if(fread(dataDouble, sizeof(double), 6, fp) != 6){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)dataDouble, sizeof(double), 6);
        tag = dataInt[0];
        parentTag = dataInt[1];
        minX = dataDouble[0];
        minY = dataDouble[1];
        minZ = dataDouble[2];
        maxX = dataDouble[3];
        maxY = dataDouble[4];
        maxZ = dataDouble[5];
      }
      else{
        if(fscanf(fp, "%d %d %lf %lf %lf %lf %lf %lf %[0-9- ]",
                  &tag, &parentTag, &minX, &minY, &minZ, &maxX, &maxY, &maxZ, str) != 9){
          fclose(fp);
          return;
        }
      }
    }
    else{
      if(binary){
        int dataInt;
        double dataDouble[6];
        if(fread(&dataInt, sizeof(int), 1, fp) != 1){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)&dataInt, sizeof(int), 1);
        if(fread(dataDouble, sizeof(double), 6, fp) != 6){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)dataDouble, sizeof(double), 6);
        tag = dataInt;
        minX = dataDouble[0];
        minY = dataDouble[1];
        minZ = dataDouble[2];
        maxX = dataDouble[3];
        maxY = dataDouble[4];
        maxZ = dataDouble[5];
      }
      else{
        if(fscanf(fp, "%d %lf %lf %lf %lf %lf %lf %[0-9- ]",
                  &tag, &minX, &minY, &minZ, &maxX, &maxY, &maxZ, str) != 8){
          fclose(fp);
          return;
        }
      }
    }

    GEdge *ge = model->getEdgeByTag(tag);
    if (!ge){
      if(partition){
        ge = new partitionEdge(model, tag, 0, 0);
        static_cast<partitionEdge*>(ge)->setParentEntity
          ((parentTag != 0) ? model->getEdgeByTag(parentTag) : 0);
      }
      else{
        ge = new discreteEdge(model, tag, 0, 0);
      }
      model->add(ge);
    }
    readMSH4Physicals(model, fp, ge, binary, str, swap);

    if(partition){
      std::vector<unsigned int> partitions = getPartition(model, ge);
      static_cast<partitionEdge*>(ge)->setPartition(partitions);
    }

    readMSH4BoundingEntities(model, fp, ge, binary, str, swap);
  }
  // Faces
  for(unsigned int i = 0; i < numFaces; i++){
    int tag = 0;
    double minX = 0., minY = 0., minZ = 0., maxX = 0., maxY = 0., maxZ = 0.;
    int parentTag = 0;
    if(partition){
      if(binary){
        int dataInt[2];
        double dataDouble[6];
        if(fread(dataInt, sizeof(int), 2, fp) != 2){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)dataInt, sizeof(int), 2);
        if(fread(dataDouble, sizeof(double), 6, fp) != 6){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)dataDouble, sizeof(double), 6);
        tag = dataInt[0];
        parentTag = dataInt[1];
        minX = dataDouble[0];
        minY = dataDouble[1];
        minZ = dataDouble[2];
        maxX = dataDouble[3];
        maxY = dataDouble[4];
        maxZ = dataDouble[5];
      }
      else{
        if(fscanf(fp, "%d %d %lf %lf %lf %lf %lf %lf %[0-9- ]",
                  &tag, &parentTag, &minX, &minY, &minZ, &maxX, &maxY, &maxZ, str) != 9){
          fclose(fp);
          return;
        }
      }
    }
    else{
      if(binary){
        int dataInt;
        double dataDouble[6];
        if(fread(&dataInt, sizeof(int), 1, fp) != 1){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)&dataInt, sizeof(int), 1);
        if(fread(dataDouble, sizeof(double), 6, fp) != 6){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)dataDouble, sizeof(double), 6);
        tag = dataInt;
        minX = dataDouble[0];
        minY = dataDouble[1];
        minZ = dataDouble[2];
        maxX = dataDouble[3];
        maxY = dataDouble[4];
        maxZ = dataDouble[5];
      }
      else{
        if(fscanf(fp, "%d %lf %lf %lf %lf %lf %lf %[0-9- ]",
                  &tag, &minX, &minY, &minZ, &maxX, &maxY, &maxZ, str) != 8){
          fclose(fp);
          return;
        }
      }
    }

    GFace *gf = model->getFaceByTag(tag);
    if (!gf){
      if(partition){
        gf = new partitionFace(model, tag);
        static_cast<partitionFace*>(gf)->setParentEntity
          ((parentTag != 0) ? model->getFaceByTag(parentTag) : 0);
      }
      else{
        gf = new discreteFace(model, tag);
      }
      model->add(gf);
    }
    readMSH4Physicals(model, fp, gf, binary, str, swap);

    if(partition){
      std::vector<unsigned int> partitions = getPartition(model, gf);
      static_cast<partitionFace*>(gf)->setPartition(partitions);
    }

    readMSH4BoundingEntities(model, fp, gf, binary, str, swap);
  }

  // Regions
  for(unsigned int i = 0; i < numReg; i++){
    int tag = 0;
    double minX = 0., minY = 0., minZ = 0., maxX = 0., maxY = 0., maxZ = 0.;
    int parentTag = 0;
    if(partition){
      if(binary){
        int dataInt[2];
        double dataDouble[6];
        if(fread(dataInt, sizeof(int), 2, fp) != 2){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)dataInt, sizeof(int), 2);
        if(fread(dataDouble, sizeof(double), 6, fp) != 6){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)dataDouble, sizeof(double), 6);
        tag = dataInt[0];
        parentTag = dataInt[1];
        minX = dataDouble[0];
        minY = dataDouble[1];
        minZ = dataDouble[2];
        maxX = dataDouble[3];
        maxY = dataDouble[4];
        maxZ = dataDouble[5];
      }
      else{
        if(fscanf(fp, "%d %d %lf %lf %lf %lf %lf %lf %[0-9- ]",
                  &tag, &parentTag, &minX, &minY, &minZ, &maxX, &maxY, &maxZ, str) != 9){
          fclose(fp);
          return;
        }
      }
    }
    else{
      if(binary){
        int dataInt;
        double dataDouble[6];
        if(fread(&dataInt, sizeof(int), 1, fp) != 1){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)&dataInt, sizeof(int), 1);
        if(fread(dataDouble, sizeof(double), 6, fp) != 6){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)dataDouble, sizeof(double), 6);
        tag = dataInt;
        minX = dataDouble[0];
        minY = dataDouble[1];
        minZ = dataDouble[2];
        maxX = dataDouble[3];
        maxY = dataDouble[4];
        maxZ = dataDouble[5];
      }
      else{
        if(fscanf(fp, "%d %lf %lf %lf %lf %lf %lf %[0-9- ]",
                  &tag, &minX, &minY, &minZ, &maxX, &maxY, &maxZ, str) != 8){
          fclose(fp);
          return;
        }
      }
    }

    GRegion *gr = model->getRegionByTag(tag);
    if (!gr){
      if(partition){
        gr = new partitionRegion(model, tag);
        static_cast<partitionRegion*>(gr)->setParentEntity
          ((parentTag != 0) ? model->getRegionByTag(parentTag) : 0);
      }
      else{
        gr = new discreteRegion(model, tag);
      }
      model->add(gr);
    }
    readMSH4Physicals(model, fp, gr, binary, str, swap);

    if(partition){
      std::vector<unsigned int> partitions = getPartition(model, gr);
      static_cast<partitionRegion*>(gr)->setPartition(partitions);
    }

    readMSH4BoundingEntities(model, fp, gr, binary, str, swap);
  }
}

static std::pair<int, MVertex*> *readMSH4Nodes(GModel *const model, FILE* fp,
                                               bool binary, bool parametric, bool &dense,
                                               unsigned long &nbrNodes, bool swap)
{
  unsigned long numBlock = 0;
  nbrNodes = 0;
  if(binary){
    unsigned long data[2];
    if(fread(data, sizeof(unsigned long), 2, fp) != 2){
      fclose(fp);
      return 0;
    }
    if(swap) SwapBytes((char*)data, sizeof(unsigned long), 2);
    numBlock = data[0];
    nbrNodes = data[1];
  }
  else{
    if(fscanf(fp, "%lu %lu", &numBlock, &nbrNodes) != 2){
      fclose(fp);
      return 0;
    }
  }

  unsigned long nodeRead = 0;
  unsigned long minVertex = nbrNodes+1, maxVertex = 0;
  std::pair<int, MVertex*> *vertexCache = new std::pair<int, MVertex*>[nbrNodes];
  Msg::Info("%lu vertices", nbrNodes);
  for(unsigned int i = 0; i < numBlock; i++){
    int entityTag = 0, entityDim = 0;
    unsigned long numNodes = 0;

    if(binary){
      int data[2];
      if(fread(data, sizeof(int), 2, fp) != 2){
        fclose(fp);
        return 0;
      }
      if(swap) SwapBytes((char*)data, sizeof(int), 2);
      entityTag = data[0];
      entityDim = data[1];

      unsigned long dataLong;
      if(fread(&dataLong, sizeof(unsigned long), 1, fp) != 1){
        fclose(fp);
        return 0;
      }
      if(swap) SwapBytes((char*)&dataLong, sizeof(unsigned long), 1);
      numNodes = dataLong;
    }
    else{
      if(fscanf(fp, "%d %d %lu", &entityTag, &entityDim, &numNodes) != 3){
        fclose(fp);
        return 0;
      }
    }

    GEntity *entity = model->getEntityByTag(entityDim, entityTag);
    if(!entity){
      fclose(fp);
      Msg::Error("Unknown entity %d of dimension %d", entityTag, entityDim);
      return 0;
    }

    for(unsigned int j = 0; j < numNodes; j++){
      double xyz[3];
      int nodeTag = 0;
      MVertex *vertex = 0;

      if(parametric){
        double u = 0., v = 0.;

        switch (entityDim){
          case 0:
            if(binary){
              if(fread(&nodeTag, sizeof(int), 1, fp) != 1){
                fclose(fp);
                return 0;
              }
              if(swap) SwapBytes((char*)&nodeTag, sizeof(int), 1);

              if(fread(xyz, sizeof(double), 3, fp) != 3){
                fclose(fp);
                return 0;
              }
              if(swap) SwapBytes((char*)xyz, sizeof(double), 3);
            }
            else{
              if(fscanf(fp, "%d %lf %lf %lf",
                        &nodeTag, &xyz[0], &xyz[1], &xyz[2]) != 4){
                fclose(fp);
                return 0;
              }
            }
            vertex = new MVertex(xyz[0], xyz[1], xyz[2], entity, nodeTag);
            break;
          case 1:
            if(binary){
              if(fread(&nodeTag, sizeof(int), 1, fp) != 1){
                fclose(fp);
                return 0;
              }
              if(swap) SwapBytes((char*)&nodeTag, sizeof(int), 1);

              if(fread(xyz, sizeof(double), 3, fp) != 3){
                fclose(fp);
                return 0;
              }
              if(swap) SwapBytes((char*)xyz, sizeof(double), 3);

              if(fread(&u, sizeof(double), 1, fp) != 1){
                fclose(fp);
                return 0;
              }
              if(swap) SwapBytes((char*)&u, sizeof(double), 1);
            }
            else{
              if(fscanf(fp, "%d %lf %lf %lf %lf",
                        &nodeTag, &xyz[0], &xyz[1], &xyz[2], &u) != 5){
                fclose(fp);
                return 0;
              }
            }
            vertex = new MEdgeVertex(xyz[0], xyz[1], xyz[2], entity, u, -1.0, nodeTag);
            break;
          case 2:
            if(binary){
              if(fread(&nodeTag, sizeof(int), 1, fp) != 1){
                fclose(fp);
                return 0;
              }
              if(swap) SwapBytes((char*)&nodeTag, sizeof(int), 1);

              if(fread(xyz, sizeof(double), 3, fp) != 3){
                fclose(fp);
                return 0;
              }
              if(swap) SwapBytes((char*)xyz, sizeof(double), 3);

              double uv[2];
              if(fread(uv, sizeof(double), 2, fp) != 2){
                fclose(fp);
                return 0;
              }
              if(swap) SwapBytes((char*)uv, sizeof(double), 2);

              u = uv[0];
              v = uv[1];
            }
            else{
              if(fscanf(fp, "%d %lf %lf %lf %lf %lf",
                        &nodeTag, &xyz[0], &xyz[1], &xyz[2], &u, &v) != 6){
                fclose(fp);
                return 0;
              }
            }
            vertex = new MFaceVertex(xyz[0], xyz[1], xyz[2], entity, u, v, nodeTag);
            break;
          case 3:
            if(binary){
              if(fread(&nodeTag, sizeof(int), 1, fp) != 1){
                fclose(fp);
                return 0;
              }
              if(swap) SwapBytes((char*)&nodeTag, sizeof(int), 1);

              if(fread(xyz, sizeof(double), 3, fp) != 3){
                fclose(fp);
                return 0;
              }
              if(swap) SwapBytes((char*)xyz, sizeof(double), 3);
            }
            else{
              if(fscanf(fp, "%d %lf %lf %lf", &nodeTag, &xyz[0], &xyz[1], &xyz[2]) != 4){
                fclose(fp);
                return 0;
              }
            }
            vertex = new MVertex(xyz[0], xyz[1], xyz[2], entity, nodeTag);
            break;
          default:
            fclose(fp);
            return 0;
            break;
        }
      }
      else{
        if(binary){
          if(fread(&nodeTag, sizeof(int), 1, fp) != 1){
            fclose(fp);
            return 0;
          }
          if(swap) SwapBytes((char*)&nodeTag, sizeof(int), 1);

          if(fread(xyz, sizeof(double), 3, fp) != 3){
            fclose(fp);
            return 0;
          }
          if(swap) SwapBytes((char*)xyz, sizeof(double), 3);
        }
        else{
          if(fscanf(fp, "%d %lf %lf %lf",
                    &nodeTag, &xyz[0], &xyz[1], &xyz[2]) != 4){
            fclose(fp);
            return 0;
          }
        }
        vertex = new MVertex(xyz[0], xyz[1], xyz[2], entity, nodeTag);
      }
      entity->addMeshVertex(vertex);
      vertex->setEntity(entity);
      minVertex = std::min(minVertex, (unsigned long)nodeTag);
      maxVertex = std::max(maxVertex, (unsigned long)nodeTag);

      vertexCache[nodeRead] = std::pair<int, MVertex*>(nodeTag, vertex);
      nodeRead ++;

      if(nbrNodes > 100000) Msg::ProgressMeter(nodeRead, nbrNodes, true, "Reading nodes");
    }
  }
  // if the vertex numbering is dense, we fill the vector cache, otherwise we
  // fill the map cache
  if(minVertex == 1 && maxVertex == nbrNodes){ // If dense
    Msg::Debug("Vertex numbering is dense");
    dense = true;
  }
  else{
    Msg::Debug("Vertex numbering is not dense");
    dense = false;
  }

  return vertexCache;
}

static std::pair<int, MElement*> *readMSH4Elements(GModel *const model, FILE* fp,
                                                   bool binary, bool &dense,
                                                   unsigned long &nbrElements, bool swap)
{
  char str[256];
  unsigned long numBlock = 0;
  nbrElements = 0;
  if(binary){
    unsigned long data[2];
    if(fread(data, sizeof(unsigned long), 2, fp) != 2){
      fclose(fp);
      return 0;
    }
    if(swap) SwapBytes((char*)data, sizeof(unsigned long), 2);

    numBlock = data[0];
    nbrElements = data[1];
  }
  else{
    if(fscanf(fp, "%lu %lu", &numBlock, &nbrElements) != 2){
      fclose(fp);
      return 0;
    }
  }

  unsigned long elementRead = 0;
  unsigned long minElement = nbrElements+1, maxElement = 0;
  std::pair<int, MElement*> *elementCache = new std::pair<int, MElement*>[nbrElements];
  Msg::Info("%lu elements", nbrElements);
  for(unsigned int i = 0; i < numBlock; i++){
    int entityTag = 0, entityDim = 0, elmType = 0;
    unsigned long numElements = 0;

    if(binary){
      int data[3];
      if(fread(data, sizeof(int), 3, fp) != 3){
        fclose(fp);
        return 0;
      }
      if(swap) SwapBytes((char*)data, sizeof(int), 3);

      entityTag = data[0];
      entityDim = data[1];
      elmType = data[2];

      unsigned long dataLong;
      if(fread(&dataLong, sizeof(unsigned long), 1, fp) != 1){
        fclose(fp);
        return 0;
      }
      if(swap) SwapBytes((char*)&dataLong, sizeof(unsigned long), 1);
      numElements = dataLong;
    }
    else{
      if(fscanf(fp, "%d %d %d %lu",
                &entityTag, &entityDim, &elmType, &numElements) != 4){
        fclose(fp);
        return 0;
      }
    }

    GEntity *entity = model->getEntityByTag(entityDim, entityTag);
    if(!entity){
      fclose(fp);
      Msg::Error("Unknown entity %d of dimension %d", entityTag, entityDim);
      return 0;
    }

    int nbrVertices = MElement::getInfoMSH(elmType);
    if(binary){
      int *data = new int[numElements*(nbrVertices+1)];
      if(fread(data, sizeof(int), numElements*(nbrVertices+1), fp) !=
         numElements*(nbrVertices+1)){
        fclose(fp);
        return 0;
      }
      if(swap) SwapBytes((char*)data, sizeof(int), numElements*(nbrVertices+1));

      std::vector<MVertex*> vertices(nbrVertices+1, (MVertex*)0);
      for(unsigned int j = 0; j < numElements*(nbrVertices+1); j += (nbrVertices+1)){
        for(int k = 0; k < nbrVertices; k++){
          vertices[k] = model->getMeshVertexByTag(data[j+k+1]);
          if(!vertices[k]){
            Msg::Error("Unknown vertex %d in element %d", data[j+k+1], data[j]);
            fclose(fp);
            return 0;
          }
        }

        MElementFactory elementFactory;
        MElement *element = elementFactory.create(elmType, vertices, data[j],
                                                  0, false, 0, 0, 0, 0);

        entity->addElement(element->getType(), element);

        minElement = std::min(minElement, (unsigned long)data[j]);
        maxElement = std::max(maxElement, (unsigned long)data[j]);

        elementCache[elementRead] = std::pair<int, MElement*>(data[j], element);
        elementRead ++;

        if(nbrElements > 100000)
          Msg::ProgressMeter(elementRead, nbrElements, true, "Reading elements");
      }

      delete[] data;
    }
    else{
      for(unsigned int j = 0; j < numElements; j++){
        int elmTag = 0;
        if(fscanf(fp, "%d %[0-9- ]", &elmTag, str) != 2){
          fclose(fp);
          return 0;
        }

        std::vector<MVertex*> vertices(nbrVertices+1, (MVertex*)0);

        for(int k = 0; k < nbrVertices; k++){
          int vertexTag = 0;
          if(k != nbrVertices-1){
            if(sscanf(str, "%d %[0-9- ]", &vertexTag, str) != 2){
              fclose(fp);
              return 0;
            }
          }
          else{
            if(sscanf(str, "%d", &vertexTag) != 1){
              fclose(fp);
              return 0;
            }
          }

          vertices[k] = model->getMeshVertexByTag(vertexTag);
          if(!vertices[k]){
            Msg::Error("Unknown vertex %d in element %d", vertexTag, elmTag);
            fclose(fp);
            return 0;
          }
        }

        MElementFactory elementFactory;
        MElement *element = elementFactory.create(elmType, vertices, elmTag,
                                                  0, false, 0, 0, 0, 0);

        entity->addElement(element->getType(), element);

        minElement = std::min(minElement, (unsigned long)elmTag);
        maxElement = std::max(maxElement, (unsigned long)elmTag);

        elementCache[elementRead] = std::pair<int, MElement*>(elmTag, element);
        elementRead ++;

        if(nbrElements > 100000)
          Msg::ProgressMeter(elementRead, nbrElements, true, "Reading elements");
      }
    }
  }
  // if the vertex numbering is dense, we fill the vector cache, otherwise we
  // fill the map cache
  if(minElement == 1 && maxElement == nbrElements){ //If dense
    Msg::Debug("Element numbering is dense");
    dense = true;
  }
  else{
    Msg::Debug("Element numbering is not dense");
    dense = false;
  }

  return elementCache;
}

static void readMSH4PeriodicNodes(GModel *const model, FILE* fp,
                                  bool binary, bool swap)
{
  int numPeriodicLinks = 0;
  if(binary){
    if(fread(&numPeriodicLinks, sizeof(int), 1, fp) != 1){
      fclose(fp);
      return;
    }
    if(swap) SwapBytes((char*)&numPeriodicLinks, sizeof(int), 1);
  }
  else{
    if(fscanf(fp, "%d", &numPeriodicLinks) != 1){
      fclose(fp);
      return;
    }
  }

  for(int i = 0; i < numPeriodicLinks; i++){
    int slaveDim = 0, slaveTag = 0, masterTag = 0;

    if(binary){
      int data[3];
      if(fread(&data, sizeof(int), 3, fp) != 3){
        fclose(fp);
        return;
      }
      if(swap) SwapBytes((char*)data, sizeof(int), 3);
      slaveDim = data[0];
      slaveTag = data[1];
      masterTag = data[2];
    }
    else{
      if(fscanf(fp, "%d %d %d", &slaveDim, &slaveTag, &masterTag) != 3){
        fclose(fp);
        return;
      }
    }

    GEntity *slave = 0, *master = 0;
    switch(slaveDim){
      case 0 :
        slave = model->getVertexByTag(slaveTag);
        master = model->getVertexByTag(masterTag);
        break;
      case 1 :
        slave = model->getEdgeByTag(slaveTag);
        master = model->getEdgeByTag(masterTag);
        break;
      case 2 :
        slave = model->getFaceByTag(masterTag);
        master = model->getFaceByTag(masterTag);
        break;
    }

    if(!slave){
      Msg::Error("Could not find periodic slave entity %d of dimension %d",
                 slaveTag, slaveDim);
    }
    if(!master){
      Msg::Error("Could not find periodic master entity %d of dimension %d",
                 masterTag, slaveDim);
    }

    long correspondingVertexSize = 0;
    if(binary){
      if(fread(&correspondingVertexSize, sizeof(long), 1, fp) != 1){
        fclose(fp);
        return;
      }
      if(swap) SwapBytes((char*)&correspondingVertexSize, sizeof(long), 1);

      if(correspondingVertexSize < 0){ //If there is an affine parameter
        double data[16];
        if(fread(&data, sizeof(double), 16, fp) != 16){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)data, sizeof(double), 16);

        std::vector<double> tfo(16);
        for(int j = 0; j < 16; j++){
          tfo[j] = data[j];
        }
        slave->setMeshMaster(master, tfo);

        if(fread(&correspondingVertexSize, sizeof(long), 1, fp) != 1){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)&correspondingVertexSize, sizeof(long), 1);
      }
    }
    else{
      char affine[256];

      if(fscanf(fp, "%s", affine) != 1){
        fclose(fp);
        return;
      }

      if(strncmp(affine, "Affine", 6) == 0){
        std::vector<double> tfo(16);
        if(sscanf(affine, "%*s %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf "
                  "%lf %lf %lf %lf", &tfo[0], &tfo[1], &tfo[2], &tfo[3], &tfo[4],
                  &tfo[5], &tfo[6], &tfo[7], &tfo[8], &tfo[9], &tfo[10], &tfo[11],
                  &tfo[12], &tfo[13], &tfo[14], &tfo[15]) != 16){
          fclose(fp);
          return;
        }
        slave->setMeshMaster(master, tfo);
      }
      else{
        slave->setMeshMaster(master);
      }

      if(sscanf(affine, "%lu", &correspondingVertexSize) != 1){
        fclose(fp);
        return;
      }
    }

    for(long j = 0; j < correspondingVertexSize; j++){
      int v1 = 0, v2 = 0;
      if(binary){
        int data[2];
        if(fread(&data, sizeof(int), 2, fp) != 2){
          fclose(fp);
          return;
        }
        if(swap) SwapBytes((char*)data, sizeof(int), 2);

        v1 = data[0];
        v2 = data[1];
      }
      else{
        if(fscanf(fp, "%d %d", &v1, &v2) != 2){
          fclose(fp);
          return;
        }
      }
      MVertex *mv1 = model->getMeshVertexByTag(v1);
      MVertex *mv2 = model->getMeshVertexByTag(v2);

      if(!mv1){
        Msg::Error("Could not find periodic vertex %d", v1);
      }
      if(!mv2){
        Msg::Error("Could not find periodic vertex %d", v2);
      }

      slave->correspondingVertices[mv1] = mv2;
    }
  }
}

int GModel::_readMSH4(const std::string &name)
{
  bool partitioned = false;
  FILE *fp = Fopen(name.c_str(), "rb");
  if(!fp){
    Msg::Error("Unable to open file '%s'", name.c_str());
    return 0;
  }

  char str[256] = "x";
  double version = 1.0;
  bool binary = false, swap = false, postpro = false;

  while(1)
  {
    while(str[0] != '$')
    {
      if(!fgets(str, sizeof(str), fp) || feof(fp))
        break;
    }

    std::string sectionName(&str[1]);
    std::string endSectionName = "End" + sectionName;
    if(feof(fp))
      break;

    if(!strncmp(&str[1], "MeshFormat", 10)){
      if(!fgets(str, sizeof(str), fp) || feof(fp)){
        fclose(fp);
        return 0;
      }

      int format;
      unsigned long size;
      if(sscanf(str, "%lf %d %lu", &version, &format, &size) != 3){
        fclose(fp);
        return 0;
      }
      if(format){
        binary = true;
        Msg::Debug("Mesh is in binary format");
        int one;
        if(fread(&one, sizeof(int), 1, fp) != 1){
          fclose(fp);
          return 0;
        }
        if(one != 1){
          swap = true;
          Msg::Debug("Swapping bytes from binary file");
        }
      }
    }
    else if(!strncmp(&str[1], "PhysicalNames", 13)){
      if(!fgets(str, sizeof(str), fp) || feof(fp)){
        fclose(fp); return 0;
      }
      int numPhysicalNames = 0;
      if(sscanf(str, "%d", &numPhysicalNames) != 1){
        fclose(fp); return 0;
      }
      for(int i = 0; i < numPhysicalNames; i++){
        int dim = 0, tag = 0;
        if(fscanf(fp, "%d %d", &dim, &tag) != 2){
          fclose(fp);
          return 0;

        }
        char name[128];
        if(!fgets(name, sizeof(name), fp)){
          fclose(fp);
          return 0;
        }
        std::string physicalName = ExtractDoubleQuotedString(name, 128);
        if(physicalName.size()) setPhysicalName(physicalName, dim, tag);
      }
    }
    else if(!strncmp(&str[1], "Entities", 8)){
      readMSH4Entities(this, fp, false, binary, swap);
    }
    else if(!strncmp(&str[1], "PartitionedEntities", 19)){
      readMSH4Entities(this, fp, true, binary, swap);
      partitioned = true;
    }
    else if(!strncmp(&str[1], "Nodes", 5)){
      _vertexVectorCache.clear();
      _vertexMapCache.clear();
      Msg::ResetProgressMeter();
      bool dense = false;
      unsigned long nbrNodes = 0;
      std::pair<int, MVertex*> *vertexCache = readMSH4Nodes
        (this, fp, binary, false, dense, nbrNodes, swap);
      if(vertexCache){
        if(dense){
          _vertexVectorCache.resize(nbrNodes+1, 0);
          _vertexVectorCache[0] = 0;

          for(unsigned int i = 0; i < nbrNodes; i++){
            if(!_vertexVectorCache[vertexCache[i].first]){
              _vertexVectorCache[vertexCache[i].first] = vertexCache[i].second;
            }
            else{
              Msg::Warning("Skipping duplicate vertex %d", vertexCache[i].first);
            }
          }
        }
        else{
          for(unsigned int i = 0; i < nbrNodes; i++){
            if(_vertexMapCache.count(vertexCache[i].first) == 0){
              _vertexMapCache[vertexCache[i].first] = vertexCache[i].second;
            }
            else{
              Msg::Warning("Skipping duplicate vertex %d", vertexCache[i].first);
            }
          }
        }

        delete[] vertexCache;
      }
      else{
        return 0;
      }
    }
    else if(!strncmp(&str[1], "ParametricNodes", 15)){
      _vertexVectorCache.clear();
      _vertexMapCache.clear();
      Msg::ResetProgressMeter();
      bool dense = false;
      unsigned long nbrNodes = 0;
      std::pair<int, MVertex*> *vertexCache = readMSH4Nodes
        (this, fp, binary, true, dense, nbrNodes, swap);
      if(vertexCache){
        if(dense){
          _vertexVectorCache.resize(nbrNodes+1, 0);
          _vertexVectorCache[0] = 0;

          for(unsigned int i = 0; i < nbrNodes; i++){
            if(!_vertexVectorCache[vertexCache[i].first]){
              _vertexVectorCache[vertexCache[i].first] = vertexCache[i].second;
            }
            else{
              Msg::Warning("Skipping duplicate vertex %d", vertexCache[i].first);
            }
          }
        }
        else{
          for(unsigned int i = 0; i < nbrNodes; i++){
            if(_vertexMapCache.count(vertexCache[i].first) == 0){
              _vertexMapCache[vertexCache[i].first] = vertexCache[i].second;
            }
            else{
              Msg::Warning("Skipping duplicate vertex %d", vertexCache[i].first);
            }
          }
        }

        delete[] vertexCache;
      }
      else{
        return 0;
      }
    }
    else if(!strncmp(&str[1], "Elements", 8)){
      Msg::ResetProgressMeter();
      bool dense = false;
      unsigned long nbrElements = 0;
      std::pair<int, MElement*> *elementCache = readMSH4Elements
        (this, fp, binary, dense, nbrElements, swap);
      if(elementCache){
        if(dense){
          _elementVectorCache.resize(nbrElements+1, 0);
          _elementVectorCache[0] = 0;

          for(unsigned int i = 0; i < nbrElements; i++){
            if(!_elementVectorCache[elementCache[i].first]){
              _elementVectorCache[elementCache[i].first] = elementCache[i].second;
            }
            else{
              Msg::Warning("Skipping duplicate element %d", elementCache[i].first);
            }
          }
        }
        else{
          for(unsigned int i = 0; i < nbrElements; i++){
            if(_elementMapCache.count(elementCache[i].first) == 0){
              _elementMapCache[elementCache[i].first] = elementCache[i].second;
            }
            else{
              Msg::Warning("Skipping duplicate element %d", elementCache[i].first);
            }
          }
        }

        delete[] elementCache;
      }
      else{
        return 0;
      }
    }
    else if(!strncmp(&str[1], "Periodic", 8)){
      readMSH4PeriodicNodes(this, fp, binary, swap);
    }
    else if(!strncmp(&str[1], "NodeData", 8) || !strncmp(&str[1], "ElementData", 11) ||
            !strncmp(&str[1], "ElementNodeData", 15)){
      postpro = true;
      break;
    }

    while(strncmp(&str[1], endSectionName.c_str(), endSectionName.size())){
      if(!fgets(str, sizeof(str), fp) || feof(fp)){
        break;
      }
    }
    str[0] = 'a';
  }

  fclose(fp);

  // This part is added to ensure the compatibility between the new partitioning
  // and the old one.
  if(partitioned){
    std::vector<GEntity*> entities;
    getEntities(entities);
    for(unsigned int i = 0; i < entities.size(); i++){
      if(entities[i]->geomType() == GEntity::PartitionVertex){
        partitionVertex *pv = static_cast<partitionVertex*>(entities[i]);
        if(pv->numPartitions() == 1){
          const unsigned int part = pv->getPartition(0);
          for(unsigned int j = 0; j < pv->getNumMeshElements(); j++){
            pv->getMeshElement(j)->setPartition(part);
          }
        }
      }
      else if(entities[i]->geomType() == GEntity::PartitionCurve){
        partitionEdge *pe = static_cast<partitionEdge*>(entities[i]);
        if(pe->numPartitions() == 1){
          const unsigned int part = pe->getPartition(0);
          for(unsigned int j = 0; j < pe->getNumMeshElements(); j++){
            pe->getMeshElement(j)->setPartition(part);
          }
        }
      }
      else if(entities[i]->geomType() == GEntity::PartitionSurface){
        partitionFace *pf = static_cast<partitionFace*>(entities[i]);
        if(pf->numPartitions() == 1){
          const unsigned int part = pf->getPartition(0);
          for(unsigned int j = 0; j < pf->getNumMeshElements(); j++){
            pf->getMeshElement(j)->setPartition(part);
          }
        }
      }
      else if(entities[i]->geomType() == GEntity::PartitionVolume){
        partitionRegion *pr = static_cast<partitionRegion*>(entities[i]);
        if(pr->numPartitions() == 1){
          const unsigned int part = pr->getPartition(0);
          for(unsigned int j = 0; j < pr->getNumMeshElements(); j++){
            pr->getMeshElement(j)->setPartition(part);
          }
        }
      }
    }

    recomputeMeshPartitions();
  }

  return postpro ? 2 : 1;
}

static void writeMSH4Physicals(FILE *fp, GEntity *const entity, bool binary)
{
  if(binary){
    std::vector<int> phys = entity->getPhysicalEntities();
    unsigned long phySize = phys.size();
    fwrite(&phySize, sizeof(unsigned long), 1, fp);
    for(unsigned int i = 0; i < phys.size(); i++)
    {
      int phy = phys[i];
      fwrite(&phy, sizeof(int), 1, fp);
    }
  }
  else{
    std::vector<int> phys = entity->getPhysicalEntities();
    fprintf(fp, "%lu", phys.size());
    for(unsigned int i = 0; i < phys.size(); i++)
    {
      fprintf(fp, " %d", phys[i]);
    }
    fprintf(fp, " ");
  }
}

static void writeMSH4Entities(GModel *const model, FILE *fp, bool partition,
                              bool binary, double scalingFactor)
{
  std::set<GRegion*, GEntityLessThan> regions;
  std::set<GFace*, GEntityLessThan> faces;
  std::set<GEdge*, GEntityLessThan> edges;
  std::set<GVertex*, GEntityLessThan> vertices;

  if(partition){
    for(GModel::viter it = model->firstVertex(); it != model->lastVertex(); ++it)
      if((*it)->geomType() == GEntity::PartitionVertex)
        vertices.insert(*it);
    for(GModel::eiter it = model->firstEdge(); it != model->lastEdge(); ++it)
      if((*it)->geomType() == GEntity::PartitionCurve)
        edges.insert(*it);
    for(GModel::fiter it = model->firstFace(); it != model->lastFace(); ++it)
      if((*it)->geomType() == GEntity::PartitionSurface)
        faces.insert(*it);
    for(GModel::riter it = model->firstRegion(); it != model->lastRegion(); ++it)
      if((*it)->geomType() == GEntity::PartitionVolume)
        regions.insert(*it);
  }
  else{
    for(GModel::viter it = model->firstVertex(); it != model->lastVertex(); ++it)
      if((*it)->geomType() != GEntity::PartitionVertex)
        vertices.insert(*it);
    for(GModel::eiter it = model->firstEdge(); it != model->lastEdge(); ++it)
      if((*it)->geomType() != GEntity::PartitionCurve)
        edges.insert(*it);
    for(GModel::fiter it = model->firstFace(); it != model->lastFace(); ++it)
      if((*it)->geomType() != GEntity::PartitionSurface)
        faces.insert(*it);
    for(GModel::riter it = model->firstRegion(); it != model->lastRegion(); ++it)
      if((*it)->geomType() != GEntity::PartitionVolume)
        regions.insert(*it);
  }

  if(binary){
    if(partition) {
      unsigned int nparts = model->getNumPartitions();
      fwrite(&nparts, sizeof(unsigned int), 1, fp);
    }
    unsigned long verticesSize = vertices.size();
    unsigned long edgesSize = edges.size();
    unsigned long facesSize = faces.size();
    unsigned long regionsSize = regions.size();
    fwrite(&verticesSize, sizeof(unsigned long), 1, fp);
    fwrite(&edgesSize, sizeof(unsigned long), 1, fp);
    fwrite(&facesSize, sizeof(unsigned long), 1, fp);
    fwrite(&regionsSize, sizeof(unsigned long), 1, fp);

    for(GModel::viter it = vertices.begin(); it != vertices.end(); ++it){
      int entityTag = (*it)->tag();
      fwrite(&entityTag, sizeof(int), 1, fp);
      if(partition){
        int parentEntityTag = 0;
        if(static_cast<partitionVertex*>(*it)->getParentEntity()){
          parentEntityTag = static_cast<partitionVertex*>(*it)->getParentEntity()->tag();
        }
        fwrite(&parentEntityTag, sizeof(int), 1, fp);
      }
      SBoundingBox3d boundBox = (*it)->bounds();
      boundBox *= scalingFactor;
      double minX = boundBox.min().x();
      double minY = boundBox.min().y();
      double minZ = boundBox.min().z();
      double maxX = boundBox.max().x();
      double maxY = boundBox.max().y();
      double maxZ = boundBox.max().z();
      fwrite(&minX, sizeof(double), 1, fp);
      fwrite(&minY, sizeof(double), 1, fp);
      fwrite(&minZ, sizeof(double), 1, fp);
      fwrite(&maxX, sizeof(double), 1, fp);
      fwrite(&maxY, sizeof(double), 1, fp);
      fwrite(&maxZ, sizeof(double), 1, fp);

      writeMSH4Physicals(fp, *it, binary);
    }

    for(GModel::eiter it = edges.begin(); it != edges.end(); ++it){
      std::list<GVertex*> vertices;
      std::vector<int> ori;
      if((*it)->getBeginVertex()){
        vertices.push_back((*it)->getBeginVertex());
        ori.push_back(1);
      }
      if((*it)->getEndVertex()) {// I use the convention that the end vertex is negative
        vertices.push_back((*it)->getEndVertex());
        ori.push_back(-1);
      }
      unsigned long verticesSize = vertices.size();

      int entityTag = (*it)->tag();
      fwrite(&entityTag, sizeof(int), 1, fp);
      if(partition){
        int parentEntityTag = 0;
        if(static_cast<partitionEdge*>(*it)->getParentEntity()){
          parentEntityTag = static_cast<partitionEdge*>(*it)->getParentEntity()->tag();
        }
        fwrite(&parentEntityTag, sizeof(int), 1, fp);
      }

      SBoundingBox3d boundBox = (*it)->bounds();
      double minX = boundBox.min().x();
      double minY = boundBox.min().y();
      double minZ = boundBox.min().z();
      double maxX = boundBox.max().x();
      double maxY = boundBox.max().y();
      double maxZ = boundBox.max().z();
      fwrite(&minX, sizeof(double), 1, fp);
      fwrite(&minY, sizeof(double), 1, fp);
      fwrite(&minZ, sizeof(double), 1, fp);
      fwrite(&maxX, sizeof(double), 1, fp);
      fwrite(&maxY, sizeof(double), 1, fp);
      fwrite(&maxZ, sizeof(double), 1, fp);

      writeMSH4Physicals(fp, *it, binary);

      fwrite(&verticesSize, sizeof(unsigned long), 1, fp);
      int oriI = 0;
      for(std::list<GVertex*>::iterator itv = vertices.begin();
          itv != vertices.end(); itv++){
        int brepTag = ori[oriI] * (*itv)->tag();
        fwrite(&brepTag, sizeof(int), 1, fp);
        oriI++;
      }
    }

    for(GModel::fiter it = faces.begin(); it != faces.end(); ++it){
      std::list<GEdge*> edges = (*it)->edges();
      std::list<int> ori = (*it)->edgeOrientations();
      unsigned long edgesSize = edges.size();

      int entityTag = (*it)->tag();
      fwrite(&entityTag, sizeof(int), 1, fp);
      if(partition){
        int parentEntityTag = 0;
        if(static_cast<partitionFace*>(*it)->getParentEntity()){
          parentEntityTag = static_cast<partitionFace*>(*it)->getParentEntity()->tag();
        }
        fwrite(&parentEntityTag, sizeof(int), 1, fp);
      }

      SBoundingBox3d boundBox = (*it)->bounds();
      boundBox *= scalingFactor;
      double minX = boundBox.min().x();
      double minY = boundBox.min().y();
      double minZ = boundBox.min().z();
      double maxX = boundBox.max().x();
      double maxY = boundBox.max().y();
      double maxZ = boundBox.max().z();
      fwrite(&minX, sizeof(double), 1, fp);
      fwrite(&minY, sizeof(double), 1, fp);
      fwrite(&minZ, sizeof(double), 1, fp);
      fwrite(&maxX, sizeof(double), 1, fp);
      fwrite(&maxY, sizeof(double), 1, fp);
      fwrite(&maxZ, sizeof(double), 1, fp);

      writeMSH4Physicals(fp, *it, binary);

      fwrite(&edgesSize, sizeof(unsigned long), 1, fp);

      std::vector<int> tags, signs;
      for(std::list<GEdge*>::iterator ite = edges.begin(); ite != edges.end(); ite++)
        tags.push_back((*ite)->tag());
      for(std::list<int>::iterator ite = ori.begin(); ite != ori.end(); ite++)
        signs.push_back(*ite);

      if(tags.size() == signs.size()){
        for(unsigned int i = 0; i < tags.size(); i++)
          tags[i] *= (signs[i] > 0 ? 1 : -1);
      }

      for(unsigned int i = 0; i < tags.size(); i++){
        int brepTag = tags[i];
        fwrite(&brepTag, sizeof(int), 1, fp);
      }
    }

    for(GModel::riter it = regions.begin(); it != regions.end(); ++it){
      std::list<GFace*> faces = (*it)->faces();
      std::list<int> ori = (*it)->faceOrientations();
      unsigned long facesSize = faces.size();

      int entityTag = (*it)->tag();
      fwrite(&entityTag, sizeof(int), 1, fp);
      if(partition){
        int parentEntityTag = 0;
        if(static_cast<partitionRegion*>(*it)->getParentEntity()){
          parentEntityTag = static_cast<partitionRegion*>(*it)->getParentEntity()->tag();
        }
        fwrite(&parentEntityTag, sizeof(int), 1, fp);
      }

      SBoundingBox3d boundBox = (*it)->bounds();
      double minX = boundBox.min().x();
      double minY = boundBox.min().y();
      double minZ = boundBox.min().z();
      double maxX = boundBox.max().x();
      double maxY = boundBox.max().y();
      double maxZ = boundBox.max().z();
      fwrite(&minX, sizeof(double), 1, fp);
      fwrite(&minY, sizeof(double), 1, fp);
      fwrite(&minZ, sizeof(double), 1, fp);
      fwrite(&maxX, sizeof(double), 1, fp);
      fwrite(&maxY, sizeof(double), 1, fp);
      fwrite(&maxZ, sizeof(double), 1, fp);

      writeMSH4Physicals(fp, *it, binary);

      fwrite(&facesSize, sizeof(unsigned long), 1, fp);

      std::vector<int> tags, signs;
      for(std::list<GFace*>::iterator itf = faces.begin(); itf != faces.end(); itf++)
        tags.push_back((*itf)->tag());
      for(std::list<int>::iterator itf = ori.begin(); itf != ori.end(); itf++)
        signs.push_back(*itf);

      if(tags.size() == signs.size()){
        for(unsigned int i = 0; i < tags.size(); i++)
          tags[i] *= (signs[i] > 0 ? 1 : -1);
      }

      for(unsigned int i = 0; i < tags.size(); i++){
        int brepTag = tags[i];
        fwrite(&brepTag, sizeof(int), 1, fp);
      }
    }

    fprintf(fp, "\n");
  }
  else{
    if(partition) fprintf(fp, "%d ", model->getNumPartitions());
    fprintf(fp, "%lu %lu %lu %lu\n",
            vertices.size(), edges.size(), faces.size(), regions.size());

    for(GModel::viter it = vertices.begin(); it != vertices.end(); ++it){
      fprintf(fp, "%d ", (*it)->tag());
      if(partition){
        if(static_cast<partitionVertex*>(*it)->getParentEntity()){
          fprintf(fp, "%d ", static_cast<partitionVertex*>(*it)->getParentEntity()->tag());
        }
        else{
          fprintf(fp, "0 ");
        }
      }
      SBoundingBox3d boundBox = (*it)->bounds();
      boundBox *= scalingFactor;
      fprintf(fp, "%.16g %.16g %.16g ",
              boundBox.min().x(), boundBox.min().y(), boundBox.min().z());
      fprintf(fp, "%.16g %.16g %.16g ",
              boundBox.max().x(), boundBox.max().y(), boundBox.max().z());
      writeMSH4Physicals(fp, *it, binary);
      fprintf(fp, "\n");
    }

    for(GModel::eiter it = edges.begin(); it != edges.end(); ++it){
      std::list<GVertex*> vertices;
      std::vector<int> ori;
      if((*it)->getBeginVertex()){
        vertices.push_back((*it)->getBeginVertex());
        ori.push_back(1);
      }
      if((*it)->getEndVertex()) {// I use the convention that the end vertex is negative
        vertices.push_back((*it)->getEndVertex());
        ori.push_back(-1);
      }

      fprintf(fp, "%d ", (*it)->tag());
      if(partition){
        if(static_cast<partitionEdge*>(*it)->getParentEntity()){
          fprintf(fp, "%d ", static_cast<partitionEdge*>(*it)->getParentEntity()->tag());
        }
        else{
          fprintf(fp, "0 ");
        }
      }

      SBoundingBox3d boundBox = (*it)->bounds();
      boundBox *= scalingFactor;
      fprintf(fp, "%.16g %.16g %.16g ",
              boundBox.min().x(), boundBox.min().y(), boundBox.min().z());
      fprintf(fp, "%.16g %.16g %.16g ",
              boundBox.max().x(), boundBox.max().y(), boundBox.max().z());

      writeMSH4Physicals(fp, *it, binary);

      fprintf(fp, "%lu ", vertices.size());
      int oriI = 0;
      for(std::list<GVertex*>::iterator itv = vertices.begin(); itv != vertices.end(); itv++){
        fprintf(fp, "%d ", ori[oriI]*(*itv)->tag());
        oriI++;
      }

      fprintf(fp, "\n");
    }

    for(GModel::fiter it = faces.begin(); it != faces.end(); ++it){
      std::list<GEdge*> edges = (*it)->edges();
      std::list<int> ori = (*it)->edgeOrientations();

      fprintf(fp, "%d ", (*it)->tag());
      if(partition){
        if(static_cast<partitionFace*>(*it)->getParentEntity()){
          fprintf(fp, "%d ", static_cast<partitionFace*>(*it)->getParentEntity()->tag());
        }
        else{
          fprintf(fp, "0 ");
        }
      }

      SBoundingBox3d boundBox = (*it)->bounds();
      boundBox *= scalingFactor;
      fprintf(fp, "%.16g %.16g %.16g ",
              boundBox.min().x(), boundBox.min().y(), boundBox.min().z());
      fprintf(fp, "%.16g %.16g %.16g ",
              boundBox.max().x(), boundBox.max().y(), boundBox.max().z());

      writeMSH4Physicals(fp, *it, binary);

      fprintf(fp, "%lu ", edges.size());

      std::vector<int> tags, signs;
      for(std::list<GEdge*>::iterator ite = edges.begin(); ite != edges.end(); ite++)
        tags.push_back((*ite)->tag());
      for(std::list<int>::iterator ite = ori.begin(); ite != ori.end(); ite++)
        signs.push_back(*ite);

      if(tags.size() == signs.size()){
        for(unsigned int i = 0; i < tags.size(); i++)
          tags[i] *= (signs[i] > 0 ? 1 : -1);
      }

      for(unsigned int i = 0; i < tags.size(); i++)
        fprintf(fp, "%d ", tags[i]);

      fprintf(fp, "\n");
    }

    for(GModel::riter it = regions.begin(); it != regions.end(); ++it){
      std::list<GFace*> faces = (*it)->faces();
      std::list<int> ori = (*it)->faceOrientations();

      fprintf(fp, "%d ", (*it)->tag());
      if(partition){
        if(static_cast<partitionRegion*>(*it)->getParentEntity()){
          fprintf(fp, "%d ", static_cast<partitionRegion*>(*it)->getParentEntity()->tag());
        }
        else{
          fprintf(fp, "0 ");
        }
      }

      SBoundingBox3d boundBox = (*it)->bounds();
      boundBox *= scalingFactor;
      fprintf(fp, "%.16g %.16g %.16g ",
              boundBox.min().x(), boundBox.min().y(), boundBox.min().z());
      fprintf(fp, "%.16g %.16g %.16g ",
              boundBox.max().x(), boundBox.max().y(), boundBox.max().z());

      writeMSH4Physicals(fp, *it, binary);

      fprintf(fp, "%lu ", faces.size());

      std::vector<int> tags, signs;
      for(std::list<GFace*>::iterator itf = faces.begin(); itf != faces.end(); itf++)
        tags.push_back((*itf)->tag());
      for(std::list<int>::iterator itf = ori.begin(); itf != ori.end(); itf++)
        signs.push_back(*itf);

      if(tags.size() == signs.size()){
        for(unsigned int i = 0; i < tags.size(); i++)
          tags[i] *= (signs[i] > 0 ? 1 : -1);
      }

      for(unsigned int i = 0; i < tags.size(); i++)
        fprintf(fp, "%d ", tags[i]);

      fprintf(fp, "\n");
    }
  }
}

static unsigned long getAdditionalEntities(std::set<GRegion*, GEntityLessThan> &regions,
                                           std::set<GFace*, GEntityLessThan> &faces,
                                           std::set<GEdge*, GEntityLessThan> &edges,
                                           std::set<GVertex*, GEntityLessThan> &vertices)
{
  unsigned long numVertices = 0;

  for(std::set<GVertex*, GEntityLessThan>::iterator it = vertices.begin();
      it != vertices.end(); ++it){
    numVertices += (*it)->getNumMeshVertices();
  }

  for(std::set<GEdge*, GEntityLessThan>::iterator it = edges.begin();
      it != edges.end(); ++it){
    numVertices += (*it)->getNumMeshVertices();
    for(unsigned int i = 0; i < (*it)->getNumMeshElements(); i++){
      for(int j = 0; j < (*it)->getMeshElement(i)->getNumVertices(); j++){
        if((*it)->getMeshElement(i)->getVertex(j)->onWhat() != (*it)){
          GEntity *entity = (*it)->getMeshElement(i)->getVertex(j)->onWhat();

          switch (entity->dim()){
            case 0:
              if(vertices.find(static_cast<GVertex*>(entity)) == vertices.end()){
                vertices.insert(static_cast<GVertex*>(entity));
                numVertices += entity->getNumMeshVertices();
              }
              break;
            case 1:
              if(edges.find(static_cast<GEdge*>(entity)) == edges.end()){
                edges.insert(static_cast<GEdge*>(entity));
                numVertices += entity->getNumMeshVertices();
              }
              break;
            case 2:
              if(faces.find(static_cast<GFace*>(entity)) == faces.end()){
                faces.insert(static_cast<GFace*>(entity));
                numVertices += entity->getNumMeshVertices();
              }
              break;
            case 3:
              if(regions.find(static_cast<GRegion*>(entity)) == regions.end()){
                regions.insert(static_cast<GRegion*>(entity));
                numVertices += entity->getNumMeshVertices();
              }
              break;
            default:
              break;
          }
        }
      }
    }
  }

  for(std::set<GFace*, GEntityLessThan>::iterator it = faces.begin();
      it != faces.end(); ++it){
    numVertices += (*it)->getNumMeshVertices();
    for(unsigned int i = 0; i < (*it)->getNumMeshElements(); i++){
      for(int j = 0; j < (*it)->getMeshElement(i)->getNumVertices(); j++){
        if((*it)->getMeshElement(i)->getVertex(j)->onWhat() != (*it)){
          GEntity *entity = (*it)->getMeshElement(i)->getVertex(j)->onWhat();

          switch (entity->dim()){
            case 0:
              if(vertices.find(static_cast<GVertex*>(entity)) == vertices.end()){
                vertices.insert(static_cast<GVertex*>(entity));
                numVertices += entity->getNumMeshVertices();
              }
              break;
            case 1:
              if(edges.find(static_cast<GEdge*>(entity)) == edges.end()){
                edges.insert(static_cast<GEdge*>(entity));
                numVertices += entity->getNumMeshVertices();
              }
              break;
            case 2:
              if(faces.find(static_cast<GFace*>(entity)) == faces.end()){
                faces.insert(static_cast<GFace*>(entity));
                numVertices += entity->getNumMeshVertices();
              }
              break;
            case 3:
              if(regions.find(static_cast<GRegion*>(entity)) == regions.end()){
                regions.insert(static_cast<GRegion*>(entity));
                numVertices += entity->getNumMeshVertices();
              }
              break;
            default:
              break;
          }
        }
      }
    }
  }

  for(std::set<GRegion*, GEntityLessThan>::iterator it = regions.begin();
      it != regions.end(); ++it){
    numVertices += (*it)->getNumMeshVertices();
    for(unsigned int i = 0; i < (*it)->getNumMeshElements(); i++){
      for(int j = 0; j < (*it)->getMeshElement(i)->getNumVertices(); j++){
        if((*it)->getMeshElement(i)->getVertex(j)->onWhat() != (*it)){
          GEntity *entity = (*it)->getMeshElement(i)->getVertex(j)->onWhat();

          switch (entity->dim()){
            case 0:
              if(vertices.find(static_cast<GVertex*>(entity)) == vertices.end()){
                vertices.insert(static_cast<GVertex*>(entity));
                numVertices += entity->getNumMeshVertices();
              }
              break;
            case 1:
              if(edges.find(static_cast<GEdge*>(entity)) == edges.end()){
                edges.insert(static_cast<GEdge*>(entity));
                numVertices += entity->getNumMeshVertices();
              }
              break;
            case 2:
              if(faces.find(static_cast<GFace*>(entity)) == faces.end()){
                faces.insert(static_cast<GFace*>(entity));
                numVertices += entity->getNumMeshVertices();
              }
              break;
            case 3:
              if(regions.find(static_cast<GRegion*>(entity)) == regions.end()){
                regions.insert(static_cast<GRegion*>(entity));
                numVertices += entity->getNumMeshVertices();
              }
              break;
            default:
              break;
          }
        }
      }
    }
  }

  return numVertices;
}

static void writeMSH4Nodes(GModel *const model, FILE *fp, bool partitioned, bool binary,
                           bool saveParametric, double scalingFactor, bool saveAll)
{
  std::set<GRegion*, GEntityLessThan> regions;
  std::set<GFace*, GEntityLessThan> faces;
  std::set<GEdge*, GEntityLessThan> edges;
  std::set<GVertex*, GEntityLessThan> vertices;

  if(partitioned){
    for(GModel::viter it = model->firstVertex(); it != model->lastVertex(); ++it)
      if((*it)->geomType() == GEntity::PartitionVertex)
        vertices.insert(*it);
    for(GModel::eiter it = model->firstEdge(); it != model->lastEdge(); ++it)
      if((*it)->geomType() == GEntity::PartitionCurve)
        edges.insert(*it);
    for(GModel::fiter it = model->firstFace(); it != model->lastFace(); ++it)
      if((*it)->geomType() == GEntity::PartitionSurface)
        faces.insert(*it);
    for(GModel::riter it = model->firstRegion(); it != model->lastRegion(); ++it)
      if((*it)->geomType() == GEntity::PartitionVolume)
        regions.insert(*it);
  }
  else{
    for(GModel::viter it = model->firstVertex(); it != model->lastVertex(); ++it)
      if((*it)->geomType() != GEntity::PartitionVertex &&
         (saveAll || (!saveAll && (*it)->getPhysicalEntities().size() != 0)))
        vertices.insert(*it);
    for(GModel::eiter it = model->firstEdge(); it != model->lastEdge(); ++it)
      if((*it)->geomType() != GEntity::PartitionCurve &&
         (saveAll || (!saveAll && (*it)->getPhysicalEntities().size() != 0)))
        edges.insert(*it);
    for(GModel::fiter it = model->firstFace(); it != model->lastFace(); ++it)
      if((*it)->geomType() != GEntity::PartitionSurface &&
         (saveAll || (!saveAll && (*it)->getPhysicalEntities().size() != 0)))
        faces.insert(*it);
    for(GModel::riter it = model->firstRegion(); it != model->lastRegion(); ++it)
      if((*it)->geomType() != GEntity::PartitionVolume &&
         (saveAll || (!saveAll && (*it)->getPhysicalEntities().size() != 0)))
        regions.insert(*it);
  }

  unsigned long numVertices = model->getNumMeshVertices();
  if(!saveAll && !partitioned){
    numVertices = getAdditionalEntities(regions, faces, edges, vertices);
  }

  if(binary){
    unsigned long numSection = vertices.size() + edges.size() +
      faces.size() + regions.size();
    fwrite(&numSection, sizeof(unsigned long), 1, fp);
    fwrite(&numVertices, sizeof(unsigned long), 1, fp);
  }
  else{
    fprintf(fp, "%lu %lu\n", vertices.size() + edges.size() + faces.size() +
            regions.size(), numVertices);
  }

  for(GModel::viter it = vertices.begin(); it != vertices.end(); ++it){
    if(binary){
      int entityTag = (*it)->tag();
      int entityDim = (*it)->dim();
      unsigned long numVerts = (*it)->getNumMeshVertices();
      fwrite(&entityTag, sizeof(int), 1, fp);
      fwrite(&entityDim, sizeof(int), 1, fp);
      fwrite(&numVerts, sizeof(unsigned long), 1, fp);
    }
    else{
      fprintf(fp, "%d %d %lu\n", (*it)->tag(), (*it)->dim(),
              (unsigned long)(*it)->getNumMeshVertices());
    }

    for(unsigned int i = 0; i < (*it)->getNumMeshVertices(); i++)
      (*it)->getMeshVertex(i)->writeMSH4(fp, binary, saveParametric, scalingFactor);
  }

  for(GModel::eiter it = edges.begin(); it != edges.end(); ++it){
    if(binary){
      int entityTag = (*it)->tag();
      int entityDim = (*it)->dim();
      unsigned long numVerts = (*it)->getNumMeshVertices();
      fwrite(&entityTag, sizeof(int), 1, fp);
      fwrite(&entityDim, sizeof(int), 1, fp);
      fwrite(&numVerts, sizeof(unsigned long), 1, fp);
    }
    else{
      fprintf(fp, "%d %d %lu\n", (*it)->tag(), (*it)->dim(),
              (unsigned long)(*it)->getNumMeshVertices());
    }

    for(unsigned int i = 0; i < (*it)->getNumMeshVertices(); i++)
      (*it)->getMeshVertex(i)->writeMSH4(fp, binary, saveParametric, scalingFactor);
  }

  for(GModel::fiter it = faces.begin(); it != faces.end(); ++it){
    if(binary){
      int entityTag = (*it)->tag();
      int entityDim = (*it)->dim();
      unsigned long numVerts = (*it)->getNumMeshVertices();
      fwrite(&entityTag, sizeof(int), 1, fp);
      fwrite(&entityDim, sizeof(int), 1, fp);
      fwrite(&numVerts, sizeof(unsigned long), 1, fp);
    }
    else{
      fprintf(fp, "%d %d %lu\n", (*it)->tag(), (*it)->dim(),
              (unsigned long)(*it)->getNumMeshVertices());
    }

    for(unsigned int i = 0; i < (*it)->getNumMeshVertices(); i++)
      (*it)->getMeshVertex(i)->writeMSH4(fp, binary, saveParametric, scalingFactor);
  }

  for(GModel::riter it = regions.begin(); it != regions.end(); ++it){
    if(binary){
      int entityTag = (*it)->tag();
      int entityDim = (*it)->dim();
      unsigned long numVerts = (*it)->getNumMeshVertices();
      fwrite(&entityTag, sizeof(int), 1, fp);
      fwrite(&entityDim, sizeof(int), 1, fp);
      fwrite(&numVerts, sizeof(unsigned long), 1, fp);
    }
    else{
      fprintf(fp, "%d %d %lu\n", (*it)->tag(), (*it)->dim(),
              (unsigned long)(*it)->getNumMeshVertices());
    }

    for(unsigned int i = 0; i < (*it)->getNumMeshVertices(); i++)
      (*it)->getMeshVertex(i)->writeMSH4(fp, binary, saveParametric, scalingFactor);
  }

  if(binary) fprintf(fp, "\n");
}

static void writeMSH4Elements(GModel *const model, FILE *fp, bool partitioned,
                              bool binary, bool saveAll)
{
  std::set<GRegion*, GEntityLessThan> regions;
  std::set<GFace*, GEntityLessThan> faces;
  std::set<GEdge*, GEntityLessThan> edges;
  std::set<GVertex*, GEntityLessThan> vertices;

  if(partitioned){
    for(GModel::viter it = model->firstVertex(); it != model->lastVertex(); ++it)
      if((*it)->geomType() == GEntity::PartitionVertex)
        vertices.insert(*it);
    for(GModel::eiter it = model->firstEdge(); it != model->lastEdge(); ++it)
      if((*it)->geomType() == GEntity::PartitionCurve)
        edges.insert(*it);
    for(GModel::fiter it = model->firstFace(); it != model->lastFace(); ++it)
      if((*it)->geomType() == GEntity::PartitionSurface)
        faces.insert(*it);
    for(GModel::riter it = model->firstRegion(); it != model->lastRegion(); ++it)
      if((*it)->geomType() == GEntity::PartitionVolume)
        regions.insert(*it);
  }
  else{
    for(GModel::viter it = model->firstVertex(); it != model->lastVertex(); ++it)
      if((*it)->geomType() != GEntity::PartitionVertex)
        vertices.insert(*it);
    for(GModel::eiter it = model->firstEdge(); it != model->lastEdge(); ++it)
      if((*it)->geomType() != GEntity::PartitionCurve)
        edges.insert(*it);
    for(GModel::fiter it = model->firstFace(); it != model->lastFace(); ++it)
      if((*it)->geomType() != GEntity::PartitionSurface)
        faces.insert(*it);
    for(GModel::riter it = model->firstRegion(); it != model->lastRegion(); ++it)
      if((*it)->geomType() != GEntity::PartitionVolume)
        regions.insert(*it);
  }

  std::map<std::pair<int, GEntity*>, std::vector<MElement*> > elementsByDegree;
  unsigned long numElements= 0;

  for(GModel::viter it = vertices.begin(); it != vertices.end(); ++it){
    if(!saveAll && (*it)->physicals.size() == 0) continue;

    numElements += (*it)->points.size();
    for(unsigned int i = 0; i < (*it)->points.size(); i++)
      elementsByDegree[ std::pair<int, GEntity*>
                        ((*it)->points[i]->getTypeForMSH(), *it) ]
        .push_back((*it)->points[i]);
  }

  for(GModel::eiter it = edges.begin(); it != edges.end(); ++it){
    if(!saveAll && (*it)->physicals.size() == 0) continue;

    numElements += (*it)->lines.size();
    for(unsigned int i = 0; i < (*it)->lines.size(); i++)
      elementsByDegree[ std::pair<int, GEntity*>
                        ((*it)->lines[i]->getTypeForMSH(), *it) ]
        .push_back((*it)->lines[i]);
  }

  for(GModel::fiter it = faces.begin(); it != faces.end(); ++it){
    if(!saveAll && (*it)->physicals.size() == 0) continue;

    numElements += (*it)->triangles.size();
    for(unsigned int i = 0; i < (*it)->triangles.size(); i++)
      elementsByDegree[ std::pair<int, GEntity*>
                        ((*it)->triangles[i]->getTypeForMSH(), *it) ]
        .push_back((*it)->triangles[i]);

    numElements += (*it)->quadrangles.size();
    for(unsigned int i = 0; i < (*it)->quadrangles.size(); i++)
      elementsByDegree[ std::pair<int, GEntity*>
                        ((*it)->quadrangles[i]->getTypeForMSH(), *it) ]
        .push_back((*it)->quadrangles[i]);
  }

  for(GModel::riter it = regions.begin(); it != regions.end(); ++it){
    if(!saveAll && (*it)->physicals.size() == 0) continue;

    numElements += (*it)->tetrahedra.size();
    for(unsigned int i = 0; i < (*it)->tetrahedra.size(); i++)
      elementsByDegree[ std::pair<int, GEntity*>
                        ((*it)->tetrahedra[i]->getTypeForMSH(), *it) ]
        .push_back((*it)->tetrahedra[i]);

    numElements += (*it)->hexahedra.size();
    for(unsigned int i = 0; i < (*it)->hexahedra.size(); i++)
      elementsByDegree[ std::pair<int, GEntity*>
                        ((*it)->hexahedra[i]->getTypeForMSH(), *it) ]
        .push_back((*it)->hexahedra[i]);

    numElements += (*it)->prisms.size();
    for(unsigned int i = 0; i < (*it)->prisms.size(); i++)
      elementsByDegree[ std::pair<int, GEntity*>
                        ((*it)->prisms[i]->getTypeForMSH(), *it) ]
        .push_back((*it)->prisms[i]);

    numElements += (*it)->pyramids.size();
    for(unsigned int i = 0; i < (*it)->pyramids.size(); i++)
      elementsByDegree[ std::pair<int, GEntity*>
                        ((*it)->pyramids[i]->getTypeForMSH(), *it) ]
        .push_back((*it)->pyramids[i]);

    numElements += (*it)->trihedra.size();
    for(unsigned int i = 0; i < (*it)->trihedra.size(); i++)
      elementsByDegree[ std::pair<int, GEntity*>
                        ((*it)->trihedra[i]->getTypeForMSH(), *it) ]
        .push_back((*it)->trihedra[i]);
  }

  if(binary){
    unsigned long numSection = elementsByDegree.size();
    fwrite(&numSection, sizeof(unsigned long), 1, fp);
    fwrite(&numElements, sizeof(unsigned long), 1, fp);
  }
  else{
    fprintf(fp, "%lu %lu\n", elementsByDegree.size(), numElements);
  }

  for(std::map<std::pair<int, GEntity*>, std::vector<MElement*> >::iterator it =
        elementsByDegree.begin(); it != elementsByDegree.end(); ++it){
    if(binary){
      int entityTag = it->first.second->tag();
      int entityDim = it->first.second->dim();
      int elmType = it->first.first;
      unsigned long numElm = it->second.size();
      fwrite(&entityTag, sizeof(int), 1, fp);
      fwrite(&entityDim, sizeof(int), 1, fp);
      fwrite(&elmType, sizeof(int), 1, fp);
      fwrite(&numElm, sizeof(unsigned long), 1, fp);
    }
    else{
      fprintf(fp, "%d %d %d %lu\n", it->first.second->tag(), it->first.second->dim(),
              it->first.first, it->second.size());
    }

    if(binary){
      const int nbrVertices = MElement::getInfoMSH(it->first.first);
      int indexElement = 0;
      int *elementData = new int[it->second.size()*(nbrVertices+1)];
      for(unsigned int i = 0; i < it->second.size()*(nbrVertices+1); i+=(nbrVertices+1)){
        elementData[i] = it->second[indexElement]->getNum();
        for(int j = 0; j < nbrVertices; j++){
          elementData[i+1+j] = it->second[indexElement]->getVertex(j)->getNum();
        }
        indexElement++;
      }
      fwrite(elementData, sizeof(int), it->second.size()*(nbrVertices+1), fp);
      delete[] elementData;
    }
    else{
      for(unsigned int i = 0; i < it->second.size(); i++){
        it->second[i]->writeMSH4(fp, binary);
      }
    }
  }

  if(binary) fprintf(fp, "\n");
}

static void writeMSH4PeriodicNodes(GModel *const model, FILE *fp, bool partitioned,
                                   bool binary)
{
  int count = 0;
  std::vector<GEntity*> entities;
  model->getEntities(entities);
  for(unsigned int i = 0; i < entities.size(); i++)
    if(entities[i]->meshMaster() != entities[i]) count++;

  if(!count) return;

  fprintf(fp, "$Periodic\n");

  if(binary){
    fwrite(&count, sizeof(int), 1, fp);
  }
  else{
    fprintf(fp, "%d\n", count);
  }

  for(unsigned int i = 0; i < entities.size(); i++){
    GEntity *g_slave = entities[i];
    GEntity *g_master = g_slave->meshMaster();

    if(g_slave != g_master){
      if(binary){
        int gSlaveDim = g_slave->dim();
        int gSlaveTag = g_slave->tag();
        int gMasterTag = g_master->tag();
        fwrite(&gSlaveDim, sizeof(int), 1, fp);
        fwrite(&gSlaveTag, sizeof(int), 1, fp);
        fwrite(&gMasterTag, sizeof(int), 1, fp);

        if(g_slave->affineTransform.size() == 16){
          long AffineTag = -1;
          fwrite(&AffineTag, sizeof(long), 1, fp);
          for(int j = 0; j < 16; j++){
            double value = g_slave->affineTransform[i];
            fwrite(&value, sizeof(double), 1, fp);
          }
        }

        long corrVertSize = g_slave->correspondingVertices.size();
        fwrite(&corrVertSize, sizeof(long), 1, fp);

        for (std::map<MVertex*,MVertex*>::iterator it =
               g_slave->correspondingVertices.begin();
             it != g_slave->correspondingVertices.end(); ++it){
          int numFirst = it->first->getNum();
          int numSecond = it->second->getNum();
          fwrite(&numFirst, sizeof(int), 1, fp);
          fwrite(&numSecond, sizeof(int), 1, fp);
        }
      }
      else{
        fprintf(fp, "%d %d %d\n", g_slave->dim(), g_slave->tag(), g_master->tag());

        if(g_slave->affineTransform.size() == 16){
          fprintf(fp, "Affine");
          for(int j = 0; j < 16; j++) fprintf(fp, " %.16g", g_slave->affineTransform[i]);
          fprintf(fp, "\n");
        }

        fprintf(fp, "%lu\n", g_slave->correspondingVertices.size());

        for (std::map<MVertex*,MVertex*>::iterator it =
               g_slave->correspondingVertices.begin();
             it != g_slave->correspondingVertices.end(); ++it){
          fprintf(fp, "%d %d\n", it->first->getNum(), it->second->getNum());
        }
      }
    }
  }

  if(binary) fprintf(fp, "\n");
  fprintf(fp, "$EndPeriodic\n");
}

int GModel::_writeMSH4(const std::string &name, double version, bool binary,
                       bool saveAll, bool saveParametric, double scalingFactor)
{
  FILE *fp = Fopen(name.c_str(), binary ? "wb" : "w");

  if(!fp){
    Msg::Error("Unable to open file '%s'", name.c_str());
    return 0;
  }

  // if there are no physicals we save all the elements
  if(noPhysicalGroups()) saveAll = true;

  // header
  fprintf(fp, "$MeshFormat\n");
  fprintf(fp, "%g %d %lu\n", version, (binary ? 1 : 0), sizeof(double));
  if(binary){
    int one = 1;
    fwrite(&one, sizeof(int), 1, fp);//swapping byte
    fprintf(fp, "\n");
  }
  fprintf(fp, "$EndMeshFormat\n");

  // physicals
  if(numPhysicalNames() > 0){
    fprintf(fp, "$PhysicalNames\n");
    fprintf(fp, "%d\n", numPhysicalNames());
    for(piter it = firstPhysicalName(); it != lastPhysicalName(); ++it){
      std::string name = it->second;
      if(name.size() > 128) name.resize(128);
      fprintf(fp, "%d %d \"%s\"\n", it->first.first, it->first.second, name.c_str());
    }
    fprintf(fp, "$EndPhysicalNames\n");
  }

  // entities
  fprintf(fp, "$Entities\n");
  writeMSH4Entities(this, fp, false, binary, scalingFactor);
  fprintf(fp, "$EndEntities\n");

  // partitioned entities
  if(getNumPartitions() > 0){
    fprintf(fp, "$PartitionedEntities\n");
    writeMSH4Entities(this, fp, true, binary, scalingFactor);
    fprintf(fp, "$EndPartitionedEntities\n");
  }

  // nodes
  if(saveParametric){
    fprintf(fp, "$ParametricNodes\n");
    writeMSH4Nodes(this, fp, getNumPartitions() == 0 ? false : true, binary,
                   saveParametric, scalingFactor, saveAll);
    fprintf(fp, "$EndParametricNodes\n");
  }
  else{
    fprintf(fp, "$Nodes\n");
    writeMSH4Nodes(this, fp, getNumPartitions() == 0 ? false : true, binary,
                   saveParametric, scalingFactor, saveAll);
    fprintf(fp, "$EndNodes\n");
  }

  // elements
  fprintf(fp, "$Elements\n");
  writeMSH4Elements(this, fp, getNumPartitions() == 0 ? false : true, binary, saveAll);
  fprintf(fp, "$EndElements\n");

  // periodic
  writeMSH4PeriodicNodes(this, fp, getNumPartitions() == 0 ? false : true, binary);

  fclose(fp);

  return 1;
}

int GModel::_writePartitionedMSH4(const std::string &baseName, double version,
                                  bool binary, bool saveAll, bool saveParametric,
                                  double scalingFactor)
{
  for(unsigned int i = 0; i < getNumPartitions(); i++){
    // Create a temporitary model
    GModel *tmp = new GModel();
    for(GModel::piter it = this->firstPhysicalName(); it != this->lastPhysicalName(); ++it){
      tmp->setPhysicalName(it->second, it->first.first, it->first.second);
    }

    for(unsigned int j = 0; j < getNumPartitions(); j++){
      tmp->meshPartitions.insert(j);
    }

    std::vector<GEntity*> entities;
    getEntities(entities);
    for(unsigned int j = 0; j < entities.size(); j++){
      switch(entities[j]->geomType()){
      case GEntity::PartitionVolume:
        {
          partitionRegion *pr = static_cast<partitionRegion*>(entities[j]);
          if(std::find(pr->getPartitions().begin(), pr->getPartitions().end(), i) !=
             pr->getPartitions().end()){
            tmp->add(pr);
            if(pr->getParentEntity()){
              tmp->add(pr->getParentEntity());
            }
          }
        }
        break;
      case GEntity::PartitionSurface:
        {
          partitionFace *pf = static_cast<partitionFace*>(entities[j]);
          if(std::find(pf->getPartitions().begin(), pf->getPartitions().end(), i) !=
             pf->getPartitions().end()){
            tmp->add(pf);
            if(pf->getParentEntity()){
              tmp->add(pf->getParentEntity());
            }
          }
        }
        break;
      case GEntity::PartitionCurve:
        {
          partitionEdge *pe = static_cast<partitionEdge*>(entities[j]);
          if(std::find(pe->getPartitions().begin(), pe->getPartitions().end(), i) !=
             pe->getPartitions().end()){
            tmp->add(pe);
            if(pe->getParentEntity()){
              tmp->add(pe->getParentEntity());
            }
          }
        }
        break;
      case GEntity::PartitionVertex:
        {
          partitionVertex *pv = static_cast<partitionVertex*>(entities[j]);
          if(std::find(pv->getPartitions().begin(), pv->getPartitions().end(), i) !=
             pv->getPartitions().end()){
            tmp->add(pv);
            if(pv->getParentEntity()){
              tmp->add(pv->getParentEntity());
            }
          }
        }
        break;
      default:
        break;
      }
    }
    tmp->_associateEntityWithMeshVertices();
    tmp->pruneMeshVertexAssociations();
    std::ostringstream sstream;
    sstream << baseName << "_" << i << ".msh";

    tmp->_writeMSH4(sstream.str(), version, binary, saveAll, saveParametric, scalingFactor);
    tmp->remove();
    delete tmp;
  }

  return 1;
}

int getTag(GModel* model, GEntity* entity)
{
  std::vector<int> tags = entity->getPhysicalEntities();
  for(unsigned int i = 0; i < tags.size(); i++){
    std::string name = model->getPhysicalName(entity->dim(),tags[i]);

    if(name[0] != '_') continue;
    else return tags[i];
  }

  return -1;
}

std::string getSubstr(GModel* model, GEntity* entity)
{
  std::string substr;
  std::vector<int> tags = entity->getPhysicalEntities();
  for(unsigned int i = 0; i < tags.size(); i++){
    std::string name = model->getPhysicalName(entity->dim(),tags[i]);

    std::string str;
    if(name[0] == '_'){
      for(unsigned int j = 0; j < name.length(); j++){
        if(name[j] == '{'){
          substr = str;
          break;
        }
        else if(name[j] == ' ') continue;
        else if(name[j] == '}') break;
        else{
          str += name[j];
        }
      }
    }

  }

  return substr;
}

int GModel::writePartitionedTopology(std::string &name)
{
  std::multimap<unsigned int, GEntity*> omega;
  std::multimap<unsigned int, GEntity*> sigma;
  std::multimap<unsigned int, GEntity*> tau;
  std::multimap<unsigned int, GEntity*> upsilon;
  std::multimap<unsigned int, GEntity*> omicron;
  std::multimap<unsigned int, GEntity*> omicronSigma;
  std::multimap<unsigned int, GEntity*> omicronTau;

  std::vector<GEntity*> entities;
  getEntities(entities);
  for(unsigned int i = 0; i < entities.size(); i++){
    switch(entities[i]->geomType()){
    case GEntity::PartitionVolume:
      {
        std::string substr = getSubstr(this, entities[i]);
        partitionRegion *pr = static_cast<partitionRegion*>(entities[i]);
        if(substr == "_omega"){
          for(unsigned int j = 0; j < pr->numPartitions(); j++){
            omega.insert(std::pair<unsigned int, GEntity*>
                         (pr->getPartition(j), entities[i]));
          }
        }
      }
      break;
    case GEntity::PartitionSurface:
      {
        std::string substr = getSubstr(this, entities[i]);
        partitionFace *pf = static_cast<partitionFace*>(entities[i]);
        if(substr == "_omega"){
          for(unsigned int j = 0; j < pf->numPartitions(); j++){
            omega.insert(std::pair<unsigned int, GEntity*>
                         (pf->getPartition(j), entities[i]));
          }
        }
        else if(substr == "_sigma"){
          for(unsigned int j = 0; j < pf->numPartitions(); j++){
            sigma.insert(std::pair<unsigned int, GEntity*>
                         (pf->getPartition(j), entities[i]));
          }
        }
        else if(substr == "_omicron"){
          for(unsigned int j = 0; j < pf->numPartitions(); j++){
            omicron.insert(std::pair<unsigned int, GEntity*>
                           (pf->getPartition(j), entities[i]));
          }
        }
      }
      break;
    case GEntity::PartitionCurve:
      {
        std::string substr = getSubstr(this, entities[i]);
        partitionEdge *pe = static_cast<partitionEdge*>(entities[i]);
        if(substr == "_omega"){
          for(unsigned int j = 0; j < pe->numPartitions(); j++){
            omega.insert(std::pair<unsigned int, GEntity*>
                         (pe->getPartition(j), entities[i]));
          }
        }
        else if(substr == "_sigma"){
          for(unsigned int j = 0; j < pe->numPartitions(); j++){
            sigma.insert(std::pair<unsigned int, GEntity*>
                         (pe->getPartition(j), entities[i]));
          }
        }
        else if(substr == "_tau"){
          for(unsigned int j = 0; j < pe->numPartitions(); j++){
            tau.insert(std::pair<unsigned int, GEntity*>
                       (pe->getPartition(j), entities[i]));
          }
        }
        else if(substr == "_omicron"){
          for(unsigned int j = 0; j < pe->numPartitions(); j++){
            omicron.insert(std::pair<unsigned int, GEntity*>
                           (pe->getPartition(j), entities[i]));
          }
        }
        else if(substr == "_omicronSigma"){
          for(unsigned int j = 0; j < pe->numPartitions(); j++){
            omicronSigma.insert(std::pair<unsigned int, GEntity*>
                                (pe->getPartition(j), entities[i]));
          }
        }
      }
      break;
    case GEntity::PartitionVertex:
      {
        std::string substr = getSubstr(this, entities[i]);
        partitionVertex *pv = static_cast<partitionVertex*>(entities[i]);
        if(substr == "_omega"){
          for(unsigned int j = 0; j < pv->numPartitions(); j++){
            omega.insert(std::pair<unsigned int, GEntity*>
                         (pv->getPartition(j), entities[i]));
          }
        }
        else if(substr == "_sigma"){
          for(unsigned int j = 0; j < pv->numPartitions(); j++){
            sigma.insert(std::pair<unsigned int, GEntity*>
                         (pv->getPartition(j), entities[i]));
          }
        }
        else if(substr == "_tau"){
          for(unsigned int j = 0; j < pv->numPartitions(); j++){
            tau.insert(std::pair<unsigned int, GEntity*>
                       (pv->getPartition(j), entities[i]));
          }
        }
        else if(substr == "_upsilon"){
          for(unsigned int j = 0; j < pv->numPartitions(); j++){
            upsilon.insert(std::pair<unsigned int, GEntity*>
                           (pv->getPartition(j), entities[i]));
          }
        }
        else if(substr == "_omicron"){
          for(unsigned int j = 0; j < pv->numPartitions(); j++){
            omicron.insert(std::pair<unsigned int, GEntity*>
                           (pv->getPartition(j), entities[i]));
          }
        }
        else if(substr == "_omicronSigma"){
          for(unsigned int j = 0; j < pv->numPartitions(); j++){
            omicronSigma.insert(std::pair<unsigned int, GEntity*>
                                (pv->getPartition(j), entities[i]));
          }
        }
        else if(substr == "_omicronTau"){
          for(unsigned int j = 0; j < pv->numPartitions(); j++){
            omicronTau.insert(std::pair<unsigned int, GEntity*>
                              (pv->getPartition(j), entities[i]));
          }
        }
      }
      break;
    default:
      break;
    }
  }

  const unsigned int npart = getNumPartitions();
  FILE *fp = Fopen(name.c_str(), "w");

  if(!fp) return 1;

  fprintf(fp, "Group{\n");

  //Omega_i
  for(unsigned int i = 0; i < npart; i++){
    std::pair <std::multimap<unsigned int, GEntity*>::iterator,
               std::multimap<unsigned int, GEntity*>::iterator> range;
    range = omega.equal_range(i);

    std::set<int> physicalTags;
    for(std::multimap<unsigned int, GEntity*>::iterator it = range.first;
        it != range.second; ++it){
      physicalTags.insert(getTag(this, it->second));
    }

    fprintf(fp, "\tOmega_%d = Region[{", i);
    for(std::set<int>::iterator it = physicalTags.begin(); it != physicalTags.end(); ++it){
      if(it != physicalTags.begin()) fprintf(fp, ", ");
      fprintf(fp, "%d", *it);
    }
    fprintf(fp, "}];\n");
  }
  fprintf(fp, "\n");

  //Sigma_i
  for(unsigned int i = 0; i < npart; i++){
    std::pair <std::multimap<unsigned int, GEntity*>::iterator,
               std::multimap<unsigned int, GEntity*>::iterator> range;
    range = sigma.equal_range(i);

    std::set<int> physicalTags;
    for(std::multimap<unsigned int, GEntity*>::iterator it = range.first;
        it != range.second; ++it){
      physicalTags.insert(getTag(this, it->second));
    }

    fprintf(fp, "\tSigma_%d = Region[{", i);
    for(std::set<int>::iterator it = physicalTags.begin(); it != physicalTags.end(); ++it){
      if(it != physicalTags.begin()) fprintf(fp, ", ");
      fprintf(fp, "%d", *it);
    }
    fprintf(fp, "}];\n");
  }
  fprintf(fp, "\n");

  //Sigma_i_j
  for(std::multimap<unsigned int, GEntity*>::iterator it1 = sigma.begin();
      it1 != sigma.end(); ++it1){
    for(std::multimap<unsigned int, GEntity*>::iterator it2 = sigma.begin();
        it2 != sigma.end(); ++it2){
      if(it2->second == it1->second && *it2 != *it1){
        fprintf(fp, "\tSigma_%d_%d = Region[{%d}];\n", it1->first, it2->first,
                getTag(this, it1->second));
      }
    }
  }
  fprintf(fp, "\n");

  //Tau_i
  for(unsigned int i = 0; i < npart; i++){
    std::pair <std::multimap<unsigned int, GEntity*>::iterator,
               std::multimap<unsigned int, GEntity*>::iterator> range;
    range = tau.equal_range(i);

    std::set<int> physicalTags;
    for(std::multimap<unsigned int, GEntity*>::iterator it = range.first;
        it != range.second; ++it){
      physicalTags.insert(getTag(this, it->second));
    }

    fprintf(fp, "\tTau_%d = Region[{", i);
    for(std::set<int>::iterator it = physicalTags.begin(); it != physicalTags.end(); ++it){
      if(it != physicalTags.begin()) fprintf(fp, ", ");
      fprintf(fp, "%d", *it);
    }
    fprintf(fp, "}];\n");
  }
  fprintf(fp, "\n");

  //Tau_i_j
  for(std::multimap<unsigned int, GEntity*>::iterator it1 = tau.begin();
      it1 != tau.end(); ++it1){
    for(std::multimap<unsigned int, GEntity*>::iterator it2 = tau.begin();
        it2 != tau.end(); ++it2){
      if(it2->second == it1->second && it2 != it1){
        fprintf(fp, "\tTau_%d_%d = Region[{%d}];\n", it1->first, it2->first,
                getTag(this, it1->second));
      }
    }
  }
  fprintf(fp, "\n");

  //Upsilon_i
  for(unsigned int i = 0; i < npart; i++){
    std::pair <std::multimap<unsigned int, GEntity*>::iterator,
               std::multimap<unsigned int, GEntity*>::iterator> range;
    range = upsilon.equal_range(i);

    std::set<int> physicalTags;
    for(std::multimap<unsigned int, GEntity*>::iterator it = range.first;
        it != range.second; ++it){
      physicalTags.insert(getTag(this, it->second));
    }

    fprintf(fp, "\tUpsilon_%d = Region[{", i);
    for(std::set<int>::iterator it = physicalTags.begin(); it != physicalTags.end(); ++it){
      if(it != physicalTags.begin()) fprintf(fp, ", ");
      fprintf(fp, "%d", *it);
    }
    fprintf(fp, "}];\n");
  }
  fprintf(fp, "\n");

  //Upsilon_i_j
  for(std::multimap<unsigned int, GEntity*>::iterator it1 = upsilon.begin();
      it1 != upsilon.end(); ++it1){
    for(std::multimap<unsigned int, GEntity*>::iterator it2 = upsilon.begin();
        it2 != upsilon.end(); ++it2){
      if(it2->second == it1->second && it2 != it1){
        fprintf(fp, "\tUpsilon_%d_%d = Region[{%d}];\n", it1->first, it2->first,
                getTag(this, it1->second));
      }
    }
  }
  fprintf(fp, "\n");

  //Omicron_i
  for(unsigned int i = 0; i < npart; i++){
    std::pair <std::multimap<unsigned int, GEntity*>::iterator,
               std::multimap<unsigned int, GEntity*>::iterator> range;
    range = omicron.equal_range(i);

    std::set<int> physicalTags;
    for(std::multimap<unsigned int, GEntity*>::iterator it = range.first;
        it != range.second; ++it){
      physicalTags.insert(getTag(this, it->second));
    }

    fprintf(fp, "\tOmicron_%d = Region[{", i);
    for(std::set<int>::iterator it = physicalTags.begin(); it != physicalTags.end(); ++it){
      if(it != physicalTags.begin()) fprintf(fp, ", ");
      fprintf(fp, "%d", *it);
    }
    fprintf(fp, "}];\n");
  }
  fprintf(fp, "\n");

  //Omicron_i_j
  for(std::multimap<unsigned int, GEntity*>::iterator it1 = omicron.begin();
      it1 != omicron.end(); ++it1){
    for(std::multimap<unsigned int, GEntity*>::iterator it2 = omicron.begin();
        it2 != omicron.end(); ++it2){
      if(it2->second == it1->second && it2 != it1){
        fprintf(fp, "\tOmicron_%d_%d = Region[{%d}];\n", it1->first, it2->first,
                getTag(this, it1->second));
      }
    }
  }
  fprintf(fp, "\n");

  //OmicronSigma_i
  for(unsigned int i = 0; i < npart; i++){
    std::pair <std::multimap<unsigned int, GEntity*>::iterator,
               std::multimap<unsigned int, GEntity*>::iterator> range;
    range = omicronSigma.equal_range(i);

    std::set<int> physicalTags;
    for(std::multimap<unsigned int, GEntity*>::iterator it = range.first;
        it != range.second; ++it){
      physicalTags.insert(getTag(this, it->second));
    }

    fprintf(fp, "\tOmicronSigma_%d = Region[{", i);
    for(std::set<int>::iterator it = physicalTags.begin(); it != physicalTags.end(); ++it){
      if(it != physicalTags.begin()) fprintf(fp, ", ");
      fprintf(fp, "%d", *it);
    }
    fprintf(fp, "}];\n");
  }
  fprintf(fp, "\n");

  //OmicronSigma_i_j
  for(std::multimap<unsigned int, GEntity*>::iterator it1 = omicronSigma.begin();
      it1 != omicronSigma.end(); ++it1){
    for(std::multimap<unsigned int, GEntity*>::iterator it2 = omicronSigma.begin();
        it2 != omicronSigma.end(); ++it2){
      if(it2->second == it1->second && it2 != it1){
        fprintf(fp, "\tOmicronSigma_%d_%d = Region[{%d}];\n", it1->first, it2->first,
                getTag(this, it1->second));
      }
    }
  }
  fprintf(fp, "\n");

  //OmicronTau_i
  for(unsigned int i = 0; i < npart; i++){
    std::pair <std::multimap<unsigned int, GEntity*>::iterator,
               std::multimap<unsigned int, GEntity*>::iterator> range;
    range = omicronTau.equal_range(i);

    std::set<int> physicalTags;
    for(std::multimap<unsigned int, GEntity*>::iterator it = range.first;
        it != range.second; ++it){
      physicalTags.insert(getTag(this, it->second));
    }

    fprintf(fp, "\tOmicronTau_%d = Region[{", i);
    for(std::set<int>::iterator it = physicalTags.begin(); it != physicalTags.end(); ++it){
      if(it != physicalTags.begin()) fprintf(fp, ", ");
      fprintf(fp, "%d", *it);
    }
    fprintf(fp, "}];\n");
  }
  fprintf(fp, "\n");

  //OmicronTau_i_j
  for(std::multimap<unsigned int, GEntity*>::iterator it1 = omicronTau.begin();
      it1 != omicronTau.end(); ++it1){
    for(std::multimap<unsigned int, GEntity*>::iterator it2 = omicronTau.begin();
        it2 != omicronTau.end(); ++it2){
      if(it2->second == it1->second && it2 != it1){
        fprintf(fp, "\tOmicronTau_%d_%d = Region[{%d}];\n", it1->first, it2->first,
                getTag(this, it1->second));
      }
    }
  }
  fprintf(fp, "\n");

  //D
  fprintf(fp, "\tD() = {");
  for(unsigned int i = 0; i < npart; i++){
    if(i != 0) fprintf(fp, ", ");
    fprintf(fp, "%d", i);
  }
  fprintf(fp, "};\n");

  //D_i
  std::multimap<unsigned int, unsigned int> neighbors;
  for(std::multimap<unsigned int, GEntity*>::iterator it = sigma.begin();
      it != sigma.end(); ++it){
    if(it->second->geomType() == GEntity::PartitionSurface){
      partitionFace *pf = static_cast<partitionFace*>(it->second);
      for(unsigned int i = 0; i < pf->numPartitions(); i++){
        neighbors.insert(std::pair<unsigned int, unsigned int>
                         (it->first, pf->getPartition(i)));
      }
    }
    else if(it->second->geomType() == GEntity::PartitionCurve){
      partitionEdge *pe = static_cast<partitionEdge*>(it->second);
      for(unsigned int i = 0; i < pe->numPartitions(); i++){
        neighbors.insert(std::pair<unsigned int, unsigned int>
                         (it->first, pe->getPartition(i)));
      }
    }
    else if(it->second->geomType() == GEntity::PartitionVertex){
      partitionVertex *pv = static_cast<partitionVertex*>(it->second);
      for(unsigned int i = 0; i < pv->numPartitions(); i++){
        neighbors.insert(std::pair<unsigned int, unsigned int>
                         (it->first, pv->getPartition(i)));
      }
    }
  }

  for(unsigned int i = 0; i < npart; i++){
    std::pair <std::multimap<unsigned int, unsigned int>::iterator,
               std::multimap<unsigned int, unsigned int>::iterator> range;
    range = neighbors.equal_range(i);
    std::vector<unsigned int> writeNeighbors;
    fprintf(fp, "\tD_%d() = {", i);
    int j = 0;
    for(std::multimap<unsigned int, unsigned int>::iterator it = range.first;
        it != range.second; ++it){
      if(std::find(writeNeighbors.begin(), writeNeighbors.end(), it->second) ==
         writeNeighbors.end() && i != it->second){
        if(j != 0) fprintf(fp, ", ");
        fprintf(fp, "%d", it->second);
        writeNeighbors.push_back(it->second);
        j++;
      }
    }
    fprintf(fp, "};\n");
  }

  fprintf(fp, "}\n\n");

  fclose(fp);

  return 0;
}