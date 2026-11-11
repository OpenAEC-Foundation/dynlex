#version 300 es

precision highp float;
precision highp int;

struct type_6 {
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

layout(std140) uniform type_11_block_3Fragment { type_11 _group_0_binding_1_fs; };

layout(location = 0) out vec4 _fs2p_location0;

float the_distance_from_3a_point8point5_to_segment_from_3a_point8start5_to_3a_point8end5_definition_f3a372962fa686537baec66b5c22c2fcb51087893340fec534ce76b120688b7b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b(inout type_6 point, inout type_6 start, inout type_6 end) {
    uint phi_750_ = 0u;
    float phi_752_ = 0.0;
    vec2 _e136 = end.member;
    vec2 _e139 = start.member;
    float _e141 = (_e136.x - _e139.x);
    float _e144 = (_e136.y - _e139.y);
    vec2 _e146 = point.member;
    float _e158 = (((_e141 * (_e146.x - _e139.x)) + (_e144 * (_e146.y - _e139.y))) / max(((_e141 * _e141) + (_e144 * _e144)), 1e-6));
    if ((_e158 < 0.0)) {
        phi_752_ = 0.0;
    } else {
        phi_750_ = 0u;
        if ((_e158 > 1.0)) {
        } else {
            phi_750_ = 1u;
        }
        uint _e162 = phi_750_;
        phi_752_ = _e158;
        if ((0u == _e162)) {
            phi_752_ = 1.0;
        }
    }
    float _e165 = phi_752_;
    float _e170 = (_e146.x - (_e139.x + (_e141 * _e165)));
    float _e171 = (_e146.y - (_e139.y + (_e144 * _e165)));
    return sqrt(((_e170 * _e170) + (_e171 * _e171)));
}

float the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(inout type_6 point_1, inout float phase) {
    vec2 _e135 = point_1.member;
    float _e141 = phase;
    float _e151 = (_e135.x + (sin((_e141 + ((_e135.x * 0.73) - (_e135.y * 0.41)))) * 0.58));
    float _e153 = (_e135.y + (cos((((_e135.x * 0.37) + (_e135.y * 0.89)) - (_e141 * 0.71))) * 0.58));
    float _e159 = sin(((_e141 * 0.43) + ((_e151 * 1.31) + (_e153 * 0.87))));
    float _e165 = cos((((_e153 * 1.73) - (_e151 * 0.79)) - (_e141 * 0.31)));
    return (((_e159 * 0.46) + (_e165 * 0.29)) + ((sin((((_e151 * 2.47) - (_e153 * 2.11)) + (_e159 * 1.8))) * 0.17) + (cos((((_e151 * 4.03) + (_e153 * 3.17)) + (_e165 * 1.4))) * 0.08)));
}

float the_spark_field_at_3a_point8point5_during_3a_value8phase5_definition_b45b4d0f369762d8ebfe5c0c7b634d3bb84e995046b3c28d0d2c522bd033e42e_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(inout type_6 point_2, inout float phase_1) {
    float tmp77_ = 0.0;
    float tmp39_ = 0.0;
    type_6 location = type_6(vec2(0.0));
    type_6 shifted = type_6(vec2(0.0));
    type_6 sample_ = type_6(vec2(0.0));
    uint phi_689_ = 0u;
    float phi_691_ = 0.0;
    vec2 _e140 = point_2.member;
    vec2 _e145 = vec2((_e140.x * 0.19), vec2(0.0).y);
    sample_.member = vec2(_e145.x, (_e140.y * 0.19));
    float _e152 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(sample_, phase_1);
    vec2 _e157 = vec2(((_e140.x * 0.23) + 7.0), vec2(0.0).y);
    shifted.member = vec2(_e157.x, ((_e140.y * 0.23) + -5.0));
    float _e165 = (_e140.x + (_e152 * 1.7));
    float _e166 = phase_1;
    tmp39_ = (_e166 + 1.7);
    float _e168 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(shifted, tmp39_);
    float _e170 = (_e140.y + (_e168 * 1.7));
    vec2 _e174 = vec2((_e165 * 0.11), vec2(0.0).y);
    location.member = vec2(_e174.x, (_e170 * 0.11));
    tmp77_ = (_e166 + 4.0);
    float _e181 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(location, tmp77_);
    float _e185 = ((((_e181 * 0.5) + 0.5) + -0.48) / 0.34);
    if ((_e185 < 0.0)) {
        phi_691_ = 0.0;
    } else {
        phi_689_ = 0u;
        if ((_e185 > 1.0)) {
        } else {
            phi_689_ = 1u;
        }
        uint _e189 = phi_689_;
        phi_691_ = _e185;
        if ((0u == _e189)) {
            phi_691_ = 1.0;
        }
    }
    float _e192 = phi_691_;
    float _e197 = sin((_e166 + ((_e165 * 1.73) + (_e170 * 0.31))));
    float _e204 = sin((((_e170 * 1.91) - (_e165 * 0.27)) - (_e166 * 0.83)));
    float _e208 = (sqrt(((_e197 * _e197) + (_e204 * _e204))) / 0.115);
    float _e210 = ((_e208 > 1.0) ? 1.0 : _e208);
    return ((1.0 - ((_e210 * _e210) * (3.0 - (_e210 * 2.0)))) * ((_e192 * _e192) * (3.0 - (_e192 * 2.0))));
}

float the_scene_window_from_opening_to_closing_at_moment_definition_40f5d1686fffc75cdc19da50a5aa0d0b9862c0c4b26f9c3f2f42bb35a31e707a_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(inout float opening, inout float closing, inout float moment) {
    uint phi_567_ = 0u;
    float phi_569_ = 0.0;
    uint phi_577_ = 0u;
    float phi_579_ = 0.0;
    float _e135 = opening;
    float _e137 = moment;
    float _e140 = ((_e137 - _e135) / ((_e135 + 0.55) - _e135));
    if ((_e140 < 0.0)) {
        phi_569_ = 0.0;
    } else {
        phi_567_ = 0u;
        if ((_e140 > 1.0)) {
        } else {
            phi_567_ = 1u;
        }
        uint _e144 = phi_567_;
        phi_569_ = _e140;
        if ((0u == _e144)) {
            phi_569_ = 1.0;
        }
    }
    float _e147 = phi_569_;
    float _e148 = closing;
    float _e149 = (_e148 + -0.55);
    float _e152 = ((_e137 - _e149) / (_e148 - _e149));
    if ((_e152 < 0.0)) {
        phi_579_ = 0.0;
    } else {
        phi_577_ = 0u;
        if ((_e152 > 1.0)) {
        } else {
            phi_577_ = 1u;
        }
        uint _e156 = phi_577_;
        phi_579_ = _e152;
        if ((0u == _e156)) {
            phi_579_ = 1.0;
        }
    }
    float _e159 = phi_579_;
    return (((_e147 * _e147) * (3.0 - (_e147 * 2.0))) * (1.0 - ((_e159 * _e159) * (3.0 - (_e159 * 2.0)))));
}

void main_1() {
    float tmp238_ = 0.0;
    type_6 tmp237_ = type_6(vec2(0.0));
    float tmp219_ = 0.0;
    type_6 tmp218_ = type_6(vec2(0.0));
    float tmp197_ = 0.0;
    type_6 tmp196_ = type_6(vec2(0.0));
    float tmp175_ = 0.0;
    type_6 tmp174_ = type_6(vec2(0.0));
    float tmp153_ = 0.0;
    float tmp152_ = 0.0;
    float tmp150_ = 0.0;
    float tmp149_ = 0.0;
    type_6 finish = type_6(vec2(0.0));
    type_6 start_1 = type_6(vec2(0.0));
    type_6 ending = type_6(vec2(0.0));
    type_6 center = type_6(vec2(0.0));
    float moment_1 = 0.0;
    type_6 screen = type_6(vec2(0.0));
    uint phi_350_ = 0u;
    float phi_352_ = 0.0;
    uint phi_369_ = 0u;
    float phi_371_ = 0.0;
    uint phi_406_ = 0u;
    float phi_408_ = 0.0;
    uint phi_440_ = 0u;
    float phi_442_ = 0.0;
    uint phi_492_ = 0u;
    float phi_494_ = 0.0;
    uint phi_529_ = 0u;
    float phi_531_ = 0.0;
    float phi_549_ = 0.0;
    float phi_550_ = 0.0;
    float phi_551_ = 0.0;
    vec4 _e150 = gen_gl_FragCoord_1;
    vec4 _e152 = gen_gl_FragCoord_1;
    float _e155 = _group_0_binding_0_fs.member;
    float _e157 = _group_0_binding_2_fs.member;
    float _e158 = max(_e157, 1.0);
    float _e160 = _group_0_binding_3_fs.member;
    float _e161 = max(_e160, 1.0);
    float _e163 = _group_0_binding_1_fs.member;
    float _e168 = ((_e158 / _e161) * (((_e150.x / _e158) * 2.0) + -1.0));
    vec2 _e171 = vec2(_e168, vec2(0.0).y);
    float _e174 = (1.0 - ((_e152.y / _e161) * 2.0));
    screen.member = vec2(_e171.x, _e174);
    float _e179 = (_e168 * _e168);
    float _e182 = sqrt((_e179 + (_e174 * _e174)));
    float _e183 = min(_e155, 10.4);
    moment_1 = _e183;
    if ((_e163 > 0.5)) {
        float _e476 = ((sin(((_e155 * 4.2) + ((_e168 * 19.0) - (_e174 * 13.0)))) * 0.5) + 0.5);
        float _e484 = ((_e183 + -4.45) / 2.9500003);
        if ((_e484 < 0.0)) {
            phi_531_ = 0.0;
        } else {
            phi_529_ = 0u;
            if ((_e484 > 1.0)) {
            } else {
                phi_529_ = 1u;
            }
            uint _e488 = phi_529_;
            phi_531_ = _e484;
            if ((0u == _e488)) {
                phi_531_ = 1.0;
            }
        }
        float _e491 = phi_531_;
        float _e496 = (1.0 - ((_e491 * _e491) * (3.0 - (_e491 * 2.0))));
        phi_549_ = (((_e476 * 0.3) + 0.66) - (_e496 * 0.18));
        phi_550_ = (((((sin(((_e174 * 47.0) - (_e155 * 6.8))) * 0.5) + 0.5) * 0.24) + 0.3) + (_e496 * 0.12));
        phi_551_ = (((_e476 * 0.28) + 0.08) + (_e496 * 0.38));
    } else {
        tmp149_ = 0.0;
        tmp150_ = 7.4;
        float _e185 = the_scene_window_from_opening_to_closing_at_moment_definition_40f5d1686fffc75cdc19da50a5aa0d0b9862c0c4b26f9c3f2f42bb35a31e707a_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp149_, tmp150_, moment_1);
        tmp152_ = 6.85;
        tmp153_ = 11.0;
        float _e186 = the_scene_window_from_opening_to_closing_at_moment_definition_40f5d1686fffc75cdc19da50a5aa0d0b9862c0c4b26f9c3f2f42bb35a31e707a_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp152_, tmp153_, moment_1);
        vec2 _e192 = vec2(((_e155 * 0.021) + (_e168 * 2.2)), vec2(0.0).y);
        tmp174_.member = vec2(_e192.x, ((_e174 * 2.2) - (_e155 * 0.014)));
        tmp175_ = 1.9;
        float _e200 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp174_, tmp175_);
        float _e202 = ((_e200 * 0.5) + 0.5);
        vec2 _e208 = vec2(((_e168 * 4.1) - (_e155 * 0.017)), vec2(0.0).y);
        tmp196_.member = vec2(_e208.x, ((_e155 * 0.011) + (_e174 * 4.1)));
        tmp197_ = 5.4;
        float _e216 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_definition_b36ff1377d79f97ed14ba7257106235547048854eaf25b4dfbf1e21013b41863_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp196_, tmp197_);
        float _e219 = (1.0 - min(abs(_e216), 1.0));
        float _e220 = (_e219 * _e219);
        vec2 _e226 = vec2(((_e155 * 0.24) + (_e168 * 27.0)), vec2(0.0).y);
        tmp218_.member = vec2(_e226.x, ((_e174 * 27.0) - (_e155 * 0.13)));
        tmp219_ = 9.7;
        float _e234 = the_spark_field_at_3a_point8point5_during_3a_value8phase5_definition_b45b4d0f369762d8ebfe5c0c7b634d3bb84e995046b3c28d0d2c522bd033e42e_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp218_, tmp219_);
        vec2 _e240 = vec2(((_e155 * 8.4) + (_e168 * 7.0)), vec2(0.0).y);
        tmp237_.member = vec2(_e240.x, (_e174 * 46.0));
        tmp238_ = 14.2;
        float _e246 = the_spark_field_at_3a_point8point5_during_3a_value8phase5_definition_b45b4d0f369762d8ebfe5c0c7b634d3bb84e995046b3c28d0d2c522bd033e42e_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_f32_type_cd22af9c08ef5d85283977725d4c0dec87301f16dc20d5b89458bde4455db56b(tmp237_, tmp238_);
        float _e248 = (abs(_e174) / 0.66);
        float _e250 = ((_e248 > 1.0) ? 1.0 : _e248);
        float _e257 = (_e185 * ((1.0 - ((_e250 * _e250) * (3.0 - (_e250 * 2.0)))) * _e246));
        float _e283 = ((_e182 + -0.28) / 0.98);
        if ((_e283 < 0.0)) {
            phi_352_ = 0.0;
        } else {
            phi_350_ = 0u;
            if ((_e283 > 1.0)) {
            } else {
                phi_350_ = 1u;
            }
            uint _e287 = phi_350_;
            phi_352_ = _e283;
            if ((0u == _e287)) {
                phi_352_ = 1.0;
            }
        }
        float _e290 = phi_352_;
        float _e296 = (_e186 * (1.0 - ((_e290 * _e290) * (3.0 - (_e290 * 2.0)))));
        float _e304 = ((_e183 + -7.55) / 0.5500002);
        if ((_e304 < 0.0)) {
            phi_371_ = 0.0;
        } else {
            phi_369_ = 0u;
            if ((_e304 > 1.0)) {
            } else {
                phi_369_ = 1u;
            }
            uint _e308 = phi_369_;
            phi_371_ = _e304;
            if ((0u == _e308)) {
                phi_371_ = 1.0;
            }
        }
        float _e311 = phi_371_;
        float _e317 = ((_e155 + -7.55) * 1.32);
        float _e319 = (cos(_e317) * 1.08);
        float _e324 = (sin((_e155 * 0.31)) * 0.34);
        float _e331 = (((_e319 * -(sin(_e324))) * 0.52) + 3.25);
        center.member = vec2(0.0, -0.012307692);
        float _e334 = (((_e319 * cos(_e324)) * 1.66) / _e331);
        vec2 _e337 = vec2(_e334, vec2(0.0).y);
        float _e340 = ((((sin(_e317) * 1.08) * 1.86) + -0.04) / _e331);
        ending.member = vec2(_e337.x, _e340);
        float _e345 = the_distance_from_3a_point8point5_to_segment_from_3a_point8start5_to_3a_point8end5_definition_f3a372962fa686537baec66b5c22c2fcb51087893340fec534ce76b120688b7b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b(screen, center, ending);
        float _e347 = ((_e345 + -0.002) / 0.0069999993);
        if ((_e347 < 0.0)) {
            phi_408_ = 0.0;
        } else {
            phi_406_ = 0u;
            if ((_e347 > 1.0)) {
            } else {
                phi_406_ = 1u;
            }
            uint _e351 = phi_406_;
            phi_408_ = _e347;
            if ((0u == _e351)) {
                phi_408_ = 1.0;
            }
        }
        float _e354 = phi_408_;
        float _e360 = (_e340 + 0.012307692);
        float _e365 = max(sqrt(((_e334 * _e334) + (_e360 * _e360))), 0.0001);
        float _e369 = ((-(_e360) / _e365) * 0.026);
        vec2 _e373 = vec2((_e334 - _e369), vec2(0.0).y);
        float _e374 = ((_e334 / _e365) * 0.026);
        start_1.member = vec2(_e373.x, (_e340 - _e374));
        vec2 _e383 = vec2((_e334 + _e369), vec2(0.0).y);
        finish.member = vec2(_e383.x, (_e340 + _e374));
        float _e389 = the_distance_from_3a_point8point5_to_segment_from_3a_point8start5_to_3a_point8end5_definition_f3a372962fa686537baec66b5c22c2fcb51087893340fec534ce76b120688b7b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b_a_20_point_type_1d8b216223ff18c472b0a2a0c03d1688982774185e099e56f13e23e4f69c432b(screen, start_1, finish);
        float _e391 = ((_e389 + -0.002) / 0.009);
        if ((_e391 < 0.0)) {
            phi_442_ = 0.0;
        } else {
            phi_440_ = 0u;
            if ((_e391 > 1.0)) {
            } else {
                phi_440_ = 1u;
            }
            uint _e395 = phi_440_;
            phi_442_ = _e391;
            if ((0u == _e395)) {
                phi_442_ = 1.0;
            }
        }
        float _e398 = phi_442_;
        float _e404 = (_e168 - _e334);
        float _e405 = (_e174 - _e340);
        float _e410 = (sqrt(((_e404 * _e404) + (_e405 * _e405))) / 0.045);
        float _e412 = ((_e410 > 1.0) ? 1.0 : _e410);
        float _e418 = (_e174 + 0.012307692);
        float _e422 = (sqrt((_e179 + (_e418 * _e418))) / 0.025);
        float _e424 = ((_e422 > 1.0) ? 1.0 : _e422);
        float _e437 = (((_e311 * _e311) * (3.0 - (_e311 * 2.0))) * (((1.0 - ((_e424 * _e424) * (3.0 - (_e424 * 2.0)))) * 0.36) + (((1.0 - ((_e412 * _e412) * (3.0 - (_e412 * 2.0)))) * 0.48) + (((1.0 - ((_e354 * _e354) * (3.0 - (_e354 * 2.0)))) * 0.62) + ((1.0 - ((_e398 * _e398) * (3.0 - (_e398 * 2.0)))) * 0.82)))));
        float _e439 = (((((_e234 * 0.18) + (((_e202 * 0.012) + 0.003) + (_e220 * 0.018))) + (_e257 * 0.32)) + (_e296 * 0.032)) + (_e437 * 0.46));
        float _e441 = (((((_e234 * 0.46) + (((_e202 * 0.024) + 0.006) + (_e220 * 0.015))) + (_e257 * 0.78)) + (_e296 * 0.095)) + (_e437 * 0.88));
        float _e443 = (((((_e234 * 1.02) + (((_e202 * 0.082) + 0.024) + (_e220 * 0.071))) + (_e257 * 1.46)) + (_e296 * 0.21)) + (_e437 * 1.24));
        float _e445 = ((_e182 + -0.52) / 1.1);
        if ((_e445 < 0.0)) {
            phi_494_ = 0.0;
        } else {
            phi_492_ = 0u;
            if ((_e445 > 1.0)) {
            } else {
                phi_492_ = 1u;
            }
            uint _e449 = phi_492_;
            phi_494_ = _e445;
            if ((0u == _e449)) {
                phi_494_ = 1.0;
            }
        }
        float _e452 = phi_494_;
        float _e459 = (((1.0 - ((_e452 * _e452) * (3.0 - (_e452 * 2.0)))) * 0.82) + 0.18);
        phi_549_ = ((_e443 * _e459) / (_e443 + 1.0));
        phi_550_ = ((_e441 * _e459) / (_e441 + 1.0));
        phi_551_ = ((_e439 * _e459) / (_e439 + 1.0));
    }
    float _e510 = phi_549_;
    float _e512 = phi_550_;
    float _e514 = phi_551_;
    vec4 _e519 = vec4(_e514, vec4(0.0, 0.0, 0.0, 1.0).y, vec4(0.0, 0.0, 0.0, 1.0).z, vec4(0.0, 0.0, 0.0, 1.0).w);
    vec4 _e524 = vec4(_e519.x, _e512, _e519.z, _e519.w);
    gen_gl_FragColor = vec4(_e524.x, _e524.y, _e510, _e524.w);
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
