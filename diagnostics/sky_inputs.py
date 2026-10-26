"""Offline sky-fixture diagnostics, deliberately excluded from production packages.

Acceleration is a device-frame diagnostic, never a camera attitude or heading.
Apple MakerNote and CoreMotion vectors retain distinct source conventions.
"""
import bisect
import math
import struct


def apple_acceleration(note):
    """Read only Apple iOS MakerNote tag 8; offsets are relative to the note.

    The note's endian marker overrides the enclosing TIFF's byte order. This
    allows a little-endian TIFF to contain a big-endian note.
    No unrelated MakerNote fields are returned or retained.
    """
    if not isinstance(note, bytes) or not 16 <= len(note) <= 65536:
        raise ValueError('invalid MakerNote size')
    if note[:12] != b'Apple iOS\0\0\1' or note[12:14] not in (b'MM', b'II'):
        raise ValueError('unsupported Apple MakerNote header')
    endian = '>' if note[12:14] == b'MM' else '<'
    count = struct.unpack_from(endian + 'H', note, 14)[0]
    end = 16 + 12 * count
    if count > 512 or end + 4 > len(note):
        raise ValueError('truncated MakerNote directory')
    matches = []
    for offset in range(16, end, 12):
        tag, kind, length, pointer = struct.unpack_from(endian + 'HHII', note, offset)
        if tag != 8:
            continue
        if kind != 10 or length != 3 or pointer < end + 4 or pointer + 24 > len(note):
            raise ValueError('invalid acceleration entry')
        pairs = struct.unpack_from(endian + 'iiiiii', note, pointer)
        if any(pairs[i] == 0 for i in (1, 3, 5)):
            raise ValueError('zero acceleration denominator')
        matches.append(tuple(pairs[i] / pairs[i + 1] for i in (0, 2, 4)))
    if len(matches) != 1:
        raise ValueError('missing or duplicate acceleration entry')
    return matches[0]


def acceleration_summary(vector):
    if len(vector) != 3 or any(type(v) not in (int, float) or not math.isfinite(v)
                               for v in vector):
        raise ValueError('invalid acceleration vector')
    norm = math.sqrt(sum(v * v for v in vector))
    if not math.isfinite(norm) or norm < 1e-12:
        raise ValueError('unusable acceleration norm')
    return {'norm_g': norm, 'unit_vector': [v / norm for v in vector],
            'near_one_g': 0.8 <= norm <= 1.2,
            'heading_rad': None, 'camera_attitude': None,
            'exposure_alignment_qualified': False}


def interpolate_acceleration(samples, time_s=0.0, maximum_gap_s=0.05):
    """Interpolate a declared common-monotonic-clock diagnostic; no extrapolation.

    Samples contain relative sensor time and acceleration_g only. Callback/UTC
    timestamps must never be substituted for sensor timestamps by the caller.
    Bracketing does not qualify exposure alignment or separate gravity from motion.
    """
    if not 2 <= len(samples) <= 10000 or not math.isfinite(time_s):
        raise ValueError('invalid sample window')
    if not math.isfinite(maximum_gap_s) or maximum_gap_s <= 0:
        raise ValueError('invalid gap limit')
    times = [s['relative_sensor_time_s'] for s in samples]
    if any(type(t) not in (int, float) or not math.isfinite(t) for t in times):
        raise ValueError('invalid sensor time')
    if any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError('sensor times must strictly increase')
    for s in samples:
        acceleration_summary(s['acceleration_g'])
    i = bisect.bisect_left(times, time_s)
    if i < len(times) and times[i] == time_s:
        return list(samples[i]['acceleration_g'])
    if i == 0 or i == len(times) or times[i] - times[i - 1] > maximum_gap_s:
        raise ValueError('unbracketed or stale acceleration')
    alpha = (time_s - times[i - 1]) / (times[i] - times[i - 1])
    return [(1 - alpha) * a + alpha * b for a, b in
            zip(samples[i - 1]['acceleration_g'], samples[i]['acceleration_g'])]


def display_pixel(x, y, width, height, orientation):
    """EXIF display transform of integer-centered raw pixels; not an IMU transform."""
    if type(width) is not int or type(height) is not int or width <= 0 or height <= 0:
        raise ValueError('invalid image dimensions')
    if type(orientation) is not int or orientation not in range(1, 9):
        raise ValueError('unsupported EXIF orientation')
    if not (math.isfinite(x) and math.isfinite(y) and 0 <= x <= width - 1 and 0 <= y <= height - 1):
        raise ValueError('pixel outside image')
    return ((x, y), (width - 1 - x, y), (width - 1 - x, height - 1 - y),
            (x, height - 1 - y), (y, x), (height - 1 - y, x),
            (height - 1 - y, width - 1 - x), (y, width - 1 - x))[orientation - 1]
