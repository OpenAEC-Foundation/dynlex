#version 300 es

struct class_0
{
    vec3 _m0;
};

struct _class
{
    vec2 _m0;
};

layout(std140) uniform DynlexUniformBlock0
{
    float value;
} dynlexUniform0;

layout(std140) uniform DynlexUniformBlock1
{
    float value;
} dynlexUniform1;

layout(std140) uniform DynlexUniformBlock2
{
    float value;
} dynlexUniform2;

layout(location = 0) in vec4 in_Position;
out vec4 dynlex_interpolant_7465727261696e20706f736974696f6e;
out vec4 dynlex_interpolant_7465727261696e206e6f726d616c;
out vec4 dynlex_interpolant_7465727261696e206d6174657269616c;

float the_maximum_of_left_and_right_f32_f32(float left, float right)
{
    return isnan(right) ? left : (isnan(left) ? right : max(left, right));
}

float left1_4371_43right_f32_f32(float left, float right)
{
    return left / right;
}

bool left_0_right_f32_f32(float left, float right)
{
    return left < right;
}

float left1_4321_43right_f32_f32(float left, float right)
{
    return left * right;
}

float the_sine_of_value_f32(float value)
{
    return sin(value);
}

float the_cosine_of_value_f32(float value)
{
    return cos(value);
}

float left1_4331_43right_f32_f32(float left, float right)
{
    return left + right;
}

float the_maximum_possible_terrain_height()
{
    return 12.1000003814697265625;
}

float _the_negative_of_4the_opposite_of_453value_f32(float value)
{
    return -value;
}

class_0 the_terrain_camera_at_moment_f32(float moment)
{
    float tmp = 201.6999969482421875;
    float tmp1 = 0.090999998152256011962890625;
    float tmp2 = left1_4321_43right_f32_f32(moment, tmp1);
    float tmp3 = the_sine_of_value_f32(tmp2);
    float tmp4 = 4.19999980926513671875;
    float tmp5 = left1_4321_43right_f32_f32(tmp3, tmp4);
    float tmp6 = 0.037000000476837158203125;
    float tmp7 = left1_4321_43right_f32_f32(moment, tmp6);
    float tmp8 = the_cosine_of_value_f32(tmp7);
    float tmp9 = 1.60000002384185791015625;
    float tmp10 = left1_4321_43right_f32_f32(tmp8, tmp9);
    float tmp11 = left1_4331_43right_f32_f32(tmp5, tmp10);
    vec3 _1741 = vec3(0.0);
    _1741.x = left1_4331_43right_f32_f32(tmp, tmp11);
    float tmp12 = the_maximum_possible_terrain_height();
    float tmp13 = 0.699999988079071044921875;
    _1741.y = left1_4331_43right_f32_f32(tmp12, tmp13);
    float tmp15 = 92.09999847412109375;
    float tmp16 = _the_negative_of_4the_opposite_of_453value_f32(tmp15);
    float tmp17 = 3.25;
    float tmp18 = left1_4321_43right_f32_f32(moment, tmp17);
    _1741.z = left1_4331_43right_f32_f32(tmp16, tmp18);
    class_0 class_tmp = class_0(vec3(0.0));
    class_tmp._m0 = _1741;
    return class_tmp;
}

float left1_4351_43right_f32_f32(float left, float right)
{
    return left - right;
}

float the_square_root_of_value_f32(float value)
{
    return sqrt(value);
}

bool left_2_right_f32_f32(float left, float right)
{
    return left > right;
}

float the_floor_of_value_f32(float value)
{
    return floor(value);
}

float the_simplex_permutation_of_value_f32(float value)
{
    float tmp = 34.0;
    float tmp1 = left1_4321_43right_f32_f32(value, tmp);
    float tmp2 = 1.0;
    float tmp3 = left1_4331_43right_f32_f32(tmp1, tmp2);
    float polynomial = left1_4321_43right_f32_f32(tmp3, value);
    float tmp4 = 289.0;
    float tmp5 = left1_4371_43right_f32_f32(polynomial, tmp4);
    float tmp6 = the_floor_of_value_f32(tmp5);
    float tmp7 = 289.0;
    float tmp8 = left1_4321_43right_f32_f32(tmp6, tmp7);
    return left1_4351_43right_f32_f32(polynomial, tmp8);
}

float the_fractional_part_of_number_f32(float number)
{
    float tmp = the_floor_of_value_f32(number);
    return left1_4351_43right_f32_f32(number, tmp);
}

float the_absolute_value_of_magnitude_f32(float magnitude)
{
    return abs(magnitude);
}

float the_simplex_corner_at_3a_point8offset5_with_permutation_3a_value8permutation5_a_point_f32(_class offset, float permutation)
{
    float tmp = 0.5;
    float tmp1 = offset._m0.x;
    float tmp5 = offset._m0.x;
    float tmp6 = left1_4321_43right_f32_f32(tmp1, tmp5);
    float tmp10 = offset._m0.y;
    float tmp14 = offset._m0.y;
    float tmp15 = left1_4321_43right_f32_f32(tmp10, tmp14);
    float tmp16 = left1_4331_43right_f32_f32(tmp6, tmp15);
    float tmp17 = left1_4351_43right_f32_f32(tmp, tmp16);
    float tmp18 = 0.0;
    float radius = the_maximum_of_left_and_right_f32_f32(tmp17, tmp18);
    float tmp19 = 41.0;
    float tmp20 = left1_4371_43right_f32_f32(permutation, tmp19);
    float tmp21 = the_fractional_part_of_number_f32(tmp20);
    float tmp22 = 2.0;
    float tmp23 = left1_4321_43right_f32_f32(tmp21, tmp22);
    float tmp24 = 1.0;
    float axis = left1_4351_43right_f32_f32(tmp23, tmp24);
    float tmp25 = the_absolute_value_of_magnitude_f32(axis);
    float tmp26 = 0.5;
    float elevation = left1_4351_43right_f32_f32(tmp25, tmp26);
    float tmp27 = 0.5;
    float tmp28 = left1_4331_43right_f32_f32(axis, tmp27);
    float shift = the_floor_of_value_f32(tmp28);
    axis = left1_4351_43right_f32_f32(axis, shift);
    float tmp29 = 1.792842864990234375;
    float tmp30 = 0.8537347316741943359375;
    float tmp31 = left1_4321_43right_f32_f32(axis, axis);
    float tmp32 = left1_4321_43right_f32_f32(elevation, elevation);
    float tmp33 = left1_4331_43right_f32_f32(tmp31, tmp32);
    float tmp34 = left1_4321_43right_f32_f32(tmp30, tmp33);
    float normalization = left1_4351_43right_f32_f32(tmp29, tmp34);
    float attenuation = left1_4321_43right_f32_f32(radius, radius);
    attenuation = left1_4321_43right_f32_f32(attenuation, attenuation);
    float tmp35 = left1_4321_43right_f32_f32(attenuation, normalization);
    float tmp39 = offset._m0.x;
    float tmp40 = left1_4321_43right_f32_f32(axis, tmp39);
    float tmp44 = offset._m0.y;
    float tmp45 = left1_4321_43right_f32_f32(elevation, tmp44);
    float tmp46 = left1_4331_43right_f32_f32(tmp40, tmp45);
    return left1_4321_43right_f32_f32(tmp35, tmp46);
}

