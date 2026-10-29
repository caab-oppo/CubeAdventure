import sqlite3, struct
T=[4242,9551,840,84800,9242,9846,127,9,9483,394,123,4834,32444,24355,2433,17,34234,42342,4243,14,184934,1987,3094,1901,89409,4813,37,143,3490,19483,1343,432,84732,9184,9612,1233,3434,1839,2984,1993,2984,4895,816583,13]
import os, sys
DB=os.environ.get("CUBE_DATA1", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "data1.db"))
def get(name):
    c=sqlite3.connect('file:'+DB+'?mode=ro',uri=True)
    v=c.execute("select value from blobs where key=?",(name,)).fetchone()
    if not v: return None
    b=bytearray(v[0]); n=len(b)
    for i in range(n-1,-1,-1):
        j=(T[i%44]+i)%n; b[i],b[j]=b[j],b[i]
    b=bytearray((~x)&0xff for x in b)
    sx,sy,sz=struct.unpack_from('<3i',b,0)
    return sx,sy,sz,bytes(b[12:])
