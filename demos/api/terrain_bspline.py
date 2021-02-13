import gmsh
import math
import sys

gmsh.initialize(sys.argv)

gmsh.model.add("terrain")

# create the terrain surface from N x N input data points (here simulated using
# a simple function):
N = 100
ps = []
p1 = -1
p2 = -1
p3 = -1
p4 = -1

# create a bspline surface
for i in range(N + 1):
    for j in range(N + 1):
        ps.append(gmsh.model.occ.addPoint(float(i) / N, float(j) / N,
                                          0.05 * math.sin(10 * float(i + j) / N)))
s = gmsh.model.occ.addBSplineSurface(ps, numPointsU=N+1)

# create a box
v = gmsh.model.occ.addBox(0, 0, -0.5, 1, 1, 1)

# fragment the box with the bspline surface
gmsh.model.occ.fragment([(2, s)], [(3, v)])
gmsh.model.occ.synchronize()

# set this to True to build a fully hex mesh
#transfinite = True
transfinite = False

if transfinite:
    NN = 30
    for c in gmsh.model.getEntities(1):
        gmsh.model.mesh.setTransfiniteCurve(c[1], NN)
    for s in gmsh.model.getEntities(2):
        gmsh.model.mesh.setTransfiniteSurface(s[1])
        gmsh.model.mesh.setRecombine(s[0], s[1])
        gmsh.model.mesh.setSmoothing(s[0], s[1], 100)
    gmsh.model.mesh.setTransfiniteVolume(1)
    gmsh.model.mesh.setTransfiniteVolume(2)
else:
    gmsh.option.setNumber('Mesh.MeshSizeMin', 0.05)
    gmsh.option.setNumber('Mesh.MeshSizeMax', 0.05)

if '-nopopup' not in sys.argv:
    gmsh.fltk.run()
