#version 300 es

precision highp float;
precision highp int;

struct type_7 {
    vec2 member;
};
struct type_11 {
    float member;
};
vec4 gen_gl_FragCoord_1 = vec4(0.0);

vec4 gen_gl_FragColor = vec4(0.0);

layout(std140) uniform type_11_block_0Fragment { type_11 _group_0_binding_0_fs; };

layout(std140) uniform type_11_block_1Fragment { type_11 _group_0_binding_2_fs; };

layout(std140) uniform type_11_block_2Fragment { type_11 _group_0_binding_3_fs; };

layout(location = 0) out vec4 _fs2p_location0;

float the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(inout type_7 point, inout float phase) {
    vec2 _e175 = point.member;
    float _e181 = phase;
    float _e191 = (_e175.x + (sin((_e181 + ((_e175.x * 0.73) - (_e175.y * 0.41)))) * 0.58));
    float _e193 = (_e175.y + (cos((((_e175.x * 0.37) + (_e175.y * 0.89)) - (_e181 * 0.71))) * 0.58));
    float _e199 = sin(((_e181 * 0.43) + ((_e191 * 1.31) + (_e193 * 0.87))));
    float _e205 = cos((((_e193 * 1.73) - (_e191 * 0.79)) - (_e181 * 0.31)));
    return (((_e199 * 0.46) + (_e205 * 0.29)) + ((sin((((_e191 * 2.47) - (_e193 * 2.11)) + (_e199 * 1.8))) * 0.17) + (cos((((_e191 * 4.03) + (_e193 * 3.17)) + (_e205 * 1.4))) * 0.08)));
}

float the_spark_field_at_3a_point8point5_during_3a_value8phase5_definition_b45b4d0f369762d8ebfe5c0c7b634d3bb84e995046b3c28d0d2c522bd033e42e_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(inout type_7 point_1, inout float phase_1) {
    float tmp77_ = 0.0;
    float tmp39_ = 0.0;
    type_7 location = type_7(vec2(0.0));
    type_7 shifted = type_7(vec2(0.0));
    type_7 sample_ = type_7(vec2(0.0));
    uint phi_944_ = 0u;
    float phi_946_ = 0.0;
    vec2 _e180 = point_1.member;
    vec2 _e185 = vec2((_e180.x * 0.19), vec2(0.0).y);
    sample_.member = vec2(_e185.x, (_e180.y * 0.19));
    float _e192 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(sample_, phase_1);
    vec2 _e197 = vec2(((_e180.x * 0.23) + 7.0), vec2(0.0).y);
    shifted.member = vec2(_e197.x, ((_e180.y * 0.23) + -5.0));
    float _e205 = (_e180.x + (_e192 * 1.7));
    float _e206 = phase_1;
    tmp39_ = (_e206 + 1.7);
    float _e208 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(shifted, tmp39_);
    float _e210 = (_e180.y + (_e208 * 1.7));
    vec2 _e214 = vec2((_e205 * 0.11), vec2(0.0).y);
    location.member = vec2(_e214.x, (_e210 * 0.11));
    tmp77_ = (_e206 + 4.0);
    float _e221 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(location, tmp77_);
    float _e225 = ((((_e221 * 0.5) + 0.5) + -0.48) / 0.34);
    if ((_e225 < 0.0)) {
        phi_946_ = 0.0;
    } else {
        phi_944_ = 0u;
        if ((_e225 > 1.0)) {
        } else {
            phi_944_ = 1u;
        }
        uint _e229 = phi_944_;
        phi_946_ = _e225;
        if ((0u == _e229)) {
            phi_946_ = 1.0;
        }
    }
    float _e232 = phi_946_;
    float _e237 = sin((_e206 + ((_e205 * 1.73) + (_e210 * 0.31))));
    float _e244 = sin((((_e210 * 1.91) - (_e205 * 0.27)) - (_e206 * 0.83)));
    float _e248 = (sqrt(((_e237 * _e237) + (_e244 * _e244))) / 0.115);
    float _e250 = ((_e248 > 1.0) ? 1.0 : _e248);
    return ((1.0 - ((_e250 * _e250) * (3.0 - (_e250 * 2.0)))) * ((_e232 * _e232) * (3.0 - (_e232 * 2.0))));
}

