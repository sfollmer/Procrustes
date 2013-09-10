import("/Users/follmer/Desktop/OpenSCAD test files/fileHandleRotated_Aligned2_repaired_smoothed_rep.stl", convexity=2);

{ 

rotate(a=-1.9, v=[0,0,1]) 
rotate(a=-0, v=[0,1,0]) 

rotate(a=-90, v=[1,0,0]) 


rotate(a=90, v=[0,1,0])



  translate([47.6321, -66.3706, -19.4844]) scale([170.394, 170.394, 170.394]) rotate(a=90, v=[1,0,0]) rotate(a=90, v=[0,1,0] ) import("trianglesExp.stl", convexity=2);}