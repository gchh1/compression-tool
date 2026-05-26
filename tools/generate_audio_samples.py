"""
Audio sample generator for compression tool testing.
Generates various interesting synthetic audio samples (WAV, 44100Hz, 16-bit).

All generated files are original synthetic audio — no copyright issues.
"""
import struct
import math
import wave
import os
import random

OUT_DIR = r"d:\AAA_C\compression-tool\resources\audio_samples"
SR = 44100
AMP16 = 32767


def write_wav(path, samples_mono, sample_rate=SR):
    """Write mono 16-bit PCM WAV file."""
    raw = struct.pack('<' + 'h' * len(samples_mono),
                      *[max(-32768, min(32767, int(s))) for s in samples_mono])
    with wave.open(path, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(raw)
    size_kb = os.path.getsize(path) / 1024
    dur = len(samples_mono) / sample_rate
    print(f"  OK: {os.path.basename(path)} — {dur:.1f}s, {size_kb:.0f} KB")


def piano_arpeggio():
    """
    C major arpeggio: C4-E4-G4-C5-E5-G5-C6
    Each note has 6 harmonics with exponential decay.
    """
    print("[1/8] Piano arpeggio (C major, harmonics + decay)")
    notes = [261.63, 329.63, 392.00, 523.25, 659.25, 783.99, 1046.50]
    note_len = int(SR * 0.35)
    samples = []
    for freq in notes:
        for i in range(note_len):
            t = i / SR
            env = math.exp(-3.5 * t)
            y = 0.0
            for h in range(1, 7):
                y += (1.0 / h) * math.sin(2 * math.pi * freq * h * t)
            samples.append(AMP16 * 0.6 * y * env)
    write_wav(os.path.join(OUT_DIR, "piano_arpeggio.wav"), samples)


def jazz_walking_bass():
    """
    Walking bass line over 12-bar blues progression in Bb.
    Notes: Bb2 C3 D3 Eb3 E3 F3 G3 Ab3
    Uses a plucked-bass envelope.
    """
    print("[2/8] Jazz walking bass (12-bar blues, Bb)")
    scale = [116.54, 130.81, 146.83, 155.56, 164.81, 174.61, 196.00, 207.65]
    progression = [
        0, 0, 0, 0, 3, 3, 0, 0, 5, 3, 0, 4,
        0, 0, 0, 0, 3, 3, 0, 0, 5, 3, 0, 4,
        0, 0, 0, 0, 3, 3, 0, 0, 5, 3, 0, 4,
        0, 0, 0, 0, 3, 3, 0, 0, 5, 3, 0, 4,
    ]
    note_len = int(SR * 0.20)
    samples = []
    for chord_root in progression:
        n = scale[chord_root] if chord_root < len(scale) else 261.63
        walk = random.choice([0, 2, 4, 5, 7])
        freq = scale[(chord_root + walk) % len(scale)]
        for i in range(note_len):
            t = i / SR
            env = math.exp(-7.0 * t)
            y = math.sin(2 * math.pi * freq * t)
            y += 0.5 * math.sin(2 * math.pi * freq * 2 * t)
            samples.append(AMP16 * 0.7 * y * env)
    write_wav(os.path.join(OUT_DIR, "jazz_walking_bass.wav"), samples)


def electronic_drums():
    """
    808-style drum pattern: kick, snare, hi-hat, clap.
    BPM=120, 4-bar loop.
    """
    print("[3/8] Electronic drum pattern (808 style, 120 BPM)")
    beat_len = int(SR * 0.5)  # half-second per beat
    num_beats = 16
    samples = []

    for beat in range(num_beats):
        for i in range(beat_len):
            t = i / SR
            y = 0.0
            pos_in_bar = beat % 4

            # Kick on 1 and 3
            if pos_in_bar in (0, 2):
                f0 = 55.0
                env = math.exp(-30.0 * t)
                y += AMP16 * 0.8 * math.sin(2 * math.pi * (f0 + f0 * env) * t) * env

            # Snare/clap on 2 and 4
            if pos_in_bar in (1, 3):
                env = math.exp(-20.0 * t)
                noise = random.uniform(-1, 1)
                tone = math.sin(2 * math.pi * 200.0 * t)
                y += AMP16 * 0.5 * (0.6 * noise + 0.4 * tone) * env

            # Hi-hat every 8th
            if beat % 2 == 0 and t < 0.04:
                env_hh = math.exp(-80.0 * t)
                noise_hh = random.uniform(-1, 1)
                y += AMP16 * 0.15 * noise_hh * env_hh

            # Extra off-beat hi-hat
            if pos_in_bar in (0, 2) and t < 0.02 and i > beat_len // 2:
                y += AMP16 * 0.12 * random.uniform(-1, 1) * math.exp(-100 * (t - 0.25))

            samples.append(y)

    write_wav(os.path.join(OUT_DIR, "electronic_drums.wav"), samples)


def speech_synthetic():
    """
    Formant synthesis — alternates between /a/ /e/ /i/ /o/ /u/ vowel sounds
    with a glottal pulse source.
    """
    print("[4/8] Synthetic speech (vowel formants: a-e-i-o-u)")
    vowels = {
        'a': [(730, 1.0), (1090, 0.7), (2440, 0.3)],
        'e': [(530, 1.0), (1840, 0.7), (2480, 0.3)],
        'i': [(270, 1.0), (2290, 0.6), (3010, 0.2)],
        'o': [(480, 1.0), (840, 0.6), (2440, 0.2)],
        'u': [(300, 1.0), (870, 0.4), (2240, 0.15)],
    }
    f0 = 140.0
    vowel_dur = int(SR * 0.25)
    gap_dur = int(SR * 0.06)
    sequence = ['a', 'e', 'i', 'o', 'u', 'o', 'i', 'e', 'a']
    samples = []

    for v in sequence:
        formants = vowels[v]
        for i in range(vowel_dur):
            t = i / SR
            glottal = 0.0
            pulse_idx = int(t * f0) % 1
            if pulse_idx < 1.0 / f0:
                glottal = max(0, 1 - abs(pulse_idx - 0.5) / 0.5)
                glottal *= glottal

            env_start = min(1.0, t * 40)
            env_end = max(0, 1 - max(0, t - 0.22) * 30)
            env = env_start * env_end

            y = 0.0
            for ff, amp in formants:
                y += amp * math.sin(2 * math.pi * ff * t * (1 + 0.1 * glottal))
            samples.append(AMP16 * 0.35 * y * glottal * env)

        for i in range(gap_dur):
            samples.append(0)

    write_wav(os.path.join(OUT_DIR, "speech_synthetic.wav"), samples)


def rain_ambient():
    """
    Rain sound — filtered noise with random drops.
    """
    print("[5/8] Rain ambient (filtered noise + drops)")
    duration = 3.0
    n = int(SR * duration)
    samples = []
    lpf_buf = 0.0

    for i in range(n):
        noise = random.uniform(-1, 1)
        lpf_buf = 0.97 * lpf_buf + 0.03 * noise
        y = lpf_buf * 0.4

        if random.random() < 0.03:
            drop_env = 1.0
            drop_t = 0.0
            for j in range(min(int(SR * 0.06), n - i - 1)):
                idx = i + j
                if idx >= n:
                    break
                drop_phase = drop_t * 2000 * math.exp(-drop_t * 30)
                drop_env = math.exp(-drop_t * 50)
                y_drop = 0.8 * drop_env * math.sin(2 * math.pi * drop_phase)
                if idx < len(samples):
                    samples[idx] += AMP16 * y_drop
                else:
                    samples.append(AMP16 * y_drop)
                drop_t += 1 / SR

        samples.append(AMP16 * y)

    write_wav(os.path.join(OUT_DIR, "rain_ambient.wav"), samples)


def orchestral_hit():
    """
    Orchestral hit — layered brass, strings, percussion.
    """
    print("[6/8] Orchestral hit (layered brass + strings + percussion)")
    duration = 1.5
    n = int(SR * duration)
    samples = []

    for i in range(n):
        t = i / SR
        env = math.exp(-1.8 * t)
        y = 0.0

        # Brass: odd harmonics
        for h in [1, 3, 5, 7, 9]:
            y += (1.0 / h) * math.sin(2 * math.pi * 220 * h * t)

        # Strings: higher sustained
        str_env = math.exp(-0.5 * t)
        for h in [1, 2, 3]:
            y += (0.5 / h) * math.sin(2 * math.pi * 440 * h * t) * str_env

        # Percussion attack
        if t < 0.02:
            noise = random.uniform(-1, 1)
            y += 2.0 * noise * math.exp(-100 * t * t)

        # Tympani roll
        roll_env = math.exp(-6.0 * t)
        y += 0.4 * math.sin(2 * math.pi * (120 + 20 * math.exp(-15 * t)) * t) * roll_env

        samples.append(AMP16 * 0.35 * y * env)

    write_wav(os.path.join(OUT_DIR, "orchestral_hit.wav"), samples)


def guitar_pluck():
    """
    Karplus-Strong plucked string algorithm.
    Plays a simple melody: E-B-G-D-A-E (open strings tuning).
    """
    print("[7/8] Guitar plucks (Karplus-Strong, open strings melody)")

    def ks_string(freq, dur_sec, sr):
        period = int(sr / freq)
        if period < 2:
            period = 2
        buf = [random.uniform(-1, 1) for _ in range(period)]
        n = int(sr * dur_sec)
        out = []
        idx = 0
        for i in range(n):
            out.append(buf[idx] * math.exp(-1.5 * i / sr))
            buf[idx] = 0.4995 * (buf[idx] + buf[(idx + 1) % period])
            idx = (idx + 1) % period
        return out

    melody = [
        (329.63, 0.35), (246.94, 0.35), (196.00, 0.35),
        (146.83, 0.35), (110.00, 0.35), (82.41, 0.50),
    ]
    samples = []
    for freq, dur in melody:
        pluck = ks_string(freq, dur, SR)
        silence = [0] * int(SR * 0.06)
        samples.extend([int(AMP16 * 0.6 * s) for s in pluck])
        samples.extend(silence)

    # Strum chord at end
    chord = [329.63, 261.63, 196.00, 164.81, 130.81, 98.00]
    chord_samples = [0] * int(SR * 0.8)
    for cf in chord:
        pluck = ks_string(cf, 0.8, SR)
        for j, s in enumerate(pluck):
            chord_samples[j] += s * 0.3
    samples.extend([int(AMP16 * 0.5 * s) for s in chord_samples])

    write_wav(os.path.join(OUT_DIR, "guitar_pluck.wav"), samples)


def frequency_sweep():
    """
    Exponential sweep from 20Hz to 20kHz — good for testing frequency response.
    """
    print("[8/8] Frequency sweep (20Hz → 20kHz, 2s)")
    duration = 2.0
    n = int(SR * duration)
    f0, f1 = 20.0, 20000.0
    samples = []
    for i in range(n):
        t = i / SR
        freq = f0 * (f1 / f0) ** (t / duration)
        phase = 2 * math.pi * f0 * duration / math.log(f1 / f0) * \
                ((f1 / f0) ** (t / duration) - 1)
        samples.append(int(AMP16 * 0.8 * math.sin(phase)))
    write_wav(os.path.join(OUT_DIR, "frequency_sweep.wav"), samples)


if __name__ == "__main__":
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"Generating synthetic audio samples to: {OUT_DIR}\n")

    piano_arpeggio()
    jazz_walking_bass()
    electronic_drums()
    speech_synthetic()
    rain_ambient()
    orchestral_hit()
    guitar_pluck()
    frequency_sweep()

    print(f"\nDone! {len(os.listdir(OUT_DIR))} files generated.")