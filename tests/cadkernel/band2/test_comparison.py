# SPDX-License-Identifier: MPL-2.0
"""Ensure unordered band comparison cannot hide geometry or provenance errors."""
from copy import deepcopy
from pathlib import Path
import runpy
import unittest

compare=runpy.run_path(str(Path(__file__).with_name('verify.py')))['compare']


def baseline():
    return dict(length=10.,clone=1,
        stations=[(0.,0.,10.,0.,0,0.,10.,0.,10.)],
        edges=[(0.,1.,10.,1.,0,0.,10.,0.,10.),(10.,1.,0.,1.,0,10.,0.,10.,0.)])


class ComparisonTests(unittest.TestCase):
    def test_edge_order_is_unobservable(self):
        expected=baseline();actual=deepcopy(expected)
        actual['edges'].reverse()
        self.assertEqual(compare(actual,expected,10.,exact=True),31)

    def test_projection_bound_accounts_for_coordinate_rounding(self):
        expected=baseline();actual=deepcopy(expected)
        edge=list(actual['edges'][0]);edge[0]=edge[5]=edge[7]=1e-15
        actual['edges'][0]=tuple(edge)
        compare(actual,expected,10.)
        with self.assertRaises(AssertionError):compare(actual,expected,10.,exact=True)

    def test_geometry_direction_and_every_provenance_field_are_checked(self):
        expected=baseline()
        for field in range(9):
            with self.subTest(field=field):
                actual=deepcopy(expected);edge=list(actual['edges'][0]);edge[field]+=1
                actual['edges'][0]=tuple(edge)
                with self.assertRaises(AssertionError):compare(actual,expected,10.)
        actual=deepcopy(expected)
        actual['edges'][0]=(10.,1.,0.,1.,0,10.,0.,10.,0.)
        with self.assertRaises(AssertionError):compare(actual,expected,10.)

    def test_matching_cannot_reuse_the_same_expected_edge(self):
        expected=baseline();actual=deepcopy(expected)
        actual['edges'][1]=actual['edges'][0]
        with self.assertRaises(AssertionError):compare(actual,expected,10.)

    def test_stations_and_scalar_fields_remain_strict(self):
        expected=baseline()
        for key,value in [('length',11.),('clone',0)]:
            actual=deepcopy(expected);actual[key]=value
            with self.assertRaises(AssertionError):compare(actual,expected,10.)
        actual=deepcopy(expected);station=list(actual['stations'][0]);station[5]=1e-15
        actual['stations'][0]=tuple(station)
        with self.assertRaises(AssertionError):compare(actual,expected,10.)

    def test_collapsed_station_has_no_projection_allowance(self):
        expected=baseline()
        expected['stations']=[(0.,0.,0.,0.,0,0.,0.,0.,0.)]
        expected['edges']=[(0.,1.,0.,2.,0,0.,0.,0.,0.)]
        actual=deepcopy(expected);edge=list(actual['edges'][0]);edge[5]=1e-20
        actual['edges'][0]=tuple(edge)
        with self.assertRaises(AssertionError):compare(actual,expected,10.)


if __name__=='__main__':unittest.main()
