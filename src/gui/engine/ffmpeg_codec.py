"""FFmpeg video compression codec wrapper (CLI-based).

Compresses/decompresses raw video (16-byte header + RGB24 frame data)
using FFmpeg via subprocess.  FFmpeg binaries are searched in this order:
  1. Bundled in ``{project_root}/ffmpeg/`` (or ``Package/ffmpeg/``)
  2. Next to the running Python executable
  3. System ``PATH``

Raw video format (compatible with C++ VideoCodec.hpp):
  [width:4B][height:4B][fps:4B][num_frames:4B][RGB24 frame data...]
"""

from __future__ import annotations

import json
import logging
import os
import shutil
import struct
import subprocess
import tempfile
import time
from pathlib import Path

logger = logging.getLogger(__name__)

_CODEC_INFO = {
    "h264": {"lib": "libx264", "format": "h264"},
    "hevc": {"lib": "libx265", "format": "hevc"},
}

_FFMPEG_TIMEOUT = 600

# ── Bundled binary lookup ──

_FFMPEG_PATH: str | None = None
_FFPROBE_PATH: str | None = None


def _find_bundled_binary(name: str) -> str | None:
    """Search for ``name``.exe in bundled locations, then PATH."""
    exe = f"{name}.exe"

    script_dir = Path(__file__).resolve().parent
    candidates = [
        script_dir / ".." / ".." / ".." / ".." / "ffmpeg" / exe,
        script_dir / ".." / ".." / ".." / ".." / "Package" / "ffmpeg" / exe,
        script_dir / ".." / ".." / ".." / "ffmpeg" / exe,
        script_dir / "ffmpeg" / exe,
    ]

    for c in candidates:
        resolved = c.resolve()
        if resolved.is_file():
            logger.debug("Found bundled %s at %s", name, resolved)
            return str(resolved)

    path_result = shutil.which(name)
    if path_result:
        logger.debug("Found system %s at %s", name, path_result)
        return path_result

    logger.debug("%s not found (searched bundled dirs + PATH)", name)
    return None


def _get_ffmpeg_path() -> str | None:
    global _FFMPEG_PATH
    if _FFMPEG_PATH is None:
        _FFMPEG_PATH = _find_bundled_binary("ffmpeg")
    return _FFMPEG_PATH


def _get_ffprobe_path() -> str | None:
    global _FFPROBE_PATH
    if _FFPROBE_PATH is None:
        _FFPROBE_PATH = _find_bundled_binary("ffprobe")
    return _FFPROBE_PATH


