"""
Download public domain / CC-licensed audio samples from the web.
"""
import urllib.request
import urllib.error
import os
import time

OUT_DIR = r"d:\AAA_C\compression-tool\resources\audio_samples"
os.makedirs(OUT_DIR, exist_ok=True)

# Well-known Kevin MacLeod CC-BY tracks on incompetech.com
# These are all Creative Commons: By Attribution 4.0
INCOMPETECH_TRACKS = [
    ("Dreams Become Real", "dreams-become-real"),
    ("Sneaky Snitch", "sneaky-snitch"),
    ("Cut and Run", "cut-and-run"),
    ("Carefree", "carefree"),
    ("Scheming Weasel", "scheming-weasel-faster"),
]

# Wayback Machine archive.org — public domain classical recordings
ARCHIVE_TRACKS = [
    # Beethoven Symphony No. 5 (public domain recording from Musopen)
    ("https://archive.org/download/Beethoven-SymphonyNo.5/02-Beethoven-SymphonyNo.5InCMinorOp.67-AllegroConBrio.mp3",
     "beethoven_symphony5_1st.mp3"),
    # Mozart Eine Kleine Nachtmusik
    ("https://archive.org/download/mozart-eine-kleine-nachtmusik/Mozart_Eine_Kleine_Nachtmusik.mp3",
     "mozart_eine_kleine_nachtmusik.mp3"),
]

print("Downloading audio samples...\n")


def download(url, filename, source_name):
    path = os.path.join(OUT_DIR, filename)
    if os.path.exists(path):
        print(f"  SKIP (exists): {filename}")
        return True

    print(f"  [{source_name}] Downloading: {filename} ...")
    try:
        req = urllib.request.Request(url, headers={
            'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
        })
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = resp.read()
            with open(path, 'wb') as f:
                f.write(data)
            size_kb = len(data) / 1024
            print(f"    OK: {size_kb:.0f} KB")
            return True
    except urllib.error.HTTPError as e:
        print(f"    HTTP {e.code}: {e.reason}")
        return False
    except Exception as e:
        print(f"    ERROR: {e}")
        return False


# Try incompetech
for title, slug in INCOMPETECH_TRACKS:
    url = f"https://incompetech.com/music/royalty-free/mp3-royaltyfree/{slug}.mp3"
    download(url, f"{slug}.mp3", "incompetech")
    time.sleep(0.5)

# Try archive.org
for url, filename in ARCHIVE_TRACKS:
    download(url, filename, "archive.org")
    time.sleep(0.5)

# List results
print(f"\nResults in {OUT_DIR}:")
for f in sorted(os.listdir(OUT_DIR)):
    path = os.path.join(OUT_DIR, f)
    size_kb = os.path.getsize(path) / 1024
    print(f"  {f} — {size_kb:.0f} KB")

print(f"\nTotal: {len(os.listdir(OUT_DIR))} files")