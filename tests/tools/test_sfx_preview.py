"""The library's audio preview on hand-written logs, as `make sfx-preview` runs it.

One key sound gives a WAV of the right length. A buffer the dumped tables
do not hold gives the warning that stops the target.
"""
import os
import re
import subprocess
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CORE = os.environ.get("ROBCO_CORE", os.path.join(ROOT, "external", "robco-core"))
TOOLS = os.path.join(CORE, "tools")
MODEL = os.path.join(ROOT, "src", "sfx", "sfx_models.json")
OUT_RATE = 37500
TAIL_S = 0.3
START_MS = 100


def header(name):
    """(len, crc) of one table, from the generated header."""
    with open(os.path.join(ROOT, "src", "sfx", "sfx_table.h")) as f:
        h = f.read()
    up = name.upper()
    n = int(re.search(rf"#define UOS_SFX_{up}_LEN (\d+)u", h).group(1))
    crc = re.search(rf"#define UOS_SFX_{up}_CRC32 0x([0-9a-f]{{8}})u", h).group(1)
    return n, crc


def tool(name, *args):
    return subprocess.run([sys.executable, "-I", os.path.join(TOOLS, name), *args],
                          capture_output=True, text=True, timeout=150)


def render(tmp_path, n, crc):
    pcm = tmp_path / "pcm"
    r = tool("sfx_table.py", MODEL, "--dump", str(pcm))
    assert r.returncode == 0, r.stdout + r.stderr
    log = tmp_path / "serial.log"
    end_ms = START_MS + n * 1000 // OUT_RATE
    log.write_text(
        f"@RC stage=uos ev=audio op=pcm rate={OUT_RATE} len={n} vol=80 crc={crc} t_ms={START_MS}\n"
        f"@RC stage=uos ev=audio op=pcm_end n={n} last={n - 1} t_ms={end_ms}\n")
    out = tmp_path / "out.wav"
    r = tool("audio_preview.py", str(log), str(out), "--pcm", str(pcm))
    assert r.returncode == 0, r.stdout + r.stderr
    return out, r


def test_one_key_sound_gives_a_wav_of_the_right_length(tmp_path):
    n, crc = header("key_a")
    out, r = render(tmp_path, n, crc)
    assert "warning:" not in r.stderr, r.stderr
    with wave.open(str(out)) as w:
        assert (w.getframerate(), w.getnchannels(), w.getsampwidth()) == (OUT_RATE, 1, 2)
        frames = w.getnframes()
        data = w.readframes(frames)
    # The play ends at its last sample, rounded up to a whole ms, then the tail.
    end_ms = START_MS + -(-n * 1000 // OUT_RATE)
    want = end_ms * OUT_RATE // 1000 + round(TAIL_S * OUT_RATE)
    assert frames == want, (frames, want)
    assert any(data), "the WAV is silent"


def test_unknown_buffer_warns(tmp_path):
    n, _ = header("key_a")
    _, r = render(tmp_path, n, "00000000")
    assert re.search(r"^audio_preview\.py: warning: no \.u8 file has crc=00000000", r.stderr,
                     re.M), r.stderr
