SetFactory("OpenCASCADE");

//Mesh.Algorithm = 6;
Mesh.CharacteristicLengthMin = 0.1;
Mesh.CharacteristicLengthMax = 0.1;

DefineConstant[
  x = {0, Min -5, Max 5, Step 0.1, Name "Bloc 1/0x"}
  y = {0, Min -5, Max 5, Step 0.1, Name "Bloc 1/0y"}
  z = {0, Min -5, Max 5, Step 0.1, Name "Bloc 1/0z"}
  dx = {2, Min 0.1, Max 5, Step 0.1, Name "Bloc 1/dx"}
  dy = {2, Min 0.1, Max 5, Step 0.1, Name "Bloc 1/dy"}
  dz = {2, Min 0.1, Max 5, Step 0.1, Name "Bloc 1/dz"}
  x2 = {x+dx, Min -5, Max 5, Step 0.1, Name "Bloc 2/0x"}
  y2 = {0, Min -5, Max 5, Step 0.1, Name "Bloc 2/0y"}
  z2 = {0, Min -5, Max 5, Step 0.1, Name "Bloc 2/0z"}
  dx2 = {1, Min 0.1, Max 5, Step 0.1, Name "Bloc 2/dx"}
  dy2 = {1, Min 0.1, Max 5, Step 0.1, Name "Bloc 2/dy"}
  dz2 = {3, Min 0.1, Max 5, Step 0.1, Name "Bloc 2/dz"}
];

Block(1) = {x,y,z, x+dx,y+dy,z+dz};
Block(2) = {x2,y2,z2, x2+dx2,y2+dy2,z2+dz2};

Translate{0.2,0.2,0.2}{ Volume{1}; }
Rotate { {1,0,0}, {0,0,0}, Pi/3 } { Volume{1}; }
Rotate { {0,1,0}, {0,0,0}, Pi/3 } { Volume{1}; }

f() = BooleanFragments { Volume{1}; Delete; }{ Volume{2}; Delete; };
