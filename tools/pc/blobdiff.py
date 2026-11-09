import sys
from PIL import Image, ImageChops, ImageFilter
a=Image.open(sys.argv[1]); r=Image.open(sys.argv[2])
d=ImageChops.difference(a,r).convert('L').point(lambda v:255 if v else 0)
e=d.filter(ImageFilter.MinFilter(3))   # erode: 1-px lines vanish
n=sum(1 for v in e.getdata() if v)
print(f"{n} pixels in blobs ({100.0*n/(a.width*a.height):.2f}%)")
if len(sys.argv)>3: e.save(sys.argv[3])
