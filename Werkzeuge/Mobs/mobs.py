import os,sys
OUT=sys.argv[1]   # Zielordner "Strukturen"
def mob(name,base,factor,head,body,host=1):
    L=["# Eigener Mob (LAB). teil=Nr;Quelle;Index;Datei;Skala;Pos(0/1);x;y;z",
       "# Quelle: 0 = von der Basisrasse, 1 = keins, 2 = nativer Registry-Index, 3 = eigene .cub",
       f"name={name}",f"basis={base}","groesse=0",f"faktor={factor}",f"feindlichkeit={host}","level=-1","ausruestung=0","wandern=1"]
    for i in range(8):
        if i==0: L.append(f"teil=0;3;-1;{head};0;0;0;0;0")
        elif i==4: L.append(f"teil=4;3;-1;{body};0;0;0;0;0")
        else: L.append(f"teil={i};0;-1;;0;0;0;0;0")
    p=os.path.join(OUT,'Mobs',name+'.mob'); os.makedirs(os.path.dirname(p),exist_ok=True)
    open(p,'w',newline='\n').write('\n'.join(L)+'\n')
for v in range(1,6):
    mob(f'elf-w{v}',1,1.2,f'Teile\\Elfen\\elf-w{v}-kopf.cub',f'Teile\\Elfen\\elf-w{v}-koerper.cub',3)
    mob(f'elf-m{v}',0,1.2,f'Teile\\Elfen\\elf-m{v}-kopf.cub',f'Teile\\Elfen\\elf-m{v}-koerper.cub',3)
    mob(f'zwerg-untot-w{v}',10,1.0,f'Teile\\Zwerge\\zwerg-untot-w{v}-kopf.cub',f'Teile\\Zwerge\\zwerg-untot-w{v}-koerper.cub')
    mob(f'zwerg-untot-m{v}',9,1.0,f'Teile\\Zwerge\\zwerg-untot-m{v}-kopf.cub',f'Teile\\Zwerge\\zwerg-untot-m{v}-koerper.cub')
print('20 .mob')
