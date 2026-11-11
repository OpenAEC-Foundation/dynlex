struct type_7 {
    member: vec2<f32>,
}

struct type_11 {
    member: f32,
}

var<private> gl_FragCoord_1: vec4<f32>;
var<private> gl_FragColor: vec4<f32>;
@group(0) @binding(0) 
var<uniform> ubo_time: type_11;
@group(0) @binding(2) 
var<uniform> ubo_width: type_11;
@group(0) @binding(3) 
var<uniform> ubo_height: type_11;

fn the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(point: ptr<function, type_7>, phase: ptr<function, f32>) -> f32 {
    let _e175 = (*point).member;
    let _e181 = (*phase);
    let _e191 = (_e175.x + (sin((_e181 + ((_e175.x * 0.73f) - (_e175.y * 0.41f)))) * 0.58f));
    let _e193 = (_e175.y + (cos((((_e175.x * 0.37f) + (_e175.y * 0.89f)) - (_e181 * 0.71f))) * 0.58f));
    let _e199 = sin(((_e181 * 0.43f) + ((_e191 * 1.31f) + (_e193 * 0.87f))));
    let _e205 = cos((((_e193 * 1.73f) - (_e191 * 0.79f)) - (_e181 * 0.31f)));
    return (((_e199 * 0.46f) + (_e205 * 0.29f)) + ((sin((((_e191 * 2.47f) - (_e193 * 2.11f)) + (_e199 * 1.8f))) * 0.17f) + (cos((((_e191 * 4.03f) + (_e193 * 3.17f)) + (_e205 * 1.4f))) * 0.08f)));
}

fn the_spark_field_at_3a_point8point5_during_3a_value8phase5_definition_b45b4d0f369762d8ebfe5c0c7b634d3bb84e995046b3c28d0d2c522bd033e42e_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(point_1: ptr<function, type_7>, phase_1: ptr<function, f32>) -> f32 {
    var tmp77_: f32;
    var tmp39_: f32;
    var location: type_7;
    var shifted: type_7;
    var sample: type_7;
    var phi_944_: u32;
    var phi_946_: f32;

    let _e180 = (*point_1).member;
    let _e185 = vec2<f32>((_e180.x * 0.19f), vec2<f32>().y);
    sample.member = vec2<f32>(_e185.x, (_e180.y * 0.19f));
    let _e192 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&sample), phase_1);
    let _e197 = vec2<f32>(((_e180.x * 0.23f) + 7f), vec2<f32>().y);
    shifted.member = vec2<f32>(_e197.x, ((_e180.y * 0.23f) + -5f));
    let _e205 = (_e180.x + (_e192 * 1.7f));
    let _e206 = (*phase_1);
    tmp39_ = (_e206 + 1.7f);
    let _e208 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&shifted), (&tmp39_));
    let _e210 = (_e180.y + (_e208 * 1.7f));
    let _e214 = vec2<f32>((_e205 * 0.11f), vec2<f32>().y);
    location.member = vec2<f32>(_e214.x, (_e210 * 0.11f));
    tmp77_ = (_e206 + 4f);
    let _e221 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&location), (&tmp77_));
    let _e225 = ((((_e221 * 0.5f) + 0.5f) + -0.48f) / 0.34f);
    if (_e225 < 0f) {
        phi_946_ = 0f;
    } else {
        phi_944_ = 0u;
        if (_e225 > 1f) {
        } else {
            phi_944_ = 1u;
        }
        let _e229 = phi_944_;
        phi_946_ = _e225;
        if (0u == _e229) {
            phi_946_ = 1f;
        }
    }
    let _e232 = phi_946_;
    let _e237 = sin((_e206 + ((_e205 * 1.73f) + (_e210 * 0.31f))));
    let _e244 = sin((((_e210 * 1.91f) - (_e205 * 0.27f)) - (_e206 * 0.83f)));
    let _e248 = (sqrt(((_e237 * _e237) + (_e244 * _e244))) / 0.115f);
    let _e250 = select(_e248, 1f, (_e248 > 1f));
    return ((1f - ((_e250 * _e250) * (3f - (_e250 * 2f)))) * ((_e232 * _e232) * (3f - (_e232 * 2f))));
}