float the_simplex_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(_class point, float phase)
{
    float tmp = point._m0.x;
    float tmp1 = 17.1700000762939453125;
    float tmp2 = left1_4321_43right_f32_f32(phase, tmp1);
    vec2 _1383 = vec2(0.0);
    _1383.x = left1_4331_43right_f32_f32(tmp, tmp2);
    float tmp6 = point._m0.y;
    float tmp7 = 11.13000011444091796875;
    float tmp8 = left1_4321_43right_f32_f32(phase, tmp7);
    _1383.y = left1_4351_43right_f32_f32(tmp6, tmp8);
    _class class_tmp = _class(vec2(0.0));
    class_tmp._m0 = _1383;
    _class _sample = class_tmp;
    float tmp14 = _sample._m0.x;
    float tmp18 = _sample._m0.y;
    float tmp19 = left1_4331_43right_f32_f32(tmp14, tmp18);
    float tmp20 = 0.366025388240814208984375;
    float skew = left1_4321_43right_f32_f32(tmp19, tmp20);
    float tmp25 = _sample._m0.x;
    float tmp26 = left1_4331_43right_f32_f32(tmp25, skew);
    vec2 _1405 = vec2(0.0);
    _1405.x = the_floor_of_value_f32(tmp26);
    float tmp31 = _sample._m0.y;
    float tmp32 = left1_4331_43right_f32_f32(tmp31, skew);
    _1405.y = the_floor_of_value_f32(tmp32);
    _class class_tmp21 = _class(vec2(0.0));
    class_tmp21._m0 = _1405;
    _class corner = class_tmp21;
    float tmp39 = corner._m0.x;
    float tmp43 = corner._m0.y;
    float tmp44 = left1_4331_43right_f32_f32(tmp39, tmp43);
    float tmp45 = 0.211324870586395263671875;
    float unskew = left1_4321_43right_f32_f32(tmp44, tmp45);
    float tmp50 = _sample._m0.x;
    float tmp54 = corner._m0.x;
    float tmp55 = left1_4351_43right_f32_f32(tmp54, unskew);
    vec2 _1430 = vec2(0.0);
    _1430.x = left1_4351_43right_f32_f32(tmp50, tmp55);
    float tmp60 = _sample._m0.y;
    float tmp64 = corner._m0.y;
    float tmp65 = left1_4351_43right_f32_f32(tmp64, unskew);
    _1430.y = left1_4351_43right_f32_f32(tmp60, tmp65);
    _class class_tmp46 = _class(vec2(0.0));
    class_tmp46._m0 = _1430;
    _class offset = class_tmp46;
    _class class_tmp69 = _class(vec2(0.0));
    class_tmp69._m0 = vec2(0.0, 1.0);
    _class second = class_tmp69;
    float tmp75 = offset._m0.x;
    float tmp79 = offset._m0.y;
    if (left_2_right_f32_f32(tmp75, tmp79))
    {
        vec2 _1452 = second._m0;
        _1452.x = 1.0;
        second._m0 = _1452;
        vec2 _1456 = second._m0;
        _1456.y = 0.0;
        second._m0 = _1456;
    }
    float tmp91 = offset._m0.x;
    float tmp95 = second._m0.x;
    float tmp96 = left1_4351_43right_f32_f32(tmp91, tmp95);
    float tmp97 = 0.211324870586395263671875;
    vec2 _1467 = vec2(0.0);
    _1467.x = left1_4331_43right_f32_f32(tmp96, tmp97);
    float tmp102 = offset._m0.y;
    float tmp106 = second._m0.y;
    float tmp107 = left1_4351_43right_f32_f32(tmp102, tmp106);
    float tmp108 = 0.211324870586395263671875;
    _1467.y = left1_4331_43right_f32_f32(tmp107, tmp108);
    _class class_tmp87 = _class(vec2(0.0));
    class_tmp87._m0 = _1467;
    _class middle = class_tmp87;
    float tmp116 = offset._m0.x;
    float tmp117 = 0.57735025882720947265625;
    vec2 _1483 = vec2(0.0);
    _1483.x = left1_4351_43right_f32_f32(tmp116, tmp117);
    float tmp122 = offset._m0.y;
    float tmp123 = 0.57735025882720947265625;
    _1483.y = left1_4351_43right_f32_f32(tmp122, tmp123);
    _class class_tmp112 = _class(vec2(0.0));
    class_tmp112._m0 = _1483;
    _class third = class_tmp112;
    float tmp131 = corner._m0.x;
    float tmp135 = corner._m0.x;
    float tmp136 = 289.0;
    float tmp137 = left1_4371_43right_f32_f32(tmp135, tmp136);
    float tmp138 = the_floor_of_value_f32(tmp137);
    float tmp139 = 289.0;
    float tmp140 = left1_4321_43right_f32_f32(tmp138, tmp139);
    vec2 _1501 = vec2(0.0);
    _1501.x = left1_4351_43right_f32_f32(tmp131, tmp140);
    float tmp145 = corner._m0.y;
    float tmp149 = corner._m0.y;
    float tmp150 = 289.0;
    float tmp151 = left1_4371_43right_f32_f32(tmp149, tmp150);
    float tmp152 = the_floor_of_value_f32(tmp151);
    float tmp153 = 289.0;
    float tmp154 = left1_4321_43right_f32_f32(tmp152, tmp153);
    _1501.y = left1_4351_43right_f32_f32(tmp145, tmp154);
    _class class_tmp127 = _class(vec2(0.0));
    class_tmp127._m0 = _1501;
    _class wrapped = class_tmp127;
    float tmp161 = wrapped._m0.y;
    float tmp162 = the_simplex_permutation_of_value_f32(tmp161);
    float tmp166 = wrapped._m0.x;
    float tmp167 = left1_4331_43right_f32_f32(tmp162, tmp166);
    float initial = the_simplex_permutation_of_value_f32(tmp167);
    float tmp171 = wrapped._m0.y;
    float tmp175 = second._m0.y;
    float tmp176 = left1_4331_43right_f32_f32(tmp171, tmp175);
    float tmp177 = the_simplex_permutation_of_value_f32(tmp176);
    float tmp181 = wrapped._m0.x;
    float tmp182 = left1_4331_43right_f32_f32(tmp177, tmp181);
    float tmp186 = second._m0.x;
    float tmp187 = left1_4331_43right_f32_f32(tmp182, tmp186);
    float following = the_simplex_permutation_of_value_f32(tmp187);
    float tmp191 = wrapped._m0.y;
    float tmp192 = 1.0;
    float tmp193 = left1_4331_43right_f32_f32(tmp191, tmp192);
    float tmp194 = the_simplex_permutation_of_value_f32(tmp193);
    float tmp198 = wrapped._m0.x;
    float tmp199 = left1_4331_43right_f32_f32(tmp194, tmp198);
    float tmp200 = 1.0;
    float tmp201 = left1_4331_43right_f32_f32(tmp199, tmp200);
    float final = the_simplex_permutation_of_value_f32(tmp201);
    float leading = the_simplex_corner_at_3a_point8offset5_with_permutation_3a_value8permutation5_a_point_f32(offset, initial);
    float central = the_simplex_corner_at_3a_point8offset5_with_permutation_3a_value8permutation5_a_point_f32(middle, following);
    float trailing = the_simplex_corner_at_3a_point8offset5_with_permutation_3a_value8permutation5_a_point_f32(third, final);
    float tmp202 = left1_4331_43right_f32_f32(leading, central);
    float tmp203 = left1_4331_43right_f32_f32(tmp202, trailing);
    float tmp204 = 130.0;
    return left1_4321_43right_f32_f32(tmp203, tmp204);
}

