"""Original geometric aircraft icon. No external artwork, fonts or logos."""
from pathlib import Path
from PIL import Image, ImageDraw
root=Path(__file__).resolve().parent.parent
S=1024
image=Image.new('RGBA',(S,S),(0,0,0,0));mask=Image.new('L',(S,S),0)
ImageDraw.Draw(mask).rounded_rectangle((32,32,992,992),radius=216,fill=255)
gradient=Image.new('RGBA',(S,S));pixels=gradient.load()
for y in range(S):
    t=y/(S-1);color=tuple(round(a*(1-t)+b*t) for a,b in zip((18,110,209),(7,28,67)))+(255,)
    for x in range(S):pixels[x,y]=color
image.paste(gradient,(0,0),mask);d=ImageDraw.Draw(image)
aircraft=[(54,182),(114,122),(128,52),(142,122),(202,182),(195,193),(139,167),(136,198),(149,212),(128,206),(107,212),(120,198),(117,167),(61,193)]
d.polygon([(x*4,y*4) for x,y in aircraft],fill='white')
for x,y,w,h in [(34,131,26,16),(43,103,29,16),(59,75,29,16),(194,79,22,22),(215,111,15,15)]:d.rounded_rectangle((x*4,y*4,(x+w)*4,(y+h)*4),radius=8,fill=(98,229,219,255))
assets=root/'assets';assets.mkdir(exist_ok=True)
image.resize((256,256),Image.Resampling.LANCZOS).save(assets/'app-icon.png')
image.save(assets/'app.ico',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
print('Original PNG/ICO icon created from geometric artwork.')
