ID NASTRAN,WING_RIB
SOL 101
CEND
BEGIN BULK
$ Local rectangular coordinate system translated +5.0 in X
CORD2R         1       0     5.0     0.0     0.0     5.0     0.0     1.0+C1     
+C1          6.0     0.0     0.0
$ Small-field & exponentless float GRID cards in CID=1
GRID           1       1     0.0     0.0     0.0
GRID           2       1  2.5+0      0.0     0.0
GRID           3       1  2.5+0   1.5+0      0.0
GRID           4       1     0.0  1.5+0      0.0
GRID           5       1     0.0     0.0  1.2+0 
GRID           6       1  2.5+0      0.0  1.2+0 
$ Large-field GRID* two-line cards
GRID*                  7               1      2.500000+0      1.500000+0*G7
*G7           1.200000+0
GRID*                  8               1             0.0      1.500000+0*G8
*G8           1.200000+0
$ Free-field GRID cards for wedge tip
GRID,9,1,4.0,0.75,0.0
GRID,10,1,4.0,0.75,1.2
$ Solid CHEXA + CPENTA sharing face (2,3,7,6)
CHEXA        101       1       1       2       3       4       5       6+HX1    
+HX1           7       8
CPENTA       102       1       2       9       3       6      10       7
ENDDATA