float a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3floating5point_number8scale5_during_3floating5point_number8phase5_at_3floating5point_number8time5_definition_c5856d2c5efd026e505778d50574371148e52e90ec263059ef5737eecfdfcf6b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(inout type_7 position, inout float scale, inout float phase_2, inout float time) {
    float tmp62_ = 0.0;
    type_7 tmp58_ = type_7(vec2(0.0));
    float tmp45_ = 0.0;
    type_7 tmp41_ = type_7(vec2(0.0));
    float tmp28_ = 0.0;
    type_7 tmp26_ = type_7(vec2(0.0));
    float _e182 = time;
    float _e184 = phase_2;
    float _e185 = ((_e182 * 0.13) + _e184);
    float _e187 = (_e185 - floor(_e185));
    float _e190 = (((_e187 * _e187) * 3.6) + 0.42);
    vec2 _e192 = position.member;
    float _e195 = scale;
    float _e196 = (_e195 * (_e192.x / _e190));
    vec2 _e199 = vec2(_e196, vec2(0.0, 0.0).y);
    float _e202 = (_e195 * (_e192.y / _e190));
    tmp26_.member = vec2(_e199.x, _e202);
    float _e207 = (_e184 * 13.7);
    tmp28_ = _e207;
    float _e208 = the_spark_field_at_3a_point8point5_during_3a_value8phase5_definition_b45b4d0f369762d8ebfe5c0c7b634d3bb84e995046b3c28d0d2c522bd033e42e_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp26_, tmp28_);
    vec2 _e212 = vec2((_e196 * 0.33), vec2(0.0).y);
    tmp41_.member = vec2(_e212.x, _e202);
    tmp45_ = (_e207 + 0.09);
    float _e218 = the_spark_field_at_3a_point8point5_during_3a_value8phase5_definition_b45b4d0f369762d8ebfe5c0c7b634d3bb84e995046b3c28d0d2c522bd033e42e_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp41_, tmp45_);
    tmp58_.member = vec2(_e199.x, (_e202 * 0.33));
    tmp62_ = (_e207 + -0.11);
    float _e225 = the_spark_field_at_3a_point8point5_during_3a_value8phase5_definition_b45b4d0f369762d8ebfe5c0c7b634d3bb84e995046b3c28d0d2c522bd033e42e_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp58_, tmp62_);
    float _e231 = (sqrt(((_e192.x * _e192.x) + (_e192.y * _e192.y))) / 1.35);
    float _e233 = ((_e231 > 1.0) ? 1.0 : _e231);
    return (((sin(((_e182 * 2.7) + (_e184 * 29.0))) * 0.13) + 0.87) * (((_e187 * 1.25) + 0.28) * ((_e208 * 2.8) + (((_e218 + _e225) * (1.0 - ((_e233 * _e233) * (3.0 - (_e233 * 2.0))))) * 0.17))));
}

