import gzip, json, sqlite3, sys, os
from pathlib import Path
sys.path.insert(0, str(Path(os.environ['TEMP']) / 'metro-render-deps'))
import mapbox_vector_tile
import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont
root = Path('E:/projs/c/GZ-metro-planner')
out = root / 'output/metro-preview'
db = sqlite3.connect('file:' + (root/'data/metro.mbtiles').as_posix() + '?mode=ro', uri=True)
colors = {i: f'#{c:06x}' for i,n,c in db.execute('select * from lines')}
line_names = {i:n for i,n,c in db.execute('select * from lines')}
stations = {r[0]:r for r in db.execute('select * from stations')}
edges = {r[0]:r for r in db.execute('select * from edges')}
canvas = Image.new('RGB',(4096,4096),'white')
draw = ImageDraw.Draw(canvas)
paths = {}
layer_counts = {}
for z,x,row,blob in db.execute('select * from tiles order by zoom_level'):
    layers = mapbox_vector_tile.decode(gzip.decompress(blob),default_options={'y_coord_down':True})
    for name,layer in layers.items():
        layer_counts[name] = layer_counts.get(name,0) + len(layer['features'])
        if z != 5: continue
        size = 4096 / 2**z
        y = 2**z-1-row
        for f in layer['features']:
            geom = f['geometry']
            parts = [geom['coordinates']] if geom['type']=='LineString' else geom['coordinates']
            for part in parts:
                pts = [(x*size+p[0]*size/layer['extent'], y*size+p[1]*size/layer['extent']) for p in part]
                draw.line(pts,fill=colors[f['properties']['line_id']],width=7,joint='curve')
                paths.setdefault(f['id'],[]).append(pts)
canvas.save(out/'metro-lines.png')
annotated = canvas.copy()
d = ImageDraw.Draw(annotated)
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',16)
for sid,name,py,initials,x,y,transfer in stations.values():
    r=6 if transfer else 4
    d.ellipse((x-r,y-r,x+r,y+r),fill='white',outline='#444444',width=2)
    d.text((x+8,y-20),name,font=font,fill='#222222',stroke_width=2,stroke_fill='white')
annotated.save(out/'metro-stations.png')
original = Image.open(root/'data/railway.png').convert('RGB')
base = Image.blend(original, Image.new('RGB',original.size,'white'),0.55)
d = ImageDraw.Draw(base)
for parts in paths.values():
    for pts in parts: d.line(pts,fill='#e000bd',width=5,joint='curve')
base.save(out/'overlay.png')
compare = Image.new('RGB',(2048,1070),'white')
d = ImageDraw.Draw(compare)
font_title = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',24)
d.text((20,10),'原图 railway.png',font=font_title,fill='black')
d.text((1044,10),'MBTiles 实际瓦片几何 + lines 表颜色',font=font_title,fill='black')
compare.paste(original.resize((1024,1024)),(0,46))
compare.paste(canvas.resize((1024,1024)),(1024,46))
compare.save(out/'comparison.png')
# A geometric proxy: distance to similarly colored source pixels, not a similarity score.
hsv = cv2.cvtColor(np.array(original),cv2.COLOR_RGB2HSV)
metrics=[]
all_dist=[]
for lid,col in colors.items():
    rgb=np.uint8([[[int(col[k:k+2],16) for k in (1,3,5)]]])
    target=cv2.cvtColor(rgb,cv2.COLOR_RGB2HSV)[0,0]
    hue=np.abs(hsv[:,:,0].astype(np.int16)-int(target[0])); hue=np.minimum(hue,180-hue)
    mask=(hue<9)&(hsv[:,:,1]>75)&(hsv[:,:,2]>50)&(np.abs(hsv[:,:,1].astype(np.int16)-int(target[1]))<70)
    distance=cv2.distanceTransform((~mask).astype(np.uint8),cv2.DIST_L2,5)
    for eid,parts in paths.items():
        edge=edges[eid]
        if edge[1]!=lid: continue
        samples=[]
        for pts in parts:
            for a,b in zip(pts,pts[1:]):
                n=max(2,int(np.hypot(b[0]-a[0],b[1]-a[1]))+1)
                p=np.linspace(a,b,n).round().astype(int).clip(0,4095)
                samples.extend(distance[p[:,1],p[:,0]].tolist())
        a=np.array(samples)
        if not len(a):continue
        all_dist.extend(samples)
        metrics.append({'edge':eid,'line':line_names[lid],'from':stations[edge[2]][1],'to':stations[edge[3]][1],'mean_px':round(float(a.mean()),2),'p95_px':round(float(np.percentile(a,95)),2),'max_px':round(float(a.max()),2)})
a=np.array(all_dist)
report={'integrity':db.execute('pragma integrity_check').fetchone()[0],'tiles':db.execute('select count(*) from tiles').fetchone()[0],'stations':len(stations),'lines':len(colors),'edges':len(edges),'rendered_unique_edges':len(paths),'decoded_layers':layer_counts,'method':'Distance from z5 edge samples (~1 px intervals) to similarly colored pixels of original 4096px image. Includes symbol/color interference; not percent visual similarity or topology accuracy.','mean_px':float(a.mean()),'median_px':float(np.median(a)),'p95_px':float(np.percentile(a,95)),'within_10px_percent':float((a<=10).mean()*100),'worst_edges':sorted(metrics,key=lambda r:r['mean_px'],reverse=True)[:20]}
(out/'inspection.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(report,ensure_ascii=True,indent=2))
