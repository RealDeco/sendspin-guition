#!/usr/bin/env python3
import sys
import json
import urllib.request
import io

def get_dominant_color(url):
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        data = urllib.request.urlopen(req, timeout=5).read()
        from PIL import Image
        img = Image.open(io.BytesIO(data)).convert('RGB')
        img = img.resize((50, 50))
        pixels = list(img.getdata())  # vrací list tuplů (r, g, b)
        count = len(pixels)
        r = sum(p[0] for p in pixels) // count
        g = sum(p[1] for p in pixels) // count
        b = sum(p[2] for p in pixels) // count
        print(json.dumps({"r": r, "g": g, "b": b}))
    except Exception as e:
        print(json.dumps({"r": 0, "g": 0, "b": 0, "error": str(e)}))

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(json.dumps({"r": 0, "g": 0, "b": 0}))
    else:
        get_dominant_color(sys.argv[1])