void main_1() {
    float tmp593_ = 0.0;
    type_7 tmp592_ = type_7(vec2(0.0));
    float tmp446_ = 0.0;
    type_7 tmp445_ = type_7(vec2(0.0));
    float tmp431_ = 0.0;
    type_7 tmp430_ = type_7(vec2(0.0));
    float tmp308_ = 0.0;
    type_7 tmp307_ = type_7(vec2(0.0));
    float tmp284_ = 0.0;
    float tmp283_ = 0.0;
    float tmp281_ = 0.0;
    float tmp280_ = 0.0;
    float tmp278_ = 0.0;
    float tmp277_ = 0.0;
    float tmp220_ = 0.0;
    type_7 tmp218_ = type_7(vec2(0.0));
    float tmp199_ = 0.0;
    type_7 tmp198_ = type_7(vec2(0.0));
    float tmp178_ = 0.0;
    type_7 tmp177_ = type_7(vec2(0.0));
    type_7 universe = type_7(vec2(0.0));
    float time_1 = 0.0;
    uint phi_395_ = 0u;
    float phi_397_ = 0.0;
    uint phi_402_ = 0u;
    float phi_404_ = 0.0;
    uint phi_435_ = 0u;
    float phi_437_ = 0.0;
    uint phi_442_ = 0u;
    float phi_444_ = 0.0;
    uint phi_465_ = 0u;
    float phi_467_ = 0.0;
    uint phi_474_ = 0u;
    float phi_476_ = 0.0;
    uint phi_482_ = 0u;
    float phi_484_ = 0.0;
    uint phi_490_ = 0u;
    float phi_492_ = 0.0;
    uint phi_505_ = 0u;
    float phi_507_ = 0.0;
    uint phi_512_ = 0u;
    float phi_514_ = 0.0;
    uint phi_521_ = 0u;
    float phi_523_ = 0.0;
    uint phi_530_ = 0u;
    float phi_532_ = 0.0;
    uint phi_542_ = 0u;
    float phi_544_ = 0.0;
    vec4 _e194 = gen_gl_FragCoord_1;
    vec4 _e196 = gen_gl_FragCoord_1;
    float _e199 = _group_0_binding_0_fs.member;
    time_1 = _e199;
    float _e201 = _group_0_binding_2_fs.member;
    float _e202 = max(_e201, 1.0);
    float _e204 = _group_0_binding_3_fs.member;
    float _e205 = max(_e204, 1.0);
    float _e206 = (_e202 / _e205);
    float _e210 = (_e206 * (((_e194.x / _e202) * 2.0) + -1.0));
    float _e213 = (((_e196.y / _e205) * 2.0) + -1.0);
    float _e218 = (((_e199 * 0.055) + 0.62) + ((_e199 * _e199) * 0.0018));
    float _e226 = (((sin((_e199 * 0.27)) * 0.048) + _e210) / _e218);
    float _e228 = (((cos((_e199 * 0.21)) * 0.026) + _e213) / _e218);
    float _e232 = sqrt(((_e226 * _e226) + (_e228 * _e228)));
    float _e234 = (_e232 * _e232);
    float _e239 = min(((_e199 * 0.035) + (0.19 / (_e234 + 0.021))), 4.6);
    float _e240 = sin(_e239);
    float _e241 = cos(_e239);
    float _e244 = ((0.12 / (_e234 + 0.012)) + 1.0);
    float _e248 = (_e244 * ((_e226 * _e241) - (_e228 * _e240)));
    vec2 _e251 = vec2(_e248, vec2(0.0).y);
    float _e255 = (_e244 * ((_e226 * _e240) + (_e228 * _e241)));
    universe.member = vec2(_e251.x, _e255);
    vec2 _e265 = vec2(((_e199 * 0.025) + (_e248 * 1.8)), vec2(0.0).y);
    tmp177_.member = vec2(_e265.x, ((_e255 * 1.8) + -4.0));
    tmp178_ = 1.7;
    float _e272 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp177_, tmp178_);
    float _e274 = ((_e272 * 0.5) + 0.5);
    vec2 _e279 = vec2(((_e248 * 4.6) + -11.0), vec2(0.0).y);
    tmp198_.member = vec2(_e279.x, ((_e199 * 0.018) + (_e255 * 4.6)));
    tmp199_ = 5.3;
    float _e287 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp198_, tmp199_);
    vec2 _e294 = vec2(((_e248 * 2.4) + 7.0), vec2(0.0).y);
    tmp218_.member = vec2(_e294.x, ((_e255 * 2.4) + -9.0));
    tmp220_ = (_e199 * 0.045);
    float _e302 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp218_, tmp220_);
    float _e304 = ((_e274 + -0.45) / 0.41000003);
    if ((_e304 < 0.0)) {
        phi_397_ = 0.0;
    } else {
        phi_395_ = 0u;
        if ((_e304 > 1.0)) {
        } else {
            phi_395_ = 1u;
        }
        uint _e308 = phi_395_;
        phi_397_ = _e304;
        if ((0u == _e308)) {
            phi_397_ = 1.0;
        }
    }
    float _e311 = phi_397_;
    float _e313 = ((((_e287 * 0.5) + 0.5) + -0.52) / 0.38);
    if ((_e313 < 0.0)) {
        phi_404_ = 0.0;
    } else {
        phi_402_ = 0u;
        if ((_e313 > 1.0)) {
        } else {
            phi_402_ = 1u;
        }
        uint _e317 = phi_402_;
        phi_404_ = _e313;
        if ((0u == _e317)) {
            phi_404_ = 1.0;
        }
    }
    float _e320 = phi_404_;
    tmp277_ = 13.0;
    tmp278_ = 0.09;
    float _e321 = a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3floating5point_number8scale5_during_3floating5point_number8phase5_at_3floating5point_number8time5_definition_c5856d2c5efd026e505778d50574371148e52e90ec263059ef5737eecfdfcf6b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(universe, tmp277_, tmp278_, time_1);
    tmp280_ = 21.0;
    tmp281_ = 0.41;
    float _e322 = a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3floating5point_number8scale5_during_3floating5point_number8phase5_at_3floating5point_number8time5_definition_c5856d2c5efd026e505778d50574371148e52e90ec263059ef5737eecfdfcf6b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(universe, tmp280_, tmp281_, time_1);
    tmp283_ = 31.0;
    tmp284_ = 0.77;
    float _e323 = a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3floating5point_number8scale5_during_3floating5point_number8phase5_at_3floating5point_number8time5_definition_c5856d2c5efd026e505778d50574371148e52e90ec263059ef5737eecfdfcf6b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(universe, tmp283_, tmp284_, time_1);
    vec2 _e327 = vec2((_e248 * 5.1), vec2(0.0).y);
    tmp307_.member = vec2(_e327.x, (_e255 * 5.1));
    tmp308_ = 8.2;
    float _e333 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp307_, tmp308_);
    float _e337 = ((sin((_e199 * 0.17)) * 0.018) + 0.12);
    float _e338 = sin(_e337);
    float _e339 = cos(_e337);
    float _e342 = ((_e339 * _e226) + (_e338 * _e228));
    float _e345 = ((_e339 * _e228) - (_e338 * _e226));
    float _e346 = (_e345 * 7.2);
    float _e350 = sqrt(((_e342 * _e342) + (_e346 * _e346)));
    float _e352 = ((_e350 + -0.23) / 0.054999992);
    if ((_e352 < 0.0)) {
        phi_437_ = 0.0;
    } else {
        phi_435_ = 0u;
        if ((_e352 > 1.0)) {
        } else {
            phi_435_ = 1u;
        }
        uint _e356 = phi_435_;
        phi_437_ = _e352;
        if ((0u == _e356)) {
            phi_437_ = 1.0;
        }
    }
    float _e359 = phi_437_;
    float _e361 = ((_e350 + -0.72) / 0.39999998);
    if ((_e361 < 0.0)) {
        phi_444_ = 0.0;
    } else {
        phi_442_ = 0u;
        if ((_e361 > 1.0)) {
        } else {
            phi_442_ = 1u;
        }
        uint _e365 = phi_442_;
        phi_444_ = _e361;
        if ((0u == _e365)) {
            phi_444_ = 1.0;
        }
    }
    float _e368 = phi_444_;
    vec2 _e374 = vec2(((_e199 * 0.52) + (_e342 * 8.3)), vec2(0.0).y);
    tmp430_.member = vec2(_e374.x, (_e346 * 1.8));
    tmp431_ = 3.1;
    float _e380 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp430_, tmp431_);
    vec2 _e386 = vec2(((_e342 * 18.0) - (_e199 * 0.91)), vec2(0.0).y);
    tmp445_.member = vec2(_e386.x, (_e346 * 3.7));
    tmp446_ = 7.9;
    float _e392 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp445_, tmp446_);
    float _e394 = ((_e350 + -0.29) / 0.53999996);
    if ((_e394 < 0.0)) {
        phi_467_ = 0.0;
    } else {
        phi_465_ = 0u;
        if ((_e394 > 1.0)) {
        } else {
            phi_465_ = 1u;
        }
        uint _e398 = phi_465_;
        phi_467_ = _e394;
        if ((0u == _e398)) {
            phi_467_ = 1.0;
        }
    }
    float _e401 = phi_467_;
    float _e405 = (((_e342 / max(_e350, 0.001)) * 0.5) + 0.5);
    if ((_e405 < 0.0)) {
        phi_476_ = 0.0;
    } else {
        phi_474_ = 0u;
        if ((_e405 > 1.0)) {
        } else {
            phi_474_ = 1u;
        }
        uint _e409 = phi_474_;
        phi_476_ = _e405;
        if ((0u == _e409)) {
            phi_476_ = 1.0;
        }
    }
    float _e412 = phi_476_;
    float _e413 = abs(_e226);
    float _e415 = ((_e413 + -0.018) / 0.14199999);
    if ((_e415 < 0.0)) {
        phi_484_ = 0.0;
    } else {
        phi_482_ = 0u;
        if ((_e415 > 1.0)) {
        } else {
            phi_482_ = 1u;
        }
        uint _e419 = phi_482_;
        phi_484_ = _e415;
        if ((0u == _e419)) {
            phi_484_ = 1.0;
        }
    }
    float _e422 = phi_484_;
    float _e425 = ((abs(_e228) + -0.19) / 0.53000003);
    if ((_e425 < 0.0)) {
        phi_492_ = 0.0;
    } else {
        phi_490_ = 0u;
        if ((_e425 > 1.0)) {
        } else {
            phi_490_ = 1u;
        }
        uint _e429 = phi_490_;
        phi_492_ = _e425;
        if ((0u == _e429)) {
            phi_492_ = 1.0;
        }
    }
    float _e432 = phi_492_;
    vec2 _e436 = vec2((_e226 * 15.0), vec2(0.0).y);
    tmp592_.member = vec2(_e436.x, ((_e228 * 3.0) - (_e199 * 0.8)));
    tmp593_ = 10.0;
    float _e444 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp592_, tmp593_);
    float _e446 = ((_e232 + -0.176) / 0.026000008);
    if ((_e446 < 0.0)) {
        phi_507_ = 0.0;
    } else {
        phi_505_ = 0u;
        if ((_e446 > 1.0)) {
        } else {
            phi_505_ = 1u;
        }
        uint _e450 = phi_505_;
        phi_507_ = _e446;
        if ((0u == _e450)) {
            phi_507_ = 1.0;
        }
    }
    float _e453 = phi_507_;
    float _e455 = ((_e345 + 0.019) / 0.033999998);
    if ((_e455 < 0.0)) {
        phi_514_ = 0.0;
    } else {
        phi_512_ = 0u;
        if ((_e455 > 1.0)) {
        } else {
            phi_512_ = 1u;
        }
        uint _e459 = phi_512_;
        phi_514_ = _e455;
        if ((0u == _e459)) {
            phi_514_ = 1.0;
        }
    }
    float _e462 = phi_514_;
    float _e466 = ((abs((_e232 + -0.216)) + -0.001) / 0.016);
    if ((_e466 < 0.0)) {
        phi_523_ = 0.0;
    } else {
        phi_521_ = 0u;
        if ((_e466 > 1.0)) {
        } else {
            phi_521_ = 1u;
        }
        uint _e470 = phi_521_;
        phi_523_ = _e466;
        if ((0u == _e470)) {
            phi_523_ = 1.0;
        }
    }
    float _e473 = phi_523_;
    float _e477 = ((abs((_e228 / max(_e232, 0.001))) + -0.16) / 0.70000005);
    if ((_e477 < 0.0)) {
        phi_532_ = 0.0;
    } else {
        phi_530_ = 0u;
        if ((_e477 > 1.0)) {
        } else {
            phi_530_ = 1u;
        }
        uint _e481 = phi_530_;
        phi_532_ = _e477;
        if ((0u == _e481)) {
            phi_532_ = 1.0;
        }
    }
    float _e484 = phi_532_;
    float _e485 = (_e210 / _e206);
    float _e491 = ((sqrt(((_e213 * _e213) + (_e485 * _e485))) + -0.48) / 0.86);
    if ((_e491 < 0.0)) {
        phi_544_ = 0.0;
    } else {
        phi_542_ = 0u;
        if ((_e491 > 1.0)) {
        } else {
            phi_542_ = 1u;
        }
        uint _e495 = phi_542_;
        phi_544_ = _e491;
        if ((0u == _e495)) {
            phi_544_ = 1.0;
        }
    }
    float _e498 = phi_544_;
    float _e502 = ((_e311 * _e311) * (3.0 - (_e311 * 2.0)));
    float _e508 = ((_e320 * _e320) * (3.0 - (_e320 * 2.0)));
    float _e516 = ((sin(((_e274 * 8.0) + ((_e248 * 2.7) - (_e255 * 3.6)))) * 0.5) + 0.5);
    float _e525 = (((_e321 * 0.46) + (_e322 * 0.76)) + (_e323 * 1.14));
    float _e527 = ((_e333 * 0.5) + 0.5);
    float _e572 = (((((_e359 * _e359) * (3.0 - (_e359 * 2.0))) * (1.0 - ((_e368 * _e368) * (3.0 - (_e368 * 2.0))))) * ((((cos((((_e350 * 71.0) - (_e199 * 10.4)) - (_e380 * 4.0))) * 0.5) + 0.5) * 0.23) + ((((sin(((((_e350 * 39.0) - (_e199 * 6.8)) + (_e380 * 7.4)) + (_e392 * 2.2))) * 0.5) + 0.5) * 0.58) + 0.19))) * (((1.0 - ((_e401 * _e401) * (3.0 - (_e401 * 2.0)))) * 4.4) + 0.55));
    float _e577 = (_e413 / 0.032);
    float _e579 = ((_e577 > 1.0) ? 1.0 : _e577);
    float _e602 = (((((1.0 - ((_e579 * _e579) * (3.0 - (_e579 * 2.0)))) * 0.52) + ((1.0 - ((_e422 * _e422) * (3.0 - (_e422 * 2.0)))) * 0.13)) * ((_e432 * _e432) * (3.0 - (_e432 * 2.0)))) * ((((_e444 * 0.5) + 0.5) * 0.65) + 0.35));
    float _e610 = ((((_e453 * _e453) * (3.0 - (_e453 * 2.0))) + -1.0) + 1.0);
    float _e617 = (_e572 * (1.0 - ((_e462 * _e462) * (3.0 - (_e462 * 2.0)))));
    float _e626 = (1.0 - ((_e473 * _e473) * (3.0 - (_e473 * 2.0))));
    float _e633 = ((sin(((_e199 * 2.1) + (_e380 * 5.0))) * 0.1) + 0.9);
    float _e640 = (_e626 * ((_e484 * _e484) * (3.0 - (_e484 * 2.0))));
    float _e647 = ((0.04 / (abs((_e232 + -0.202)) + 0.04)) * _e610);
    float _e649 = ((_e647 * 0.31) + ((((((((((_e502 * 0.13) + 0.014) + (((1.0 - _e516) * _e508) * 0.24)) + (_e525 * (1.25 - (_e527 * 0.18)))) + (_e572 * ((_e412 * 2.18) + 0.055))) + (_e602 * 1.36)) * _e610) + (((_e412 * 1.94) + 0.07) * _e617)) + (_e633 * (_e626 * 0.44))) + (_e640 * 1.48)));
    float _e676 = ((_e647 * 0.07) + ((((((((((_e502 * 0.028) + 0.004) + ((_e516 * _e508) * 0.052)) + (_e525 * ((_e527 * 0.31) + 0.7))) + (_e572 * ((_e412 * 1.04) + 0.3))) + (_e602 * 0.48)) * _e610) + (((_e412 * 0.93) + 0.34) * _e617)) + (_e633 * (_e626 * 0.72))) + (_e640 * 0.93)));
    float _e681 = (1.0 - min(abs(_e302), 1.0));
    float _e707 = ((_e647 * 0.19) + (((((((((((_e681 * _e681) * _e516) * 0.075) + ((_e502 * 0.022) + 0.002)) + (_e525 * ((_e527 * 0.62) + 0.58))) + (_e572 * (1.72 - (_e412 * 0.62)))) + (_e602 * 0.21)) * _e610) + ((1.61 - (_e412 * 0.48)) * _e617)) + (_e633 * (_e626 * 1.34))) + (_e640 * 2.2)));
    float _e714 = (((1.0 - ((_e498 * _e498) * (3.0 - (_e498 * 2.0)))) * 0.82) + 0.18);
    vec4 _e728 = vec4(((_e707 * _e714) / (_e707 + 1.0)), vec4(0.0, 0.0, 0.0, 1.0).y, vec4(0.0, 0.0, 0.0, 1.0).z, vec4(0.0, 0.0, 0.0, 1.0).w);
    vec4 _e733 = vec4(_e728.x, ((_e676 * _e714) / (_e676 + 1.0)), _e728.z, _e728.w);
    gen_gl_FragColor = vec4(_e733.x, _e733.y, ((_e649 * _e714) / (_e649 + 1.0)), _e733.w);
    return;
}

void main() {
    vec4 gen_gl_FragCoord = gl_FragCoord;
    gen_gl_FragCoord_1 = gen_gl_FragCoord;
    main_1();
    vec4 _e3 = gen_gl_FragColor;
    _fs2p_location0 = _e3;
    return;
}
