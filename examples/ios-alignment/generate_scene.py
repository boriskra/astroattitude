#!/usr/bin/env python3
"""Render the generated board through an independent ideal pinhole camera.
No photograph, observation timestamp or handset parameters are inputs.
"""
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]

def generate():
    width, height, focal = 600, 800, 700
    cx, cy = width / 2, height / 2
    ax, ay = .25, .35
    sx, cxr, sy, cyr = math.sin(ax), math.cos(ax), math.sin(ay), math.cos(ay)
    r = [[cyr, sy*sx, sy*cxr], [0, cxr, -sx], [-sy, cyr*sx, cyr*cxr]]
    t = [-.09, -.09, .6]
    h = [[focal*r[0][0]+cx*r[2][0], focal*r[0][1]+cx*r[2][1], focal*t[0]+cx*t[2]],
         [focal*r[1][0]+cy*r[2][0], focal*r[1][1]+cy*r[2][1], focal*t[1]+cy*t[2]],
         [r[2][0], r[2][1], t[2]]]
    # Adjugate is enough for homogeneous inversion.
    inverse = [[h[(j+1)%3][(i+1)%3]*h[(j+2)%3][(i+2)%3]-h[(j+1)%3][(i+2)%3]*h[(j+2)%3][(i+1)%3] for j in range(3)] for i in range(3)]
    rects = []
    for e in ET.parse(ROOT/'examples/ios-alignment/target.svg').getroot():
        if e.tag.endswith('rect') and 'x' in e.attrib:
            rects.append(tuple(float(e.attrib[k]) for k in ('x','y','width','height'))+(0 if e.attrib['fill']=='black' else 255,))
    pixels = []
    for y in range(height):
        for x in range(width):
            p = [sum(inverse[i][j]*[x+.5,y+.5,1][j] for j in range(3)) for i in range(3)]
            u,v=p[0]/p[2]*1000+15,p[1]/p[2]*1000+35
            value=255
            for rx,ry,rw,rh,color in rects:
                if rx<=u<rx+rw and ry<=v<ry+rh: value=color
            pixels.append(value)
    return dict(width=width,height=height,focal=focal,luma=pixels,
                expected_reference_from_camera=[r[j][i] for i in range(3) for j in range(3)])

if __name__=='__main__':
    output=ROOT/'build/fixtures/target-scene.json'
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(generate(),separators=(',',':'))+'\n')
    print('Independent target scene generated')