class VideoFFmpegCompressor:
    _PRESET_MAP: dict[int, str] = {
        0: "ultrafast", 1: "fast", 2: "medium", 3: "slow", 4: "veryslow",
    }

    def __init__(self, codec: str = "h264", quality: int = 23, preset: str | int = "medium"):
        if codec not in _CODEC_INFO:
            raise ValueError(f"Unsupported codec: {codec}, choose from {list(_CODEC_INFO.keys())}")
        self.codec = codec
        self.quality = max(0, min(51, quality))
        if isinstance(preset, int):
            self.preset = self._PRESET_MAP.get(preset, "medium")
        else:
            self.preset = preset

    # ── Raw video header helpers ──

    @staticmethod
    def _parse_raw_header(data: bytes) -> dict:
        if len(data) < 16:
            raise ValueError("Raw video data too short: need 16-byte header")
        w, h, fps, num_frames = struct.unpack_from("<IIII", data, 0)
        if w <= 0 or h <= 0:
            raise ValueError(f"Invalid video dimensions: {w}x{h}")
        return {"width": w, "height": h, "fps": fps, "num_frames": num_frames}

    @staticmethod
    def _build_raw_header(width: int, height: int, fps: int, num_frames: int) -> bytes:
        return struct.pack("<IIII", width, height, fps, num_frames)

    # ── FFmpeg availability ──

    @staticmethod
    def ffmpeg_available() -> bool:
        return _get_ffmpeg_path() is not None

    @staticmethod
    def ffprobe_available() -> bool:
        return _get_ffprobe_path() is not None

    @staticmethod
    def bundled_ffmpeg_dir() -> str | None:
        """Return the directory containing the discovered ffmpeg.exe, or None."""
        p = _get_ffmpeg_path()
        return str(Path(p).parent) if p else None

    # ── Compression ──

    def compress(self, data: bytes) -> dict:
        t0 = time.perf_counter()
        ffmpeg_exe = _get_ffmpeg_path()

        if not ffmpeg_exe:
            return self._make_result(False, len(data), 0, t0,
                                     "FFmpeg not found — drop ffmpeg.exe + ffprobe.exe into the project's ffmpeg/ folder", b"")

        try:
            info = self._parse_raw_header(data)
        except ValueError as e:
            return self._make_result(False, len(data), 0, t0, str(e), b"")

        w, h, fps, num_frames = info["width"], info["height"], info["fps"], info["num_frames"]
        frame_data = data[16:]
        expected = w * h * 3 * num_frames
        if len(frame_data) < expected:
            return self._make_result(
                False, len(data), 0, t0,
                f"Frame data too short: {len(frame_data)} < {expected}", b"",
            )

        cinfo = _CODEC_INFO[self.codec]
        cmd = [
            ffmpeg_exe, "-y", "-loglevel", "error",
            "-f", "rawvideo",
            "-pixel_format", "rgb24",
            "-video_size", f"{w}x{h}",
            "-framerate", str(fps),
            "-i", "pipe:0",
            "-c:v", cinfo["lib"],
            "-crf", str(self.quality),
            "-preset", self.preset,
            "-pix_fmt", "yuv420p",
            "-frames:v", str(num_frames),
            "-an",
            "-f", cinfo["format"],
            "pipe:1",
        ]

        try:
            proc = subprocess.Popen(
                cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            out, err = proc.communicate(input=frame_data, timeout=_FFMPEG_TIMEOUT)
        except subprocess.TimeoutExpired:
            proc.kill()
            return self._make_result(False, len(data), 0, t0, "ffmpeg encode timed out", b"")

        if proc.returncode != 0:
            err_msg = err.decode("utf-8", errors="replace")[:300]
            return self._make_result(False, len(data), 0, t0, f"ffmpeg encode error: {err_msg}", b"")

        return self._make_result(True, len(data), len(out), t0, "", out)

    # ── Decompression ──

    def decompress(self, data: bytes) -> dict:
        t0 = time.perf_counter()
        ffmpeg_exe = _get_ffmpeg_path()
        ffprobe_exe = _get_ffprobe_path()

        missing = []
        if not ffmpeg_exe:
            missing.append("ffmpeg")
        if not ffprobe_exe:
            missing.append("ffprobe")
        if missing:
            return self._make_result(
                False, len(data), 0, t0,
                f"{' and '.join(missing)} not found — drop them into the project's ffmpeg/ folder", b"",
            )

        if len(data) < 8:
            return self._make_result(False, len(data), 0, t0, "Compressed data too short", b"")

        ext = ".h264" if self.codec == "h264" else ".hevc"
        tmp_path = None
        try:
            with tempfile.NamedTemporaryFile(delete=False, suffix=ext) as tmp:
                tmp.write(data)
                tmp_path = tmp.name

            probe_cmd = [
                ffprobe_exe, "-v", "error",
                "-select_streams", "v:0",
                "-show_entries", "stream=width,height,r_frame_rate,nb_frames",
                "-of", "json",
                tmp_path,
            ]
            probe_result = subprocess.run(probe_cmd, capture_output=True, text=True, timeout=30)

            if probe_result.returncode != 0:
                return self._make_result(False, len(data), 0, t0,
                                         f"ffprobe error: {probe_result.stderr[:200]}", b"")

            probe_data = json.loads(probe_result.stdout)
            streams = probe_data.get("streams", [])
            if not streams:
                return self._make_result(False, len(data), 0, t0,
                                         "ffprobe: no video stream found", b"")

            stream = streams[0]
            w = int(stream.get("width", 0))
            h = int(stream.get("height", 0))
            fps_str = stream.get("r_frame_rate", "30/1")
            nb_str = stream.get("nb_frames", None)

            if w <= 0 or h <= 0:
                return self._make_result(False, len(data), 0, t0,
                                         "ffprobe returned invalid dimensions", b"")

            if "/" in fps_str:
                num, den = fps_str.split("/")
                fps = int(num) // max(int(den), 1)
            else:
                fps = int(fps_str)

            decode_cmd = [
                ffmpeg_exe, "-y", "-loglevel", "error",
                "-i", tmp_path,
                "-f", "rawvideo",
                "-pixel_format", "rgb24",
                "-an",
                "pipe:1",
            ]
            decode_proc = subprocess.Popen(
                decode_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            raw_out, raw_err = decode_proc.communicate(timeout=_FFMPEG_TIMEOUT)

            if decode_proc.returncode != 0:
                err_msg = raw_err.decode("utf-8", errors="replace")[:300]
                return self._make_result(False, len(data), 0, t0,
                                         f"ffmpeg decode error: {err_msg}", b"")

            frame_size = w * h * 3
            num_frames = int(nb_str) if nb_str else (len(raw_out) // frame_size if frame_size > 0 else 0)

            header = self._build_raw_header(w, h, fps, num_frames)
            result = header + raw_out
            return self._make_result(True, len(data), len(result), t0, "", result)

        except subprocess.TimeoutExpired:
            return self._make_result(False, len(data), 0, t0,
                                     "ffmpeg/ffprobe timed out", b"")
        except Exception as e:
            return self._make_result(False, len(data), 0, t0, str(e), b"")
        finally:
            if tmp_path:
                try:
                    os.unlink(tmp_path)
                except OSError:
                    pass

    # ── Result helper ──

    @staticmethod
    def _make_result(
        success: bool,
        original_size: int,
        compressed_size: int,
        t0: float,
        error: str,
        data: bytes,
    ) -> dict:
        ms = (time.perf_counter() - t0) * 1000.0
        return {
            "success": success,
            "original_size": original_size,
            "compressed_size": compressed_size,
            "compression_ratio": (compressed_size / original_size) if original_size > 0 else 0.0,
            "time_ms": ms,
            "error_message": error or ("OK" if success else "Compression failed"),
            "data": data,
        }