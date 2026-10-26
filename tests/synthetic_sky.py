"""Independent procedural controls. No recorded inputs or catalog associations.

All values below come from algebra, integer sequences, or Random(1729).
MakerNote expectations are chosen rational vectors, not output from a parser.
"""
import hashlib
import random
import struct
from diagnostics.sky_inputs import acceleration_summary
SEED = 1729

def corpus():
    rng = random.Random(SEED)
    frames = []
    vectors = {}
    for i in range(16):
        identifier = f'synthetic-{i:02d}'
        width, height = 640, 480
        vector = [0.6, 0.0, 0.8] if i % 2 == 0 else [0.0, -1.0, 0.0]
        count = 4 if i % 4 == 0 else 10
        candidates = [{'id': str(k+1), 'pixel': {'x': rng.uniform(10, 630), 'y': rng.uniform(10, 470)}, 'flux': float(100-k), 'centroid_covariance_px2': None} for k in range(count)]
        acceleration = {'vector_g': vector, 'summary': acceleration_summary(vector)}
        scene = 'synthetic_note' if i < 8 else 'synthetic_sensor'
        if i < 8:
            pairs = [v for x in vector for v in (int(x*10), 10)]
            note = b'Apple iOS\0\0\1MM' + struct.pack('>HHHII', 1, 8, 10, 3, 32) + b'\0'*4 + struct.pack('>iiiiii', *pairs)
            acceleration['tag_only_note_hex'] = note.hex()
            vectors[identifier] = vector
        else:
            times = [-.04, -.02, -.01, .01, .02, .04]
            acceleration.update(samples=[{'relative_sensor_time_s': t, 'acceleration_g': vector, 'source_line': j+1} for j,t in enumerate(times)], sample_timing='synthetic_unqualified_exposure')
        frames.append({'id': identifier, 'scene': scene, 'physical_attitude_truth': None, 'callback_minus_pts_s': .1, 'acceleration': acceleration,
            'image': {'width': width, 'height': height, 'orientation': i%8+1, 'candidates': candidates, 'sha256': hashlib.sha256(identifier.encode()).hexdigest()}})
    return {'origin': 'independent-procedural-synthetic', 'seed': SEED, 'frames': frames}, {'vectors': vectors}