float number_saturated_f32(float number)
{
    float result = number;
    float tmp = 0.0;
    if (left_0_right_f32_f32(result, tmp))
    {
        result = 0.0;
    }
    float tmp3 = 1.0;
    if (left_2_right_f32_f32(result, tmp3))
    {
        result = 1.0;
    }
    return result;
}

float the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(float lower, float upper, float _sample)
{
    float tmp = left1_4351_43right_f32_f32(_sample, lower);
    float tmp1 = left1_4351_43right_f32_f32(upper, lower);
    float normalized = left1_4371_43right_f32_f32(tmp, tmp1);
    normalized = number_saturated_f32(normalized);
    float tmp2 = left1_4321_43right_f32_f32(normalized, normalized);
    float tmp3 = 3.0;
    float tmp4 = 2.0;
    float tmp5 = left1_4321_43right_f32_f32(tmp4, normalized);
    float tmp6 = left1_4351_43right_f32_f32(tmp3, tmp5);
    return left1_4321_43right_f32_f32(tmp2, tmp6);
}

float the_terrain_height_at_3terrain_coordinate8position5_a_point(class_0 position)
{
    float tmp = position._m0.x;
    float tmp1 = 0.010999999940395355224609375;
    vec2 _1091 = vec2(0.0);
    _1091.x = left1_4321_43right_f32_f32(tmp, tmp1);
    float tmp5 = position._m0.z;
    float tmp6 = 0.010999999940395355224609375;
    _1091.y = left1_4321_43right_f32_f32(tmp5, tmp6);
    _class class_tmp = _class(vec2(0.0));
    class_tmp._m0 = _1091;
    _class tmp9 = class_tmp;
    float tmp10 = 1.7000000476837158203125;
    float continental = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp9, tmp10);
    float tmp16 = position._m0.x;
    float tmp17 = 0.02099999971687793731689453125;
    float tmp18 = left1_4321_43right_f32_f32(tmp16, tmp17);
    float tmp19 = 9.0;
    vec2 _1105 = vec2(0.0);
    _1105.x = left1_4331_43right_f32_f32(tmp18, tmp19);
    float tmp24 = position._m0.z;
    float tmp25 = 0.02099999971687793731689453125;
    float tmp26 = left1_4321_43right_f32_f32(tmp24, tmp25);
    float tmp27 = 4.0;
    _1105.y = left1_4351_43right_f32_f32(tmp26, tmp27);
    _class class_tmp12 = _class(vec2(0.0));
    class_tmp12._m0 = _1105;
    _class tmp31 = class_tmp12;
    float tmp32 = 4.099999904632568359375;
    vec3 _1115 = vec3(0.0);
    _1115.x = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp31, tmp32);
    _1115.y = 0.0;
    float tmp39 = position._m0.x;
    float tmp40 = 0.006000000052154064178466796875;
    float tmp41 = _the_negative_of_4the_opposite_of_453value_f32(tmp40);
    float tmp42 = left1_4321_43right_f32_f32(tmp39, tmp41);
    float tmp46 = position._m0.z;
    float tmp47 = 0.01899999938905239105224609375;
    float tmp48 = left1_4321_43right_f32_f32(tmp46, tmp47);
    vec2 _1127 = vec2(0.0);
    _1127.x = left1_4331_43right_f32_f32(tmp42, tmp48);
    float tmp53 = position._m0.z;
    float tmp54 = 0.007000000216066837310791015625;
    float tmp55 = left1_4321_43right_f32_f32(tmp53, tmp54);
    float tmp59 = position._m0.x;
    float tmp60 = 0.0199999995529651641845703125;
    float tmp61 = left1_4321_43right_f32_f32(tmp59, tmp60);
    _1127.y = left1_4331_43right_f32_f32(tmp55, tmp61);
    _class class_tmp35 = _class(vec2(0.0));
    class_tmp35._m0 = _1127;
    _class tmp65 = class_tmp35;
    float tmp66 = 8.30000019073486328125;
    _1115.z = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp65, tmp66);
    class_0 class_tmp11 = class_0(vec3(0.0));
    class_tmp11._m0 = _1115;
    class_0 warp = class_tmp11;
    float tmp74 = position._m0.x;
    float tmp78 = warp._m0.x;
    float tmp79 = 9.5;
    float tmp80 = left1_4321_43right_f32_f32(tmp78, tmp79);
    vec3 _1152 = vec3(0.0);
    _1152.x = left1_4331_43right_f32_f32(tmp74, tmp80);
    _1152.y = 0.0;
    float tmp86 = position._m0.z;
    float tmp90 = warp._m0.z;
    float tmp91 = 9.5;
    float tmp92 = left1_4321_43right_f32_f32(tmp90, tmp91);
    _1152.z = left1_4331_43right_f32_f32(tmp86, tmp92);
    class_0 class_tmp70 = class_0(vec3(0.0));
    class_tmp70._m0 = _1152;
    class_0 bent = class_tmp70;
    float tmp100 = bent._m0.x;
    float tmp101 = 0.02700000070035457611083984375;
    float tmp102 = left1_4321_43right_f32_f32(tmp100, tmp101);
    float tmp106 = bent._m0.z;
    float tmp107 = 0.006000000052154064178466796875;
    float tmp108 = left1_4321_43right_f32_f32(tmp106, tmp107);
    vec2 _1174 = vec2(0.0);
    _1174.x = left1_4331_43right_f32_f32(tmp102, tmp108);
    float tmp113 = bent._m0.z;
    float tmp114 = 0.02700000070035457611083984375;
    float tmp115 = left1_4321_43right_f32_f32(tmp113, tmp114);
    float tmp119 = bent._m0.x;
    float tmp120 = 0.006000000052154064178466796875;
    float tmp121 = left1_4321_43right_f32_f32(tmp119, tmp120);
    _1174.y = left1_4351_43right_f32_f32(tmp115, tmp121);
    _class class_tmp96 = _class(vec2(0.0));
    class_tmp96._m0 = _1174;
    _class tmp125 = class_tmp96;
    float tmp126 = 2.2999999523162841796875;
    float mountain = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp125, tmp126);
    float tmp131 = bent._m0.x;
    float tmp132 = 0.017999999225139617919921875;
    float tmp133 = _the_negative_of_4the_opposite_of_453value_f32(tmp132);
    float tmp134 = left1_4321_43right_f32_f32(tmp131, tmp133);
    float tmp138 = bent._m0.z;
    float tmp139 = 0.0570000000298023223876953125;
    float tmp140 = left1_4321_43right_f32_f32(tmp138, tmp139);
    vec2 _1198 = vec2(0.0);
    _1198.x = left1_4331_43right_f32_f32(tmp134, tmp140);
    float tmp145 = bent._m0.z;
    float tmp146 = 0.01899999938905239105224609375;
    float tmp147 = left1_4321_43right_f32_f32(tmp145, tmp146);
    float tmp151 = bent._m0.x;
    float tmp152 = 0.0540000014007091522216796875;
    float tmp153 = left1_4321_43right_f32_f32(tmp151, tmp152);
    _1198.y = left1_4331_43right_f32_f32(tmp147, tmp153);
    _class class_tmp127 = _class(vec2(0.0));
    class_tmp127._m0 = _1198;
    _class tmp157 = class_tmp127;
    float tmp158 = 6.900000095367431640625;
    float crossing = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp157, tmp158);
    float tmp163 = bent._m0.x;
    float tmp164 = 0.11900000274181365966796875;
    float tmp165 = left1_4321_43right_f32_f32(tmp163, tmp164);
    float tmp169 = bent._m0.z;
    float tmp170 = 0.041000001132488250732421875;
    float tmp171 = left1_4321_43right_f32_f32(tmp169, tmp170);
    vec2 _1221 = vec2(0.0);
    _1221.x = left1_4331_43right_f32_f32(tmp165, tmp171);
    float tmp176 = bent._m0.z;
    float tmp177 = 0.112999998033046722412109375;
    float tmp178 = left1_4321_43right_f32_f32(tmp176, tmp177);
    float tmp182 = bent._m0.x;
    float tmp183 = 0.037000000476837158203125;
    float tmp184 = left1_4321_43right_f32_f32(tmp182, tmp183);
    _1221.y = left1_4351_43right_f32_f32(tmp178, tmp184);
    _class class_tmp159 = _class(vec2(0.0));
    class_tmp159._m0 = _1221;
    _class tmp188 = class_tmp159;
    float tmp189 = 9.69999980926513671875;
    float broken = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp188, tmp189);
    float tmp190 = 0.60000002384185791015625;
    float tmp191 = left1_4321_43right_f32_f32(mountain, tmp190);
    float tmp192 = 0.2700000107288360595703125;
    float tmp193 = left1_4321_43right_f32_f32(crossing, tmp192);
    float tmp194 = left1_4331_43right_f32_f32(tmp191, tmp193);
    float tmp195 = 0.12999999523162841796875;
    float tmp196 = left1_4321_43right_f32_f32(broken, tmp195);
    float mass = left1_4331_43right_f32_f32(tmp194, tmp196);
    float tmp197 = 0.0;
    float ridge = the_maximum_of_left_and_right_f32_f32(mass, tmp197);
    ridge = left1_4321_43right_f32_f32(ridge, ridge);
    float tmp198 = 1.0;
    float tmp199 = 0.7799999713897705078125;
    float tmp200 = left1_4321_43right_f32_f32(mountain, tmp199);
    float tmp201 = 0.3400000035762786865234375;
    float tmp202 = left1_4321_43right_f32_f32(crossing, tmp201);
    float tmp203 = left1_4331_43right_f32_f32(tmp200, tmp202);
    float tmp204 = the_absolute_value_of_magnitude_f32(tmp203);
    float fold = left1_4351_43right_f32_f32(tmp198, tmp204);
    float tmp205 = 0.4199999868869781494140625;
    float tmp206 = left1_4351_43right_f32_f32(fold, tmp205);
    float tmp207 = 0.0;
    float crest = the_maximum_of_left_and_right_f32_f32(tmp206, tmp207);
    crest = left1_4321_43right_f32_f32(crest, crest);
    float tmp208 = 0.4799999892711639404296875;
    float tmp209 = _the_negative_of_4the_opposite_of_453value_f32(tmp208);
    float tmp210 = 0.449999988079071044921875;
    float uplift = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp209, tmp210, continental);
    float tmp211 = 1.0;
    float tmp212 = 0.0350000001490116119384765625;
    float tmp213 = 0.310000002384185791015625;
    float tmp214 = 0.23999999463558197021484375;
    float tmp215 = left1_4321_43right_f32_f32(broken, tmp214);
    float tmp216 = left1_4331_43right_f32_f32(crossing, tmp215);
    float tmp217 = the_absolute_value_of_magnitude_f32(tmp216);
    float tmp218 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp212, tmp213, tmp217);
    float erosion = left1_4351_43right_f32_f32(tmp211, tmp218);
    float tmp219 = 1.37999999523162841796875;
    float alpine = left1_4321_43right_f32_f32(mass, tmp219);
    float tmp220 = 2.0;
    float tmp221 = 6.0;
    float tmp222 = left1_4321_43right_f32_f32(uplift, tmp221);
    float tmp223 = left1_4331_43right_f32_f32(tmp220, tmp222);
    float tmp224 = left1_4321_43right_f32_f32(ridge, tmp223);
    alpine = left1_4331_43right_f32_f32(alpine, tmp224);
    float tmp225 = left1_4321_43right_f32_f32(crest, uplift);
    float tmp226 = 3.099999904632568359375;
    float tmp227 = 2.400000095367431640625;
    float tmp228 = left1_4321_43right_f32_f32(ridge, tmp227);
    float tmp229 = left1_4331_43right_f32_f32(tmp226, tmp228);
    float tmp230 = left1_4321_43right_f32_f32(tmp225, tmp229);
    alpine = left1_4331_43right_f32_f32(alpine, tmp230);
    float tmp231 = left1_4321_43right_f32_f32(broken, uplift);
    float tmp232 = 0.3400000035762786865234375;
    float tmp233 = left1_4321_43right_f32_f32(tmp231, tmp232);
    alpine = left1_4331_43right_f32_f32(alpine, tmp233);
    float tmp234 = left1_4321_43right_f32_f32(erosion, uplift);
    float tmp235 = 0.1599999964237213134765625;
    float tmp236 = 0.37999999523162841796875;
    float tmp237 = left1_4321_43right_f32_f32(ridge, tmp236);
    float tmp238 = left1_4331_43right_f32_f32(tmp235, tmp237);
    float tmp239 = left1_4321_43right_f32_f32(tmp234, tmp238);
    alpine = left1_4351_43right_f32_f32(alpine, tmp239);
    float tmp240 = 1.2000000476837158203125;
    float basin = left1_4321_43right_f32_f32(continental, tmp240);
    float tmp241 = left1_4331_43right_f32_f32(basin, alpine);
    float tmp242 = 0.75;
    return left1_4351_43right_f32_f32(tmp241, tmp242);
}

