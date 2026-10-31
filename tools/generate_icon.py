"""Render the code-native llama/terminal emblem to PNG and Windows ICO."""
from pathlib import Path
from PIL import Image, ImageDraw

root=Path(__file__).resolve().parents[1]
scale=4
im=Image.new('RGBA',(256*scale,256*scale),(0,0,0,0))
d=ImageDraw.Draw(im)
def box(v):return tuple(int(n*scale) for n in v)
def polygon(points,fill):d.polygon([(x*scale,y*scale) for x,y in points],fill=fill)
d.rounded_rectangle(box((8,8,248,248)),radius=54*scale,fill='#17233d')
d.rounded_rectangle(box((19,19,237,237)),radius=44*scale,fill='#253962')
polygon([(66,201),(66,124),(77,106),(69,47),(73,36),(79,36),(85,44),(109,94),(136,89),(147,40),(153,32),(160,32),(166,42),(171,96),(194,116),(196,150),(188,162),(177,168),(166,167),(147,158),(141,201)],'#edf4ff')
polygon([(81,52),(86,96),(99,93)],'#86b5ff')
polygon([(157,49),(151,91),(163,94)],'#86b5ff')
d.ellipse(box((157,116,171,130)),fill='#17233d')
d.line(box((184,143,191,143)),fill='#17233d',width=5*scale)
d.rounded_rectangle(box((124,171,225,230)),radius=17*scale,fill='#5c83f2')
d.line([(143*scale,187*scale),(157*scale,200*scale),(143*scale,213*scale)],fill='white',width=7*scale,joint='curve')
d.line(box((171,213,202,213)),fill='white',width=7*scale)
im.resize((256,256),Image.Resampling.LANCZOS).save(root/'resources/app-icon.png')
im.resize((256,256),Image.Resampling.LANCZOS).save(root/'resources/app.ico',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
