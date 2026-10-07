"""Bunk-inch go-to conversion. Mirrors request_goto_height in boat-lift.yaml.

Arm angle is not a uniform height scale. dh/dθ = -L·cos(θ), with θ in degrees
from horizontal: one degree near 0° (high on this lift) is about 0.9 in, and
one degree near the settled bottom (~50°) is about 0.55 in.
"""
from math import asin, cos, degrees, radians, sin
from pathlib import Path
import unittest

L = 51.0
DATUM = -13.0
ROOT = Path(__file__).resolve().parents[1]


def bunk_height(theta_deg, theta_down):
    return DATUM + L * (sin(radians(theta_down)) - sin(radians(theta_deg)))


def theta_for_height(height_in, theta_down):
    s = sin(radians(theta_down)) - (height_in - DATUM) / L
    if s < -1.0 or s > 1.0:
        raise ValueError('outside the arm')
    return degrees(asin(s))


def inches_per_degree(theta_deg):
    return L * abs(cos(radians(theta_deg))) * 3.141592653589793 / 180.0


def height_target_percent(height_in, theta_down, theta_up, theta_empty=None):
    theta = theta_for_height(height_in, theta_down)
    angle_top = theta_up if theta_empty is None else min(theta_up, theta_empty)
    if theta > theta_down + 0.05 or theta < angle_top - 0.05:
        raise ValueError('outside calibrated stroke')
    span = theta_up - theta_down
    return (theta - theta_down) / span * 100.0


class BunkInches(unittest.TestCase):
    def test_round_trip(self):
        for theta_down, theta in ((52.0, 52.0), (52.0, 46.0), (50.0, 0.0), (50.0, -20.0)):
            recovered = theta_for_height(bunk_height(theta, theta_down), theta_down)
            self.assertAlmostEqual(recovered, theta, places=6)

    def test_same_two_inches_is_not_a_fixed_angle(self):
        bottom = 52.0 - theta_for_height(bunk_height(52.0, 52.0) + 2.0, 52.0)
        high = 0.0 - theta_for_height(bunk_height(0.0, 52.0) + 2.0, 52.0)
        self.assertAlmostEqual(bottom, 3.52, delta=0.05)
        self.assertAlmostEqual(high, 2.25, delta=0.05)
        self.assertGreater(bottom - high, 1.0)

    def test_this_sensors_zero_is_the_steep_part_of_the_arc(self):
        self.assertAlmostEqual(inches_per_degree(0.0), 0.89, delta=0.02)
        self.assertAlmostEqual(inches_per_degree(50.0), 0.57, delta=0.02)
        self.assertGreater(inches_per_degree(0.0), inches_per_degree(50.0))

    def test_percent_uses_the_lift_span_not_a_linear_inch_map(self):
        # 2 in above a 52° lowered capture, Lift at -5°.
        pct = height_target_percent(-11.0, 52.0, -5.0)
        self.assertAlmostEqual(pct, 6.2, delta=0.2)

    def test_refuses_above_the_captured_ceiling(self):
        ceiling = bunk_height(-21.0, 52.0)
        with self.assertRaises(ValueError):
            height_target_percent(ceiling + 2.0, 52.0, -5.0, -21.0)

    def test_firmware_inverts_with_asin(self):
        text = (ROOT / 'boat-lift.yaml').read_text(encoding='utf-8')
        body = text.split('id: request_goto_height', 1)[1].split('id: request_stop', 1)[0]
        self.assertIn('asinf', body)
        self.assertIn('sinf(id(cal_angle_down)', body)
        self.assertIn('${arm_length_in}', body)
        self.assertIn('${bunk_lowered_waterline_in}', body)


if __name__ == '__main__':
    unittest.main()
