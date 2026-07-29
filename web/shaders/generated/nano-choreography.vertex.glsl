#version 300 es

struct class_0
{
    float _m0;
    float _m1;
};

struct _class
{
    float _m0;
    float _m1;
    float _m2;
};

layout(std140) uniform DynlexUniformBlock0
{
    float value;
} dynlexUniform0;

layout(std140) uniform DynlexUniformBlock2
{
    float value;
} dynlexUniform2;

layout(std140) uniform DynlexUniformBlock3
{
    float value;
} dynlexUniform3;

layout(std140) uniform DynlexUniformBlock1
{
    float value;
} dynlexUniform1;

layout(location = 0) in vec4 in_Position;

float left1_4371_43right_f32_f32(float left, float right)
{
    return left / right;
}

float the_floor_of_value_f32(float value)
{
    return floor(value);
}

float left1_4321_43right_f32_f32(float left, float right)
{
    return left * right;
}

float left1_4351_43right_f32_f32(float left, float right)
{
    return left - right;
}

float the_maximum_of_left_and_right_f32_f32(float left, float right)
{
    return isnan(right) ? left : (isnan(left) ? right : max(left, right));
}

float the_fractional_part_of_number_f32(float number)
{
    float tmp = the_floor_of_value_f32(number);
    return left1_4351_43right_f32_f32(number, tmp);
}

float left1_4331_43right_f32_f32(float left, float right)
{
    return left + right;
}

bool left_0_right_f32_f32(float left, float right)
{
    return left < right;
}

bool left_2_right_f32_f32(float left, float right)
{
    return left > right;
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

float the_scene_window_from_3float8opening5_to_3float8closing5_at_3float8moment5_f32_f32_f32(float opening, float closing, float moment)
{
    float tmp = 0.550000011920928955078125;
    float tmp1 = left1_4331_43right_f32_f32(opening, tmp);
    float arrival = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(opening, tmp1, moment);
    float tmp2 = 1.0;
    float tmp3 = 0.550000011920928955078125;
    float tmp4 = left1_4351_43right_f32_f32(closing, tmp3);
    float tmp5 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp4, closing, moment);
    float departure = left1_4351_43right_f32_f32(tmp2, tmp5);
    return left1_4321_43right_f32_f32(arrival, departure);
}

float _the_negative_of_4the_opposite_of_453value_f32(float value)
{
    return -value;
}

float the_sine_of_value_f32(float value)
{
    return sin(value);
}

float the_cosine_of_value_f32(float value)
{
    return cos(value);
}

bool left_1is_greater_than_or_equal_to4213_right_f32_f32(float left, float right)
{
    return left >= right;
}

