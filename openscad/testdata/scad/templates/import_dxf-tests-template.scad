import();
translate([-210,0,0]) import(file="../../dxf/polygons.dxf");
translate([-210,0,0]) import(file="../../dxf/polygons.dxf", origin=[0,110]);
translate([-210,0,0]) import(file="../../dxf/polygons.dxf", origin=[110,110], scale=0.5);
import(file="../../dxf/multiple-layers.dxf");
translate([-200,200,0]) import(file="../../dxf/multiple-layers.dxf", layer="0");
translate([0,200,0]) import(filename="../../dxf/multiple-layers.dxf", layername="0");
translate([200,200,0]) import(file="../../dxf/multiple-layers.dxf", layer="noname");
translate([0,200,0]) import(file="../../dxf/multiple-layers.dxf", layer="Layer with a pretty long name including \\ \"special\" /'\\\\ characters");
translate([200,0,0]) import(file="@CMAKE_SOURCE_DIR@/../testdata/dxf/polygons.dxf");