float the_water_detail_visibility_at_distance_f32(float _distance)
{
    float tmp = 1.0;
    float tmp1 = 48.0;
    float tmp2 = 96.0;
    float tmp3 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp1, tmp2, _distance);
    return left1_4351_43right_f32_f32(tmp, tmp3);
}

void main()
{
    float time = dynlexUniform0.value;
    float tmp = dynlexUniform1.value;
    float tmp1 = 1.0;
    vec2 _411 = vec2(0.0);
    _411.x = the_maximum_of_left_and_right_f32_f32(tmp, tmp1);
    float tmp2 = dynlexUniform2.value;
    float tmp3 = 1.0;
    _411.y = the_maximum_of_left_and_right_f32_f32(tmp2, tmp3);
    _class class_tmp = _class(vec2(0.0));
    class_tmp._m0 = _411;
    _class frame = class_tmp;
    float tmp6 = frame._m0.x;
    float tmp10 = frame._m0.y;
    float aspect = left1_4371_43right_f32_f32(tmp6, tmp10);
    float surface = in_Position.w;
    float tmp12 = 0.5;
    if (left_0_right_f32_f32(surface, tmp12))
    {
        vec4 _858 = vec4(0.0);
        _858.y = in_Position.y;
        _858.x = in_Position.x;
        dynlex_interpolant_7465727261696e20706f736974696f6e = _858;
        dynlex_interpolant_7465727261696e206e6f726d616c = vec4(0.0, 1.0, 0.0, 0.0);
        dynlex_interpolant_7465727261696e206d6174657269616c = vec4(0.5, 0.0, 0.0, 0.0);
        vec4 _862 = vec4(0.0);
        _862.x = in_Position.x;
        _862.y = in_Position.y;
        _862.z = 0.99989998340606689453125;
        _862.w = 1.0;
        gl_Position = _862;
    }
    else
    {
        float _distance = in_Position.y;
        class_0 camera = the_terrain_camera_at_moment_f32(time);
        float pitch = 0.1550000011920928955078125;
        float sine = the_sine_of_value_f32(pitch);
        float cosine = the_cosine_of_value_f32(pitch);
        float tmp27 = 0.07299999892711639404296875;
        float tmp28 = left1_4321_43right_f32_f32(time, tmp27);
        float tmp29 = the_sine_of_value_f32(tmp28);
        float tmp30 = 0.12999999523162841796875;
        float tmp31 = left1_4321_43right_f32_f32(tmp29, tmp30);
        float tmp32 = 0.041000001132488250732421875;
        float tmp33 = left1_4321_43right_f32_f32(time, tmp32);
        float tmp34 = the_cosine_of_value_f32(tmp33);
        float tmp35 = 0.04500000178813934326171875;
        float tmp36 = left1_4321_43right_f32_f32(tmp34, tmp35);
        float yaw = left1_4331_43right_f32_f32(tmp31, tmp36);
        float turning = the_sine_of_value_f32(yaw);
        float alignment = the_cosine_of_value_f32(yaw);
        float tmp37 = 0.0;
        float tmp41 = camera._m0.y;
        float vertical = left1_4351_43right_f32_f32(tmp37, tmp41);
        float tmp44 = in_Position.x;
        float tmp45 = left1_4321_43right_f32_f32(tmp44, aspect);
        float tmp46 = 0.800000011920928955078125;
        float slope = left1_4321_43right_f32_f32(tmp45, tmp46);
        float tmp47 = 1.0;
        float tmp48 = left1_4321_43right_f32_f32(slope, cosine);
        float tmp49 = left1_4321_43right_f32_f32(slope, cosine);
        float tmp50 = left1_4321_43right_f32_f32(tmp48, tmp49);
        float tmp51 = left1_4331_43right_f32_f32(tmp47, tmp50);
        float scale = the_square_root_of_value_f32(tmp51);
        float forward = left1_4371_43right_f32_f32(_distance, scale);
        float tmp52 = left1_4321_43right_f32_f32(forward, cosine);
        float tmp53 = left1_4321_43right_f32_f32(vertical, sine);
        float depth = left1_4351_43right_f32_f32(tmp52, tmp53);
        float lateral = left1_4321_43right_f32_f32(slope, depth);
        float tmp58 = camera._m0.x;
        float tmp59 = left1_4321_43right_f32_f32(lateral, alignment);
        float tmp60 = left1_4331_43right_f32_f32(tmp58, tmp59);
        float tmp61 = left1_4321_43right_f32_f32(forward, turning);
        vec3 _466 = vec3(0.0);
        _466.x = left1_4331_43right_f32_f32(tmp60, tmp61);
        _466.y = 0.0;
        float tmp67 = camera._m0.z;
        float tmp68 = left1_4321_43right_f32_f32(forward, alignment);
        float tmp69 = left1_4331_43right_f32_f32(tmp67, tmp68);
        float tmp70 = left1_4321_43right_f32_f32(lateral, turning);
        _466.z = left1_4351_43right_f32_f32(tmp69, tmp70);
        class_0 class_tmp54 = class_0(vec3(0.0));
        class_tmp54._m0 = _466;
        class_0 world = class_tmp54;
        float elevation = 0.0;
        class_0 class_tmp74 = class_0(vec3(0.0));
        class_tmp74._m0 = vec3(0.0, 1.0, 0.0);
        class_0 normal = class_tmp74;
        float tmp79 = 1.5;
        float _728 = 0.0;
        float _729 = 0.0;
        float _730 = 0.0;
        if (left_2_right_f32_f32(surface, tmp79))
        {
            float tmp80 = 0.62000000476837158203125;
            float level = _the_negative_of_4the_opposite_of_453value_f32(tmp80);
            float tmp81 = the_terrain_height_at_3terrain_coordinate8position5_a_point(world);
            float tmp82 = left1_4351_43right_f32_f32(level, tmp81);
            float tmp83 = 0.0;
            float tmp88 = world._m0.x;
            float tmp89 = 0.17000000178813934326171875;
            float tmp90 = left1_4321_43right_f32_f32(tmp88, tmp89);
            float tmp94 = world._m0.z;
            float tmp95 = 0.070000000298023223876953125;
            float tmp96 = left1_4321_43right_f32_f32(tmp94, tmp95);
            float tmp97 = left1_4331_43right_f32_f32(tmp90, tmp96);
            float tmp98 = 0.4099999964237213134765625;
            float tmp99 = left1_4321_43right_f32_f32(time, tmp98);
            vec3 _594 = vec3(0.0);
            _594.x = left1_4331_43right_f32_f32(tmp97, tmp99);
            float tmp104 = world._m0.z;
            float tmp105 = 0.20999999344348907470703125;
            float tmp106 = left1_4321_43right_f32_f32(tmp104, tmp105);
            float tmp110 = world._m0.x;
            float tmp111 = 0.0500000007450580596923828125;
            float tmp112 = left1_4321_43right_f32_f32(tmp110, tmp111);
            float tmp113 = left1_4351_43right_f32_f32(tmp106, tmp112);
            float tmp114 = 0.319999992847442626953125;
            float tmp115 = left1_4321_43right_f32_f32(time, tmp114);
            _594.y = left1_4351_43right_f32_f32(tmp113, tmp115);
            float tmp120 = world._m0.x;
            float tmp121 = 0.0900000035762786865234375;
            float tmp122 = left1_4321_43right_f32_f32(tmp120, tmp121);
            float tmp126 = world._m0.z;
            float tmp127 = 0.12999999523162841796875;
            float tmp128 = left1_4321_43right_f32_f32(tmp126, tmp127);
            float tmp129 = left1_4331_43right_f32_f32(tmp122, tmp128);
            float tmp130 = 0.23000000417232513427734375;
            float tmp131 = left1_4321_43right_f32_f32(time, tmp130);
            _594.z = left1_4331_43right_f32_f32(tmp129, tmp131);
            class_0 class_tmp84 = class_0(vec3(0.0));
            class_tmp84._m0 = _594;
            class_0 angle = class_tmp84;
            float tmp139 = angle._m0.x;
            vec3 _625 = vec3(0.0);
            _625.x = the_sine_of_value_f32(tmp139);
            float tmp144 = angle._m0.y;
            _625.y = the_sine_of_value_f32(tmp144);
            float tmp149 = angle._m0.z;
            _625.z = the_sine_of_value_f32(tmp149);
            class_0 class_tmp135 = class_0(vec3(0.0));
            class_tmp135._m0 = _625;
            class_0 wave = class_tmp135;
            float tmp157 = angle._m0.x;
            vec3 _642 = vec3(0.0);
            _642.x = the_cosine_of_value_f32(tmp157);
            float tmp162 = angle._m0.y;
            _642.y = the_cosine_of_value_f32(tmp162);
            float tmp167 = angle._m0.z;
            _642.z = the_cosine_of_value_f32(tmp167);
            class_0 class_tmp153 = class_0(vec3(0.0));
            class_tmp153._m0 = _642;
            class_0 oscillation = class_tmp153;
            float visibility = the_water_detail_visibility_at_distance_f32(_distance);
            float tmp174 = wave._m0.x;
            float tmp175 = 0.04500000178813934326171875;
            float tmp176 = left1_4321_43right_f32_f32(tmp174, tmp175);
            float tmp180 = wave._m0.y;
            float tmp181 = 0.0280000008642673492431640625;
            float tmp182 = left1_4321_43right_f32_f32(tmp180, tmp181);
            float tmp183 = left1_4331_43right_f32_f32(tmp176, tmp182);
            float tmp187 = wave._m0.z;
            float tmp188 = 0.017999999225139617919921875;
            float tmp189 = left1_4321_43right_f32_f32(tmp187, tmp188);
            float tmp190 = left1_4331_43right_f32_f32(tmp183, tmp189);
            float displacement = left1_4321_43right_f32_f32(tmp190, visibility);
            elevation = left1_4331_43right_f32_f32(level, displacement);
            vec3 _673 = normal._m0;
            float tmp193 = 0.0;
            float tmp197 = oscillation._m0.x;
            float tmp198 = 0.007650000043213367462158203125;
            float tmp199 = left1_4321_43right_f32_f32(tmp197, tmp198);
            float tmp203 = oscillation._m0.y;
            float tmp204 = 0.0013999999500811100006103515625;
            float tmp205 = left1_4321_43right_f32_f32(tmp203, tmp204);
            float tmp206 = left1_4351_43right_f32_f32(tmp199, tmp205);
            float tmp210 = oscillation._m0.z;
            float tmp211 = 0.00161999999545514583587646484375;
            float tmp212 = left1_4321_43right_f32_f32(tmp210, tmp211);
            float tmp213 = left1_4331_43right_f32_f32(tmp206, tmp212);
            float tmp214 = left1_4351_43right_f32_f32(tmp193, tmp213);
            _673.x = left1_4321_43right_f32_f32(tmp214, visibility);
            normal._m0 = _673;
            vec3 _693 = normal._m0;
            float tmp218 = 0.0;
            float tmp222 = oscillation._m0.x;
            float tmp223 = 0.00315000000409781932830810546875;
            float tmp224 = left1_4321_43right_f32_f32(tmp222, tmp223);
            float tmp228 = oscillation._m0.y;
            float tmp229 = 0.00588000006973743438720703125;
            float tmp230 = left1_4321_43right_f32_f32(tmp228, tmp229);
            float tmp231 = left1_4331_43right_f32_f32(tmp224, tmp230);
            float tmp235 = oscillation._m0.z;
            float tmp236 = 0.00233999988995492458343505859375;
            float tmp237 = left1_4321_43right_f32_f32(tmp235, tmp236);
            float tmp238 = left1_4331_43right_f32_f32(tmp231, tmp237);
            float tmp239 = left1_4351_43right_f32_f32(tmp218, tmp238);
            _693.z = left1_4321_43right_f32_f32(tmp239, visibility);
            normal._m0 = _693;
            float tmp242 = 0.5;
            float tmp246 = wave._m0.x;
            float tmp250 = wave._m0.y;
            float tmp251 = left1_4331_43right_f32_f32(tmp246, tmp250);
            float tmp252 = 0.25;
            float tmp253 = left1_4321_43right_f32_f32(tmp251, tmp252);
            float tmp254 = left1_4321_43right_f32_f32(tmp253, visibility);
            float tmp255 = 0.5;
            float tmp259 = wave._m0.z;
            float tmp260 = 0.5;
            float tmp261 = left1_4321_43right_f32_f32(tmp259, tmp260);
            float tmp262 = left1_4321_43right_f32_f32(tmp261, visibility);
            _728 = left1_4331_43right_f32_f32(tmp242, tmp254);
            _729 = left1_4331_43right_f32_f32(tmp255, tmp262);
            _730 = the_maximum_of_left_and_right_f32_f32(tmp82, tmp83);
        }
        else
        {
            float stride = 0.3400000035762786865234375;
            elevation = the_terrain_height_at_3terrain_coordinate8position5_a_point(world);
            float tmp268 = world._m0.x;
            vec3 _486 = vec3(0.0);
            _486.x = left1_4351_43right_f32_f32(tmp268, stride);
            _486.y = 0.0;
            _486.z = world._m0.z;
            class_0 class_tmp264 = class_0(vec3(0.0));
            class_tmp264._m0 = _486;
            class_0 tmp277 = class_tmp264;
            float west = the_terrain_height_at_3terrain_coordinate8position5_a_point(tmp277);
            float tmp282 = world._m0.x;
            vec3 _499 = vec3(0.0);
            _499.x = left1_4331_43right_f32_f32(tmp282, stride);
            _499.y = 0.0;
            _499.z = world._m0.z;
            class_0 class_tmp278 = class_0(vec3(0.0));
            class_tmp278._m0 = _499;
            class_0 tmp291 = class_tmp278;
            float east = the_terrain_height_at_3terrain_coordinate8position5_a_point(tmp291);
            vec3 _511 = vec3(0.0);
            _511.x = world._m0.x;
            _511.y = 0.0;
            float tmp301 = world._m0.z;
            _511.z = left1_4351_43right_f32_f32(tmp301, stride);
            class_0 class_tmp292 = class_0(vec3(0.0));
            class_tmp292._m0 = _511;
            class_0 tmp305 = class_tmp292;
            float south = the_terrain_height_at_3terrain_coordinate8position5_a_point(tmp305);
            vec3 _524 = vec3(0.0);
            _524.x = world._m0.x;
            _524.y = 0.0;
            float tmp315 = world._m0.z;
            _524.z = left1_4331_43right_f32_f32(tmp315, stride);
            class_0 class_tmp306 = class_0(vec3(0.0));
            class_tmp306._m0 = _524;
            class_0 tmp319 = class_tmp306;
            float north = the_terrain_height_at_3terrain_coordinate8position5_a_point(tmp319);
            vec3 _535 = normal._m0;
            _535.x = left1_4351_43right_f32_f32(west, east);
            normal._m0 = _535;
            vec3 _540 = normal._m0;
            float tmp326 = 2.0;
            _540.y = left1_4321_43right_f32_f32(stride, tmp326);
            normal._m0 = _540;
            vec3 _545 = normal._m0;
            _545.z = left1_4351_43right_f32_f32(south, north);
            normal._m0 = _545;
            float tmp337 = world._m0.x;
            float tmp338 = 0.064999997615814208984375;
            vec2 _553 = vec2(0.0);
            _553.x = left1_4321_43right_f32_f32(tmp337, tmp338);
            float tmp343 = world._m0.z;
            float tmp344 = 0.064999997615814208984375;
            _553.y = left1_4321_43right_f32_f32(tmp343, tmp344);
            _class class_tmp333 = _class(vec2(0.0));
            class_tmp333._m0 = _553;
            _class tmp348 = class_tmp333;
            float tmp349 = 13.69999980926513671875;
            float tmp350 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp348, tmp349);
            float tmp351 = 0.5;
            float tmp352 = left1_4321_43right_f32_f32(tmp350, tmp351);
            float tmp353 = 0.5;
            float tmp358 = world._m0.x;
            float tmp359 = 0.14000000059604644775390625;
            vec2 _568 = vec2(0.0);
            _568.x = left1_4321_43right_f32_f32(tmp358, tmp359);
            float tmp364 = world._m0.z;
            float tmp365 = 0.14000000059604644775390625;
            _568.y = left1_4321_43right_f32_f32(tmp364, tmp365);
            _class class_tmp354 = _class(vec2(0.0));
            class_tmp354._m0 = _568;
            _class tmp369 = class_tmp354;
            float tmp370 = 16.200000762939453125;
            float tmp371 = the_simplex_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp369, tmp370);
            float tmp372 = 0.5;
            float tmp373 = left1_4321_43right_f32_f32(tmp371, tmp372);
            float tmp374 = 0.5;
            _728 = left1_4331_43right_f32_f32(tmp352, tmp353);
            _729 = left1_4331_43right_f32_f32(tmp373, tmp374);
            _730 = 0.0;
        }
        float tmp378 = normal._m0.x;
        float tmp382 = normal._m0.x;
        float tmp383 = left1_4321_43right_f32_f32(tmp378, tmp382);
        float tmp387 = normal._m0.y;
        float tmp391 = normal._m0.y;
        float tmp392 = left1_4321_43right_f32_f32(tmp387, tmp391);
        float tmp393 = left1_4331_43right_f32_f32(tmp383, tmp392);
        float tmp397 = normal._m0.z;
        float tmp401 = normal._m0.z;
        float tmp402 = left1_4321_43right_f32_f32(tmp397, tmp401);
        float tmp403 = left1_4331_43right_f32_f32(tmp393, tmp402);
        float _length = the_square_root_of_value_f32(tmp403);
        vec3 _756 = normal._m0;
        float tmp409 = normal._m0.x;
        _756.x = left1_4371_43right_f32_f32(tmp409, _length);
        normal._m0 = _756;
        vec3 _764 = normal._m0;
        float tmp417 = normal._m0.y;
        _764.y = left1_4371_43right_f32_f32(tmp417, _length);
        normal._m0 = _764;
        vec3 _772 = normal._m0;
        float tmp425 = normal._m0.z;
        _772.z = left1_4371_43right_f32_f32(tmp425, _length);
        normal._m0 = _772;
        float tmp431 = camera._m0.y;
        vertical = left1_4351_43right_f32_f32(elevation, tmp431);
        float tmp433 = left1_4321_43right_f32_f32(vertical, cosine);
        float tmp434 = left1_4321_43right_f32_f32(forward, sine);
        vec2 _786 = vec2(0.0);
        _786.x = left1_4331_43right_f32_f32(tmp433, tmp434);
        float tmp436 = left1_4321_43right_f32_f32(forward, cosine);
        float tmp437 = left1_4321_43right_f32_f32(vertical, sine);
        _786.y = left1_4351_43right_f32_f32(tmp436, tmp437);
        _class class_tmp432 = _class(vec2(0.0));
        class_tmp432._m0 = _786;
        _class view = class_tmp432;
        float tmp442 = 0.7200000286102294921875;
        float tmp443 = left1_4321_43right_f32_f32(aspect, tmp442);
        vec3 _795 = vec3(0.0);
        _795.x = left1_4371_43right_f32_f32(lateral, tmp443);
        float tmp448 = view._m0.x;
        float tmp449 = 0.7200000286102294921875;
        _795.y = left1_4371_43right_f32_f32(tmp448, tmp449);
        float tmp454 = view._m0.y;
        float tmp455 = 1.00077998638153076171875;
        float tmp456 = left1_4321_43right_f32_f32(tmp454, tmp455);
        float tmp457 = 0.400160014629364013671875;
        _795.z = left1_4351_43right_f32_f32(tmp456, tmp457);
        class_0 class_tmp441 = class_0(vec3(0.0));
        class_tmp441._m0 = _795;
        class_0 clip = class_tmp441;
        vec4 _817 = vec4(0.0);
        _817.w = surface;
        _817.z = world._m0.z;
        _817.y = elevation;
        _817.x = world._m0.x;
        dynlex_interpolant_7465727261696e20706f736974696f6e = _817;
        vec4 _831 = vec4(0.0);
        _831.w = _distance;
        _831.z = normal._m0.z;
        _831.y = normal._m0.y;
        _831.x = normal._m0.x;
        dynlex_interpolant_7465727261696e206e6f726d616c = _831;
        vec4 _835 = vec4(0.0);
        _835.z = _730;
        _835.y = _729;
        _835.x = _728;
        dynlex_interpolant_7465727261696e206d6174657269616c = _835;
        vec4 _841 = vec4(0.0);
        _841.x = clip._m0.x;
        _841.y = clip._m0.y;
        _841.z = clip._m0.z;
        _841.w = view._m0.y;
        gl_Position = _841;
    }
}
