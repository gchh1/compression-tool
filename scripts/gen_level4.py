import struct, os, random, zlib, json

random.seed(42)

def make_png(width, height, color_func):
    def make_chunk(ctype, data):
        c = ctype + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    sig = b'\x89PNG\r\n\x1a\n'
    ihdr_data = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    ihdr = make_chunk(b'IHDR', ihdr_data)
    raw = b''
    for y in range(height):
        raw += b'\x00'
        for x in range(width):
            r, g, b = color_func(x, y, width, height)
            raw += bytes([r, g, b])
    idat = make_chunk(b'IDAT', zlib.compress(raw))
    iend = make_chunk(b'IEND', b'')
    return sig + ihdr + idat + iend

def make_wav(sample_rate, duration, gen_func, channels=1, bits=16):
    num_samples = int(sample_rate * duration)
    data = b''
    for i in range(num_samples):
        t = i / sample_rate
        val = gen_func(t, duration)
        val = max(-32768, min(32767, int(val * 32767)))
        data += struct.pack('<h', val)
        if channels == 2:
            data += struct.pack('<h', val)
    byte_rate = sample_rate * channels * (bits // 8)
    block_align = channels * (bits // 8)
    data_size = len(data)
    header = b'RIFF'
    header += struct.pack('<I', 36 + data_size)
    header += b'WAVE'
    header += b'fmt '
    header += struct.pack('<IHHIIHH', 16, 1, channels, sample_rate, byte_rate, block_align, bits)
    header += b'data'
    header += struct.pack('<I', data_size)
    return header + data

def make_bmp(width, height, color_func):
    row_size = (width * 3 + 3) & ~3
    pixel_data = b''
    for y in range(height - 1, -1, -1):
        row = b''
        for x in range(width):
            r, g, b = color_func(x, y, width, height)
            row += bytes([b, g, r])
        row += b'\x00' * (row_size - width * 3)
        pixel_data += row
    file_size = 54 + len(pixel_data)
    header = b'BM'
    header += struct.pack('<I', file_size)
    header += struct.pack('<HH', 0, 0)
    header += struct.pack('<I', 54)
    header += struct.pack('<I', 40)
    header += struct.pack('<i', width)
    header += struct.pack('<i', height)
    header += struct.pack('<HH', 1, 24)
    header += struct.pack('<I', 0)
    header += struct.pack('<I', len(pixel_data))
    header += struct.pack('<i', 2835)
    header += struct.pack('<i', 2835)
    header += struct.pack('<I', 0)
    header += struct.pack('<I', 0)
    return header + pixel_data

base = os.path.join(os.path.dirname(__file__), '..', 'resources', 'level4_media')

# PNG images
with open(os.path.join(base, 'images', 'logo.png'), 'wb') as f:
    f.write(make_png(64, 64, lambda x,y,w,h: (x*4 % 256, y*4 % 256, 128)))

with open(os.path.join(base, 'images', 'banner.png'), 'wb') as f:
    f.write(make_png(800, 200, lambda x,y,w,h: (int(50+150*x/w), int(100+100*y/h), 200 if (x//20)%2==0 else 180)))

with open(os.path.join(base, 'images', 'photo_landscape.png'), 'wb') as f:
    f.write(make_png(400, 300, lambda x,y,w,h: (128+60*((x*y)%3-1), 148+60*((x*y)%3-1), 118+60*((x*y)%3-1))))

with open(os.path.join(base, 'images', 'photo_portrait.png'), 'wb') as f:
    f.write(make_png(300, 400, lambda x,y,w,h: (min(int(200*(1-y/h)),255), min(int(150*x/w),255), min(int(100+100*y/h),255))))

with open(os.path.join(base, 'images', 'icon.png'), 'wb') as f:
    f.write(make_png(32, 32, lambda x,y,w,h: (255 if x<16 else 0, 128, 0 if x<16 else 255)))

with open(os.path.join(base, 'images', 'thumbnail_grid.png'), 'wb') as f:
    f.write(make_png(256, 256, lambda x,y,w,h: ((x//64)*60+30, (y//64)*50+20, ((x//64+y//64)*30+40)%256)))

# BMP
with open(os.path.join(base, 'images', 'photo_bmp.bmp'), 'wb') as f:
    f.write(make_bmp(200, 150, lambda x,y,w,h: (int(255*x/w), int(255*y/h), 128)))

# SVG
svg_parts = [
    '<?xml version="1.0" encoding="UTF-8"?>',
    '<svg xmlns="http://www.w3.org/2000/svg" width="800" height="600" viewBox="0 0 800 600">',
    '  <defs>',
    '    <linearGradient id="bg" x1="0%" y1="0%" x2="100%" y2="100%">',
    '      <stop offset="0%" style="stop-color:#1a1a2e"/>',
    '      <stop offset="100%" style="stop-color:#16213e"/>',
    '    </linearGradient>',
    '  </defs>',
    '  <rect width="800" height="600" fill="url(#bg)"/>',
    '  <circle cx="400" cy="200" r="80" fill="#e94560" opacity="0.8"/>',
    '  <text x="400" y="420" text-anchor="middle" fill="#e2e8f0" font-size="32" font-family="sans-serif">Media Gallery</text>',
]
for i in range(20):
    cx = random.randint(50, 750)
    cy = random.randint(50, 550)
    r = random.randint(5, 30)
    opacity = round(random.uniform(0.1, 0.4), 2)
    svg_parts.append(f'  <circle cx="{cx}" cy="{cy}" r="{r}" fill="#e94560" opacity="{opacity}"/>')
svg_parts.append('</svg>')
with open(os.path.join(base, 'images', 'hero_illustration.svg'), 'w', encoding='utf-8') as f:
    f.write('\n'.join(svg_parts))

# WAV audio
def notif(t, d):
    return 0.5 * (1 - t/d) * (1 if (t*880) % 1 < 0.5 else -1)
with open(os.path.join(base, 'audio', 'notification.wav'), 'wb') as f:
    f.write(make_wav(22050, 0.5, notif))

def bg_music(t, d):
    env = min(t * 4, 1.0) * min((d - t) * 4, 1.0)
    s = 0.3 * (1 if (t*261.63) % 1 < 0.5 else -1)
    s += 0.2 * (1 if (t*329.63) % 1 < 0.5 else -1)
    s += 0.15 * (1 if (t*392.0) % 1 < 0.5 else -1)
    return env * s
with open(os.path.join(base, 'audio', 'background_music.wav'), 'wb') as f:
    f.write(make_wav(44100, 5.0, bg_music))

def voice(t, d):
    env = min(t * 10, 1.0) * min((d - t) * 10, 1.0)
    f0 = 150 + 50 * (t / d)
    return env * 0.4 * (1 if (t * f0) % 1 < 0.5 else -1)
with open(os.path.join(base, 'audio', 'voice_sample.wav'), 'wb') as f:
    f.write(make_wav(22050, 3.0, voice))

# JSON
data = {
    'site': 'MediaHub',
    'version': '2.0',
    'media': [
        {'id': 1, 'type': 'image', 'src': 'images/banner.png', 'alt': 'Site Banner', 'width': 800, 'height': 200},
        {'id': 2, 'type': 'image', 'src': 'images/photo_landscape.png', 'alt': 'Landscape', 'width': 400, 'height': 300},
        {'id': 3, 'type': 'image', 'src': 'images/photo_portrait.png', 'alt': 'Portrait', 'width': 300, 'height': 400},
        {'id': 4, 'type': 'audio', 'src': 'audio/background_music.wav', 'title': 'Background Music', 'duration': 5.0},
        {'id': 5, 'type': 'audio', 'src': 'audio/notification.wav', 'title': 'Notification', 'duration': 0.5},
        {'id': 6, 'type': 'audio', 'src': 'audio/voice_sample.wav', 'title': 'Voice Sample', 'duration': 3.0},
        {'id': 7, 'type': 'image', 'src': 'images/thumbnail_grid.png', 'alt': 'Thumbnails', 'width': 256, 'height': 256},
        {'id': 8, 'type': 'image', 'src': 'images/photo_bmp.bmp', 'alt': 'BMP Photo', 'width': 200, 'height': 150},
    ],
    'settings': {'theme': 'dark', 'autoplay': False, 'quality': 'high'},
}
for i in range(50):
    data['media'].append({
        'id': 100 + i,
        'type': random.choice(['image', 'audio']),
        'src': f'images/thumb_{i}.png',
        'alt': f'Media item {i}',
        'tags': [random.choice(['nature','tech','music','art','photo']) for _ in range(random.randint(1,3))],
    })
with open(os.path.join(base, 'media_catalog.json'), 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2, ensure_ascii=False)

# CSV
csv_lines = ['id,filename,type,size_bytes,duration,format,quality,tags']
for i in range(200):
    t = random.choice(['image/png','audio/wav','image/bmp','image/svg+xml'])
    sz = random.randint(500, 500000)
    dur = round(random.uniform(0.1, 10.0), 2) if 'audio' in t else 0
    q = random.choice(['low','medium','high','lossless'])
    tags = ';'.join(random.choice(['nature','tech','music','art','portrait','landscape','abstract']) for _ in range(random.randint(1,3)))
    csv_lines.append(f'{i+1},file_{i+1},{t},{sz},{dur},{q},{tags}')
with open(os.path.join(base, 'media_index.csv'), 'w', encoding='utf-8') as f:
    f.write('\n'.join(csv_lines) + '\n')

# Print sizes
total = 0
for root, dirs, files in os.walk(base):
    for fn in files:
        fp = os.path.join(root, fn)
        sz = os.path.getsize(fp)
        total += sz
        rel = os.path.relpath(fp, base)
        print(f'  {rel}: {sz:,} bytes')
print(f'\nTotal: {total:,} bytes ({total/1024:.1f} KB)')