void main()
{
    _class class_tmp = _class(0.0, 0.0, 0.0);
    class_tmp._m0 = in_Position.x;
    class_tmp._m1 = in_Position.y;
    class_tmp._m2 = in_Position.z;
    _class point = class_tmp;
    float encoding = in_Position.w;
    float tmp = 3.0;
    float tmp9 = left1_4371_43right_f32_f32(encoding, tmp);
    float group = the_floor_of_value_f32(tmp9);
    float tmp10 = 3.0;
    float tmp11 = left1_4321_43right_f32_f32(group, tmp10);
    float corner = left1_4351_43right_f32_f32(encoding, tmp11);
    float time = dynlexUniform0.value;
    float tmp13 = dynlexUniform2.value;
    float tmp14 = 1.0;
    class_0 class_tmp12 = class_0(0.0, 0.0);
    class_tmp12._m0 = the_maximum_of_left_and_right_f32_f32(tmp13, tmp14);
    float tmp16 = dynlexUniform3.value;
    float tmp17 = 1.0;
    class_tmp12._m1 = the_maximum_of_left_and_right_f32_f32(tmp16, tmp17);
    class_0 frame = class_tmp12;
    float pass = dynlexUniform1.value;
    float aspect = left1_4371_43right_f32_f32(frame._m0, frame._m1);
    float tmp22 = 11.0;
    float tmp23 = left1_4371_43right_f32_f32(time, tmp22);
    float tmp24 = the_fractional_part_of_number_f32(tmp23);
    float tmp25 = 11.0;
    float moment = left1_4321_43right_f32_f32(tmp24, tmp25);
    float tmp26 = 7.179999828338623046875;
    float tmp27 = 11.0;
    float visibility = the_scene_window_from_3float8opening5_to_3float8closing5_at_3float8moment5_f32_f32_f32(tmp26, tmp27, moment);
    float tmp28 = 1.12000000476837158203125;
    float tmp29 = _the_negative_of_4the_opposite_of_453value_f32(tmp28);
    float tmp30 = 1.08000004291534423828125;
    float tmp33 = 11.0;
    float tmp34 = left1_4321_43right_f32_f32(point._m0, tmp33);
    float tmp35 = 1.7000000476837158203125;
    float tmp36 = left1_4321_43right_f32_f32(time, tmp35);
    float tmp37 = left1_4331_43right_f32_f32(tmp34, tmp36);
    float tmp38 = the_sine_of_value_f32(tmp37);
    float tmp39 = 0.07999999821186065673828125;
    float tmp40 = left1_4321_43right_f32_f32(tmp38, tmp39);
    float tmp41 = left1_4331_43right_f32_f32(point._m1, tmp40);
    float wave = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp29, tmp30, tmp41);
    visibility = left1_4321_43right_f32_f32(visibility, wave);
    float tmp42 = 0.310000002384185791015625;
    float tmp43 = left1_4321_43right_f32_f32(time, tmp42);
    float tmp44 = the_sine_of_value_f32(tmp43);
    float tmp45 = 0.3400000035762786865234375;
    float yaw = left1_4321_43right_f32_f32(tmp44, tmp45);
    float sine = the_sine_of_value_f32(yaw);
    float cosine = the_cosine_of_value_f32(yaw);
    float tmp48 = left1_4321_43right_f32_f32(point._m0, cosine);
    float tmp50 = left1_4321_43right_f32_f32(point._m2, sine);
    _class class_tmp46 = _class(0.0, 0.0, 0.0);
    class_tmp46._m0 = left1_4331_43right_f32_f32(tmp48, tmp50);
    class_tmp46._m1 = point._m1;
    float tmp55 = left1_4321_43right_f32_f32(point._m2, cosine);
    float tmp57 = left1_4321_43right_f32_f32(point._m0, sine);
    class_tmp46._m2 = left1_4351_43right_f32_f32(tmp55, tmp57);
    _class turned = class_tmp46;
    float tmp60 = 3.25;
    float tmp62 = 0.519999980926513671875;
    float tmp63 = left1_4321_43right_f32_f32(turned._m2, tmp62);
    float depth = left1_4331_43right_f32_f32(tmp60, tmp63);
    float tmp64 = 1.03999996185302734375;
    float tmp65 = 0.017999999225139617919921875;
    float tmp66 = left1_4321_43right_f32_f32(group, tmp65);
    float tmp67 = left1_4331_43right_f32_f32(tmp64, tmp66);
    float tmp69 = left1_4371_43right_f32_f32(tmp67, frame._m0);
    float tmp70 = 0.959999978542327880859375;
    float tmp71 = 0.039999999105930328369140625;
    float tmp72 = left1_4321_43right_f32_f32(pass, tmp71);
    float tmp73 = left1_4331_43right_f32_f32(tmp70, tmp72);
    float size = left1_4321_43right_f32_f32(tmp69, tmp73);
    float tmp74 = 1.65999996662139892578125;
    float tmp75 = 1.0;
    float tmp76 = the_maximum_of_left_and_right_f32_f32(aspect, tmp75);
    float scale = left1_4371_43right_f32_f32(tmp74, tmp76);
    float tmp77 = 1.0;
    float tmp78 = 1.519999980926513671875;
    float tmp79 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp77, tmp78, aspect);
    float tmp80 = 0.4000000059604644775390625;
    float offset = left1_4321_43right_f32_f32(tmp79, tmp80);
    float tmp82 = 0.0;
    class_0 class_tmp81 = class_0(0.0, 0.0);
    class_tmp81._m0 = left1_4351_43right_f32_f32(tmp82, size);
    float tmp84 = 0.0;
    class_tmp81._m1 = left1_4351_43right_f32_f32(tmp84, size);
    class_0 triangle = class_tmp81;
    float tmp87 = 1.0;
    if (left_1is_greater_than_or_equal_to4213_right_f32_f32(corner, tmp87))
    {
        triangle._m0 = size;
    }
    float tmp91 = 2.0;
    if (left_1is_greater_than_or_equal_to4213_right_f32_f32(corner, tmp91))
    {
        triangle._m0 = 0.0;
        float tmp93 = 1.4500000476837158203125;
        triangle._m1 = left1_4321_43right_f32_f32(size, tmp93);
    }
    float tmp97 = left1_4321_43right_f32_f32(turned._m0, scale);
    float tmp98 = left1_4321_43right_f32_f32(offset, depth);
    float tmp99 = left1_4331_43right_f32_f32(tmp97, tmp98);
    float tmp101 = left1_4321_43right_f32_f32(triangle._m0, depth);
    class_0 class_tmp95 = class_0(0.0, 0.0);
    class_tmp95._m0 = left1_4331_43right_f32_f32(tmp99, tmp101);
    float tmp104 = 1.86000001430511474609375;
    float tmp105 = left1_4321_43right_f32_f32(point._m1, tmp104);
    float tmp106 = 0.039999999105930328369140625;
    float tmp107 = left1_4351_43right_f32_f32(tmp105, tmp106);
    float tmp109 = left1_4321_43right_f32_f32(triangle._m1, depth);
    class_tmp95._m1 = left1_4331_43right_f32_f32(tmp107, tmp109);
    class_0 clip = class_tmp95;
    float tmp114 = 0.001000000047497451305389404296875;
    if (left_0_right_f32_f32(visibility, tmp114))
    {
        float tmp115 = 3.0;
        clip._m0 = left1_4321_43right_f32_f32(depth, tmp115);
    }
    float tmp120 = 0.4199999868869781494140625;
    vec4 _295 = vec4(0.0);
    _295.w = depth;
    _295.z = left1_4321_43right_f32_f32(depth, tmp120);
    _295.y = clip._m1;
    _295.x = clip._m0;
    gl_Position = _295;
}