fn a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3floating5point_number8scale5_during_3floating5point_number8phase5_at_3floating5point_number8time5_definition_c5856d2c5efd026e505778d50574371148e52e90ec263059ef5737eecfdfcf6b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(position: ptr<function, type_7>, scale: ptr<function, f32>, phase_2: ptr<function, f32>, time: ptr<function, f32>) -> f32 {
    var tmp62_: f32;
    var tmp58_: type_7;
    var tmp45_: f32;
    var tmp41_: type_7;
    var tmp28_: f32;
    var tmp26_: type_7;

    let _e182 = (*time);
    let _e184 = (*phase_2);
    let _e185 = ((_e182 * 0.13f) + _e184);
    let _e187 = (_e185 - floor(_e185));
    let _e190 = (((_e187 * _e187) * 3.6f) + 0.42f);
    let _e192 = (*position).member;
    let _e195 = (*scale);
    let _e196 = (_e195 * (_e192.x / _e190));
    let _e199 = vec2<f32>(_e196, vec2<f32>(f32(), 0f).y);
    let _e202 = (_e195 * (_e192.y / _e190));
    tmp26_.member = vec2<f32>(_e199.x, _e202);
    let _e207 = (_e184 * 13.7f);
    tmp28_ = _e207;
    let _e208 = the_spark_field_at_3a_point8point5_during_3a_value8phase5_definition_b45b4d0f369762d8ebfe5c0c7b634d3bb84e995046b3c28d0d2c522bd033e42e_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp26_), (&tmp28_));
    let _e212 = vec2<f32>((_e196 * 0.33f), vec2<f32>().y);
    tmp41_.member = vec2<f32>(_e212.x, _e202);
    tmp45_ = (_e207 + 0.09f);
    let _e218 = the_spark_field_at_3a_point8point5_during_3a_value8phase5_definition_b45b4d0f369762d8ebfe5c0c7b634d3bb84e995046b3c28d0d2c522bd033e42e_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp41_), (&tmp45_));
    tmp58_.member = vec2<f32>(_e199.x, (_e202 * 0.33f));
    tmp62_ = (_e207 + -0.11f);
    let _e225 = the_spark_field_at_3a_point8point5_during_3a_value8phase5_definition_b45b4d0f369762d8ebfe5c0c7b634d3bb84e995046b3c28d0d2c522bd033e42e_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp58_), (&tmp62_));
    let _e231 = (sqrt(((_e192.x * _e192.x) + (_e192.y * _e192.y))) / 1.35f);
    let _e233 = select(_e231, 1f, (_e231 > 1f));
    return (((sin(((_e182 * 2.7f) + (_e184 * 29f))) * 0.13f) + 0.87f) * (((_e187 * 1.25f) + 0.28f) * ((_e208 * 2.8f) + (((_e218 + _e225) * (1f - ((_e233 * _e233) * (3f - (_e233 * 2f))))) * 0.17f))));
}

