from PIL import Image

master = Image.open("/Users/yves/Development/Audio/brand/mocks/victorian/master-01-base.png").convert("RGB")

dials = {
    "mainVU": (395.3, 377.7, 205.7),
    "smallMeterTop": (1178.6, 201.2, 52.8),
    "smallMeterMid": (1186.5, 375.7, 58.1),
    "smallMeterBottom": (1185.7, 551.9, 60.7),
}

for name, (cx, cy, r) in dials.items():
    pad = r * 1.15
    box = (int(cx - pad), int(cy - pad), int(cx + pad), int(cy + pad))
    crop = master.crop(box)
    scale = 4 if r < 100 else 2
    crop = crop.resize((crop.width * scale, crop.height * scale), Image.LANCZOS)
    crop.save(f"dialcrop_{name}.png")
    print(name, box, crop.size)
