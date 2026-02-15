#!/usr/bin/env python3
"""
Download random images from the internet.
Uses https://picsum.photos (free random image service).
"""

from pathlib import Path
import requests
import time
import random
from io import BytesIO
from PIL import Image

COUNT = 5
MIN_WIDTH = 160
MAX_WIDTH = 480
MIN_HEIGHT = 120
MAX_HEIGHT = 320

for i in range(COUNT):
    ext = random.choice(("jpg", "png"))
    out_file = Path(f"random_image{i}.{ext}")
    width = random.randint(MIN_WIDTH, MAX_WIDTH)
    height = random.randint(MIN_HEIGHT, MAX_HEIGHT)
    # Picsum serves random JPEG reliably on this endpoint.
    base_url = f"https://picsum.photos/{width}/{height}"
    # Add a changing query value so each request is randomized.
    url = f"{base_url}?random={time.time_ns()}_{i}"
    resp = requests.get(url, timeout=20)
    resp.raise_for_status()
    content_type = resp.headers.get("Content-Type", "")
    if "jpeg" not in content_type.lower():
        raise RuntimeError(f"Expected JPEG but got Content-Type: {content_type}")
    if ext == "jpg":
        out_file.write_bytes(resp.content)
    else:
        # Convert downloaded JPEG to PNG when PNG was randomly chosen.
        img = Image.open(BytesIO(resp.content)).convert("RGB")
        img.save(out_file, format="PNG")
    print(f"Saved random image to: {out_file.resolve()} ({width}x{height}, {ext})")
