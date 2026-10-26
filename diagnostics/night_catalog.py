"""Offline real-catalog replay helpers; never part of the production package."""
from datetime import datetime, timezone
from decimal import Decimal
import math


def exif_exposure(exif):
    """EXIF clock is an unqualified midpoint assumption, not a clock calibration."""
    stamp = datetime.strptime(exif['DateTimeOriginal'] + exif['OffsetTimeOriginal'],
                              '%Y:%m:%d %H:%M:%S%z')
    seconds = int((stamp.astimezone(timezone.utc) - datetime(1970, 1, 1, tzinfo=timezone.utc)).total_seconds())
    subsec = str(exif.get('SubsecTimeOriginal', '0')).strip()
    if not subsec.isascii() or not subsec.isdigit() or len(subsec) > 9:
        raise ValueError('invalid EXIF subsecond precision')
    ns = int(subsec.ljust(9, '0'))
    value = exif['ExposureTime']
    duration = float(Decimal(value[0]) / Decimal(value[1])) if isinstance(value, tuple) else float(value)
    if not math.isfinite(duration) or duration <= 0:
        raise ValueError('invalid exposure duration')
    return {'midpoint': {'unix_seconds': str(seconds), 'nanoseconds': ns},
            'duration_s': duration, 'midpoint_sigma_s': None}


def pixel_request(frame_id, points, width, height, exposure, intrinsics, observer):
    """Explicit experimental assumptions; no synthetic fixture or IMU prior."""
    revision = 'unqualified-legacy-sidecar-optics'
    return {
        'observation': {'width': width, 'height': height,
            'identity': {'observation_id': frame_id, 'camera_configuration_id': 'legacy-dng',
                'calibration_revision': revision, 'exposure': exposure,
                'temporal_model': 'stationary_single_exposure',
                'timing_provenance_id': 'EXIF-unqualified-midpoint'},
            'stars': [{'id': str(i+1), 'pixel': {'x': float(x), 'y': float(y)},
                       'centroid_covariance_px2': None, 'flux': None}
                      for i, (x, y) in enumerate(points)]},
        'calibration': {'revision': revision, 'camera_configuration_id': 'legacy-dng',
            'width': width, 'height': height,
            'model': dict(fx_px=intrinsics['fx'], fy_px=intrinsics['fy'],
                          cx_px=intrinsics['cx'], cy_px=intrinsics['cy'],
                          k1=0, k2=0, k3=0, p1=0, p2=0),
            'valid_region': {'min_x': 0, 'min_y': 0, 'max_x': width-1, 'max_y': height-1}},
        'context': {'observer': observer, 'eop_policy': 'observed_only',
                    'refraction': {'kind': 'disabled'}},
        'options': {'acceptance': {'minimum_inliers': 6,
            'maximum_match_error_rad': math.radians(180/3600),
            'maximum_fit_rms_rad': math.radians(60/3600),
            'maximum_conditional_sigma_rad': None},
            'limits': {'maximum_hypotheses': 50000,
                       'maximum_candidate_comparisons': 2000000,
                       'maximum_workspace_bytes': 67108864}}}


def acceleration_comparison(acceleration, accepted):
    """Do not invent a tilt residual from absent extrinsics or an absent solve."""
    from diagnostics.sky_inputs import acceleration_summary
    result = acceleration_summary(acceleration)
    result.update(star_solution_available=accepted, tilt_residual_deg=None,
                  yaw_residual_deg=None, qualified=False,
                  reasons=['camera_to_sensor_axes_unqualified', 'sensor_sample_time_unknown',
                           'specific_force_not_separated_from_gravity', 'gravity_does_not_observe_yaw'])
    if not accepted:
        result['reasons'].insert(0, 'no_accepted_star_solution')
    return result


def relative_gravity_comparison(rotations, accelerations, nominal_interval_s):
    """Axis-invariant consistency bound, conditional on static, co-timed gravity.

    R maps raw camera vectors into CIRS. A fixed unknown orthogonal sensor/camera
    mapping preserves vector angles; gravity's angle cannot exceed the camera's
    total rotation plus Earth/frame rotation. This does not identify that mapping
    or distinguish timing, dynamic acceleration, optical error and sensor error.
    """
    from diagnostics.sky_inputs import acceleration_summary
    if len(rotations) != 2 or len(accelerations) != 2:
        raise ValueError('two paired observations required')
    if not math.isfinite(nominal_interval_s) or not 0 <= nominal_interval_s <= 60:
        raise ValueError('short nominal interval required')
    for r in rotations:
        if len(r) != 3 or any(len(row) != 3 for row in r):
            raise ValueError('rotation must be 3x3')
        if any(not math.isfinite(v) for row in r for v in row):
            raise ValueError('nonfinite rotation')
        if any(abs(sum(r[k][i]*r[k][j] for k in range(3)) - (i == j)) > 1e-6
               for i in range(3) for j in range(3)):
            raise ValueError('nonorthogonal rotation')
        det = sum(r[0][i]*(r[1][(i+1)%3]*r[2][(i+2)%3]-r[1][(i+2)%3]*r[2][(i+1)%3]) for i in range(3))
        if abs(det-1) > 1e-6: raise ValueError('reflection is not an attitude')
    angle = lambda c: math.degrees(math.acos(max(-1, min(1, c))))
    trace = sum(rotations[0][i][j]*rotations[1][i][j] for i in range(3) for j in range(3))
    optical = angle((trace-1)/2)
    a, b = [acceleration_summary(v)['unit_vector'] for v in accelerations]
    gravity = angle(sum(x*y for x, y in zip(a, b)))
    # Conservative nominal allowance above Earth's 7.292115e-5 rad/s rotation;
    # unknown sensor sample times prevent treating it as a measured error bound.
    earth = math.degrees(7.4e-5*nominal_interval_s)
    return {'optical_rotation_deg': optical, 'acceleration_direction_change_deg': gravity,
            'nominal_earth_frame_allowance_deg': earth,
            'conditional_minimum_discrepancy_deg': max(0, gravity-optical-earth),
            'qualified': False, 'absolute_tilt_error_deg': None, 'yaw_error_deg': None,
            'interpretation': 'conditional fixed-extrinsic, co-timed static-gravity consistency only; not an accuracy measurement'}