fn main_1() {
    var tmp593_: f32;
    var tmp592_: type_7;
    var tmp446_: f32;
    var tmp445_: type_7;
    var tmp431_: f32;
    var tmp430_: type_7;
    var tmp308_: f32;
    var tmp307_: type_7;
    var tmp284_: f32;
    var tmp283_: f32;
    var tmp281_: f32;
    var tmp280_: f32;
    var tmp278_: f32;
    var tmp277_: f32;
    var tmp220_: f32;
    var tmp218_: type_7;
    var tmp199_: f32;
    var tmp198_: type_7;
    var tmp178_: f32;
    var tmp177_: type_7;
    var universe: type_7;
    var time_1: f32;
    var phi_395_: u32;
    var phi_397_: f32;
    var phi_402_: u32;
    var phi_404_: f32;
    var phi_435_: u32;
    var phi_437_: f32;
    var phi_442_: u32;
    var phi_444_: f32;
    var phi_465_: u32;
    var phi_467_: f32;
    var phi_474_: u32;
    var phi_476_: f32;
    var phi_482_: u32;
    var phi_484_: f32;
    var phi_490_: u32;
    var phi_492_: f32;
    var phi_505_: u32;
    var phi_507_: f32;
    var phi_512_: u32;
    var phi_514_: f32;
    var phi_521_: u32;
    var phi_523_: f32;
    var phi_530_: u32;
    var phi_532_: f32;
    var phi_542_: u32;
    var phi_544_: f32;

    let _e194 = gl_FragCoord_1;
    let _e196 = gl_FragCoord_1;
    let _e199 = ubo_time.member;
    time_1 = _e199;
    let _e201 = ubo_width.member;
    let _e202 = max(_e201, 1f);
    let _e204 = ubo_height.member;
    let _e205 = max(_e204, 1f);
    let _e206 = (_e202 / _e205);
    let _e210 = (_e206 * (((_e194.x / _e202) * 2f) + -1f));
    let _e213 = (((_e196.y / _e205) * 2f) + -1f);
    let _e218 = (((_e199 * 0.055f) + 0.62f) + ((_e199 * _e199) * 0.0018f));
    let _e226 = (((sin((_e199 * 0.27f)) * 0.048f) + _e210) / _e218);
    let _e228 = (((cos((_e199 * 0.21f)) * 0.026f) + _e213) / _e218);
    let _e232 = sqrt(((_e226 * _e226) + (_e228 * _e228)));
    let _e234 = (_e232 * _e232);
    let _e239 = min(((_e199 * 0.035f) + (0.19f / (_e234 + 0.021f))), 4.6f);
    let _e240 = sin(_e239);
    let _e241 = cos(_e239);
    let _e244 = ((0.12f / (_e234 + 0.012f)) + 1f);
    let _e248 = (_e244 * ((_e226 * _e241) - (_e228 * _e240)));
    let _e251 = vec2<f32>(_e248, vec2<f32>().y);
    let _e255 = (_e244 * ((_e226 * _e240) + (_e228 * _e241)));
    universe.member = vec2<f32>(_e251.x, _e255);
    let _e265 = vec2<f32>(((_e199 * 0.025f) + (_e248 * 1.8f)), vec2<f32>().y);
    tmp177_.member = vec2<f32>(_e265.x, ((_e255 * 1.8f) + -4f));
    tmp178_ = 1.7f;
    let _e272 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp177_), (&tmp178_));
    let _e274 = ((_e272 * 0.5f) + 0.5f);
    let _e279 = vec2<f32>(((_e248 * 4.6f) + -11f), vec2<f32>().y);
    tmp198_.member = vec2<f32>(_e279.x, ((_e199 * 0.018f) + (_e255 * 4.6f)));
    tmp199_ = 5.3f;
    let _e287 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp198_), (&tmp199_));
    let _e294 = vec2<f32>(((_e248 * 2.4f) + 7f), vec2<f32>().y);
    tmp218_.member = vec2<f32>(_e294.x, ((_e255 * 2.4f) + -9f));
    tmp220_ = (_e199 * 0.045f);
    let _e302 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp218_), (&tmp220_));
    let _e304 = ((_e274 + -0.45f) / 0.41000003f);
    if (_e304 < 0f) {
        phi_397_ = 0f;
    } else {
        phi_395_ = 0u;
        if (_e304 > 1f) {
        } else {
            phi_395_ = 1u;
        }
        let _e308 = phi_395_;
        phi_397_ = _e304;
        if (0u == _e308) {
            phi_397_ = 1f;
        }
    }
    let _e311 = phi_397_;
    let _e313 = ((((_e287 * 0.5f) + 0.5f) + -0.52f) / 0.38f);
    if (_e313 < 0f) {
        phi_404_ = 0f;
    } else {
        phi_402_ = 0u;
        if (_e313 > 1f) {
        } else {
            phi_402_ = 1u;
        }
        let _e317 = phi_402_;
        phi_404_ = _e313;
        if (0u == _e317) {
            phi_404_ = 1f;
        }
    }
    let _e320 = phi_404_;
    tmp277_ = 13f;
    tmp278_ = 0.09f;
    let _e321 = a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3floating5point_number8scale5_during_3floating5point_number8phase5_at_3floating5point_number8time5_definition_c5856d2c5efd026e505778d50574371148e52e90ec263059ef5737eecfdfcf6b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&universe), (&tmp277_), (&tmp278_), (&time_1));
    tmp280_ = 21f;
    tmp281_ = 0.41f;
    let _e322 = a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3floating5point_number8scale5_during_3floating5point_number8phase5_at_3floating5point_number8time5_definition_c5856d2c5efd026e505778d50574371148e52e90ec263059ef5737eecfdfcf6b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&universe), (&tmp280_), (&tmp281_), (&time_1));
    tmp283_ = 31f;
    tmp284_ = 0.77f;
    let _e323 = a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3floating5point_number8scale5_during_3floating5point_number8phase5_at_3floating5point_number8time5_definition_c5856d2c5efd026e505778d50574371148e52e90ec263059ef5737eecfdfcf6b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&universe), (&tmp283_), (&tmp284_), (&time_1));
    let _e327 = vec2<f32>((_e248 * 5.1f), vec2<f32>().y);
    tmp307_.member = vec2<f32>(_e327.x, (_e255 * 5.1f));
    tmp308_ = 8.2f;
    let _e333 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp307_), (&tmp308_));
    let _e337 = ((sin((_e199 * 0.17f)) * 0.018f) + 0.12f);
    let _e338 = sin(_e337);
    let _e339 = cos(_e337);
    let _e342 = ((_e339 * _e226) + (_e338 * _e228));
    let _e345 = ((_e339 * _e228) - (_e338 * _e226));
    let _e346 = (_e345 * 7.2f);
    let _e350 = sqrt(((_e342 * _e342) + (_e346 * _e346)));
    let _e352 = ((_e350 + -0.23f) / 0.054999992f);
    if (_e352 < 0f) {
        phi_437_ = 0f;
    } else {
        phi_435_ = 0u;
        if (_e352 > 1f) {
        } else {
            phi_435_ = 1u;
        }
        let _e356 = phi_435_;
        phi_437_ = _e352;
        if (0u == _e356) {
            phi_437_ = 1f;
        }
    }
    let _e359 = phi_437_;
    let _e361 = ((_e350 + -0.72f) / 0.39999998f);
    if (_e361 < 0f) {
        phi_444_ = 0f;
    } else {
        phi_442_ = 0u;
        if (_e361 > 1f) {
        } else {
            phi_442_ = 1u;
        }
        let _e365 = phi_442_;
        phi_444_ = _e361;
        if (0u == _e365) {
            phi_444_ = 1f;
        }
    }
    let _e368 = phi_444_;
    let _e374 = vec2<f32>(((_e199 * 0.52f) + (_e342 * 8.3f)), vec2<f32>().y);
    tmp430_.member = vec2<f32>(_e374.x, (_e346 * 1.8f));
    tmp431_ = 3.1f;
    let _e380 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp430_), (&tmp431_));
    let _e386 = vec2<f32>(((_e342 * 18f) - (_e199 * 0.91f)), vec2<f32>().y);
    tmp445_.member = vec2<f32>(_e386.x, (_e346 * 3.7f));
    tmp446_ = 7.9f;
    let _e392 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp445_), (&tmp446_));
    let _e394 = ((_e350 + -0.29f) / 0.53999996f);
    if (_e394 < 0f) {
        phi_467_ = 0f;
    } else {
        phi_465_ = 0u;
        if (_e394 > 1f) {
        } else {
            phi_465_ = 1u;
        }
        let _e398 = phi_465_;
        phi_467_ = _e394;
        if (0u == _e398) {
            phi_467_ = 1f;
        }
    }
    let _e401 = phi_467_;
    let _e405 = (((_e342 / max(_e350, 0.001f)) * 0.5f) + 0.5f);
    if (_e405 < 0f) {
        phi_476_ = 0f;
    } else {
        phi_474_ = 0u;
        if (_e405 > 1f) {
        } else {
            phi_474_ = 1u;
        }
        let _e409 = phi_474_;
        phi_476_ = _e405;
        if (0u == _e409) {
            phi_476_ = 1f;
        }
    }
    let _e412 = phi_476_;
    let _e413 = abs(_e226);
    let _e415 = ((_e413 + -0.018f) / 0.14199999f);
    if (_e415 < 0f) {
        phi_484_ = 0f;
    } else {
        phi_482_ = 0u;
        if (_e415 > 1f) {
        } else {
            phi_482_ = 1u;
        }
        let _e419 = phi_482_;
        phi_484_ = _e415;
        if (0u == _e419) {
            phi_484_ = 1f;
        }
    }
    let _e422 = phi_484_;
    let _e425 = ((abs(_e228) + -0.19f) / 0.53000003f);
    if (_e425 < 0f) {
        phi_492_ = 0f;
    } else {
        phi_490_ = 0u;
        if (_e425 > 1f) {
        } else {
            phi_490_ = 1u;
        }
        let _e429 = phi_490_;
        phi_492_ = _e425;
        if (0u == _e429) {
            phi_492_ = 1f;
        }
    }
    let _e432 = phi_492_;
    let _e436 = vec2<f32>((_e226 * 15f), vec2<f32>().y);
    tmp592_.member = vec2<f32>(_e436.x, ((_e228 * 3f) - (_e199 * 0.8f)));
    tmp593_ = 10f;
    let _e444 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b((&tmp592_), (&tmp593_));
    let _e446 = ((_e232 + -0.176f) / 0.026000008f);
    if (_e446 < 0f) {
        phi_507_ = 0f;
    } else {
        phi_505_ = 0u;
        if (_e446 > 1f) {
        } else {
            phi_505_ = 1u;
        }
        let _e450 = phi_505_;
        phi_507_ = _e446;
        if (0u == _e450) {
            phi_507_ = 1f;
        }
    }
    let _e453 = phi_507_;
    let _e455 = ((_e345 + 0.019f) / 0.033999998f);
    if (_e455 < 0f) {
        phi_514_ = 0f;
    } else {
        phi_512_ = 0u;
        if (_e455 > 1f) {
        } else {
            phi_512_ = 1u;
        }
        let _e459 = phi_512_;
        phi_514_ = _e455;
        if (0u == _e459) {
            phi_514_ = 1f;
        }
    }
    let _e462 = phi_514_;
    let _e466 = ((abs((_e232 + -0.216f)) + -0.001f) / 0.016f);
    if (_e466 < 0f) {
        phi_523_ = 0f;
    } else {
        phi_521_ = 0u;
        if (_e466 > 1f) {
        } else {
            phi_521_ = 1u;
        }
        let _e470 = phi_521_;
        phi_523_ = _e466;
        if (0u == _e470) {
            phi_523_ = 1f;
        }
    }
    let _e473 = phi_523_;
    let _e477 = ((abs((_e228 / max(_e232, 0.001f))) + -0.16f) / 0.70000005f);
    if (_e477 < 0f) {
        phi_532_ = 0f;
    } else {
        phi_530_ = 0u;
        if (_e477 > 1f) {
        } else {
            phi_530_ = 1u;
        }
        let _e481 = phi_530_;
        phi_532_ = _e477;
        if (0u == _e481) {
            phi_532_ = 1f;
        }
    }
    let _e484 = phi_532_;
    let _e485 = (_e210 / _e206);
    let _e491 = ((sqrt(((_e213 * _e213) + (_e485 * _e485))) + -0.48f) / 0.86f);
    if (_e491 < 0f) {
        phi_544_ = 0f;
    } else {
        phi_542_ = 0u;
        if (_e491 > 1f) {
        } else {
            phi_542_ = 1u;
        }
        let _e495 = phi_542_;
        phi_544_ = _e491;
        if (0u == _e495) {
            phi_544_ = 1f;
        }
    }
    let _e498 = phi_544_;
    let _e502 = ((_e311 * _e311) * (3f - (_e311 * 2f)));
    let _e508 = ((_e320 * _e320) * (3f - (_e320 * 2f)));
    let _e516 = ((sin(((_e274 * 8f) + ((_e248 * 2.7f) - (_e255 * 3.6f)))) * 0.5f) + 0.5f);
    let _e525 = (((_e321 * 0.46f) + (_e322 * 0.76f)) + (_e323 * 1.14f));
    let _e527 = ((_e333 * 0.5f) + 0.5f);
    let _e572 = (((((_e359 * _e359) * (3f - (_e359 * 2f))) * (1f - ((_e368 * _e368) * (3f - (_e368 * 2f))))) * ((((cos((((_e350 * 71f) - (_e199 * 10.4f)) - (_e380 * 4f))) * 0.5f) + 0.5f) * 0.23f) + ((((sin(((((_e350 * 39f) - (_e199 * 6.8f)) + (_e380 * 7.4f)) + (_e392 * 2.2f))) * 0.5f) + 0.5f) * 0.58f) + 0.19f))) * (((1f - ((_e401 * _e401) * (3f - (_e401 * 2f)))) * 4.4f) + 0.55f));
    let _e577 = (_e413 / 0.032f);
    let _e579 = select(_e577, 1f, (_e577 > 1f));
    let _e602 = (((((1f - ((_e579 * _e579) * (3f - (_e579 * 2f)))) * 0.52f) + ((1f - ((_e422 * _e422) * (3f - (_e422 * 2f)))) * 0.13f)) * ((_e432 * _e432) * (3f - (_e432 * 2f)))) * ((((_e444 * 0.5f) + 0.5f) * 0.65f) + 0.35f));
    let _e610 = ((((_e453 * _e453) * (3f - (_e453 * 2f))) + -1f) + 1f);
    let _e617 = (_e572 * (1f - ((_e462 * _e462) * (3f - (_e462 * 2f)))));
    let _e626 = (1f - ((_e473 * _e473) * (3f - (_e473 * 2f))));
    let _e633 = ((sin(((_e199 * 2.1f) + (_e380 * 5f))) * 0.1f) + 0.9f);
    let _e640 = (_e626 * ((_e484 * _e484) * (3f - (_e484 * 2f))));
    let _e647 = ((0.04f / (abs((_e232 + -0.202f)) + 0.04f)) * _e610);
    let _e649 = ((_e647 * 0.31f) + ((((((((((_e502 * 0.13f) + 0.014f) + (((1f - _e516) * _e508) * 0.24f)) + (_e525 * (1.25f - (_e527 * 0.18f)))) + (_e572 * ((_e412 * 2.18f) + 0.055f))) + (_e602 * 1.36f)) * _e610) + (((_e412 * 1.94f) + 0.07f) * _e617)) + (_e633 * (_e626 * 0.44f))) + (_e640 * 1.48f)));
    let _e676 = ((_e647 * 0.07f) + ((((((((((_e502 * 0.028f) + 0.004f) + ((_e516 * _e508) * 0.052f)) + (_e525 * ((_e527 * 0.31f) + 0.7f))) + (_e572 * ((_e412 * 1.04f) + 0.3f))) + (_e602 * 0.48f)) * _e610) + (((_e412 * 0.93f) + 0.34f) * _e617)) + (_e633 * (_e626 * 0.72f))) + (_e640 * 0.93f)));
    let _e681 = (1f - min(abs(_e302), 1f));
    let _e707 = ((_e647 * 0.19f) + (((((((((((_e681 * _e681) * _e516) * 0.075f) + ((_e502 * 0.022f) + 0.002f)) + (_e525 * ((_e527 * 0.62f) + 0.58f))) + (_e572 * (1.72f - (_e412 * 0.62f)))) + (_e602 * 0.21f)) * _e610) + ((1.61f - (_e412 * 0.48f)) * _e617)) + (_e633 * (_e626 * 1.34f))) + (_e640 * 2.2f)));
    let _e714 = (((1f - ((_e498 * _e498) * (3f - (_e498 * 2f)))) * 0.82f) + 0.18f);
    let _e728 = vec4<f32>(((_e707 * _e714) / (_e707 + 1f)), vec4<f32>(0f, 0f, 0f, 1f).y, vec4<f32>(0f, 0f, 0f, 1f).z, vec4<f32>(0f, 0f, 0f, 1f).w);
    let _e733 = vec4<f32>(_e728.x, ((_e676 * _e714) / (_e676 + 1f)), _e728.z, _e728.w);
    gl_FragColor = vec4<f32>(_e733.x, _e733.y, ((_e649 * _e714) / (_e649 + 1f)), _e733.w);
    return;
}

@fragment 
fn main(@builtin(position) gl_FragCoord: vec4<f32>) -> @location(0) vec4<f32> {
    gl_FragCoord_1 = gl_FragCoord;
    main_1();
    let _e3 = gl_FragColor;
    return _e3;
}
