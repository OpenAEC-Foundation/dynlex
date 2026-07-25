#version 300 es

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

float _the43_floor_of_value_f32(float value)
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

float _the43_maximum_of_a_and_b_f32_f32(float a, float b)
{
    return isnan(b) ? a : (isnan(a) ? b : max(a, b));
}

float fractional_part_of_number_f32(float number)
{
    float tmp = _the43_floor_of_value_f32(number);
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

float saturate_number_f32(float number)
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

float smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(float lower, float upper, float _sample)
{
    float tmp = left1_4351_43right_f32_f32(_sample, lower);
    float tmp1 = left1_4351_43right_f32_f32(upper, lower);
    float normalized = left1_4371_43right_f32_f32(tmp, tmp1);
    normalized = saturate_number_f32(normalized);
    float tmp2 = left1_4321_43right_f32_f32(normalized, normalized);
    float tmp3 = 3.0;
    float tmp4 = 2.0;
    float tmp5 = left1_4321_43right_f32_f32(tmp4, normalized);
    float tmp6 = left1_4351_43right_f32_f32(tmp3, tmp5);
    return left1_4321_43right_f32_f32(tmp2, tmp6);
}

float scene_window_from_opening_to_closing_at_moment_f32_f32_f32(float opening, float closing, float moment)
{
    float tmp = 0.550000011920928955078125;
    float tmp1 = left1_4331_43right_f32_f32(opening, tmp);
    float arrival = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(opening, tmp1, moment);
    float tmp2 = 1.0;
    float tmp3 = 0.550000011920928955078125;
    float tmp4 = left1_4351_43right_f32_f32(closing, tmp3);
    float tmp5 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp4, closing, moment);
    float departure = left1_4351_43right_f32_f32(tmp2, tmp5);
    return left1_4321_43right_f32_f32(arrival, departure);
}

float _the_431negative_1of_34opposite_1of_3453value_f32(float value)
{
    return -value;
}

float _the43_sine_of_value_f32(float value)
{
    return sin(value);
}

float _the43_cosine_of_value_f32(float value)
{
    return cos(value);
}

bool left_1is_greater_than_or_equal_to4213_right_f32_f32(float left, float right)
{
    return left >= right;
}

void main()
{
    float point_x = in_Position.x;
    float point_y = in_Position.y;
    float point_z = in_Position.z;
    float encoded_point = in_Position.w;
    float tmp = 3.0;
    float tmp7 = left1_4371_43right_f32_f32(encoded_point, tmp);
    float point_group = _the43_floor_of_value_f32(tmp7);
    float tmp8 = 3.0;
    float tmp9 = left1_4321_43right_f32_f32(point_group, tmp8);
    float triangle_corner = left1_4351_43right_f32_f32(encoded_point, tmp9);
    float time = dynlexUniform0.value;
    float tmp10 = dynlexUniform2.value;
    float tmp11 = 1.0;
    float width = _the43_maximum_of_a_and_b_f32_f32(tmp10, tmp11);
    float tmp12 = dynlexUniform3.value;
    float tmp13 = 1.0;
    float height = _the43_maximum_of_a_and_b_f32_f32(tmp12, tmp13);
    float render_pass = dynlexUniform1.value;
    float aspect = left1_4371_43right_f32_f32(width, height);
    float tmp14 = 11.0;
    float tmp15 = left1_4371_43right_f32_f32(time, tmp14);
    float tmp16 = fractional_part_of_number_f32(tmp15);
    float tmp17 = 11.0;
    float moment = left1_4321_43right_f32_f32(tmp16, tmp17);
    float tmp18 = 7.179999828338623046875;
    float tmp19 = 11.0;
    float visibility = scene_window_from_opening_to_closing_at_moment_f32_f32_f32(tmp18, tmp19, moment);
    float tmp20 = 1.12000000476837158203125;
    float tmp21 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp20);
    float tmp22 = 1.08000004291534423828125;
    float tmp23 = 11.0;
    float tmp24 = left1_4321_43right_f32_f32(point_x, tmp23);
    float tmp25 = 1.7000000476837158203125;
    float tmp26 = left1_4321_43right_f32_f32(time, tmp25);
    float tmp27 = left1_4331_43right_f32_f32(tmp24, tmp26);
    float tmp28 = _the43_sine_of_value_f32(tmp27);
    float tmp29 = 0.07999999821186065673828125;
    float tmp30 = left1_4321_43right_f32_f32(tmp28, tmp29);
    float tmp31 = left1_4331_43right_f32_f32(point_y, tmp30);
    float build_wave = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp21, tmp22, tmp31);
    visibility = left1_4321_43right_f32_f32(visibility, build_wave);
    float tmp32 = 0.310000002384185791015625;
    float tmp33 = left1_4321_43right_f32_f32(time, tmp32);
    float tmp34 = _the43_sine_of_value_f32(tmp33);
    float tmp35 = 0.3400000035762786865234375;
    float yaw = left1_4321_43right_f32_f32(tmp34, tmp35);
    float yaw_sine = _the43_sine_of_value_f32(yaw);
    float yaw_cosine = _the43_cosine_of_value_f32(yaw);
    float tmp36 = left1_4321_43right_f32_f32(point_x, yaw_cosine);
    float tmp37 = left1_4321_43right_f32_f32(point_z, yaw_sine);
    float turned_x = left1_4331_43right_f32_f32(tmp36, tmp37);
    float tmp38 = left1_4321_43right_f32_f32(point_z, yaw_cosine);
    float tmp39 = left1_4321_43right_f32_f32(point_x, yaw_sine);
    float turned_z = left1_4351_43right_f32_f32(tmp38, tmp39);
    float tmp40 = 3.25;
    float tmp41 = 0.519999980926513671875;
    float tmp42 = left1_4321_43right_f32_f32(turned_z, tmp41);
    float depth = left1_4331_43right_f32_f32(tmp40, tmp42);
    float tmp43 = 1.03999996185302734375;
    float tmp44 = 0.017999999225139617919921875;
    float tmp45 = left1_4321_43right_f32_f32(point_group, tmp44);
    float tmp46 = left1_4331_43right_f32_f32(tmp43, tmp45);
    float tmp47 = left1_4371_43right_f32_f32(tmp46, width);
    float tmp48 = 0.959999978542327880859375;
    float tmp49 = 0.039999999105930328369140625;
    float tmp50 = left1_4321_43right_f32_f32(render_pass, tmp49);
    float tmp51 = left1_4331_43right_f32_f32(tmp48, tmp50);
    float point_size = left1_4321_43right_f32_f32(tmp47, tmp51);
    float tmp52 = 1.65999996662139892578125;
    float tmp53 = 1.0;
    float tmp54 = _the43_maximum_of_a_and_b_f32_f32(aspect, tmp53);
    float horizontal_scale = left1_4371_43right_f32_f32(tmp52, tmp54);
    float tmp55 = 1.0;
    float tmp56 = 1.519999980926513671875;
    float tmp57 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp55, tmp56, aspect);
    float tmp58 = 0.4000000059604644775390625;
    float figure_offset_x = left1_4321_43right_f32_f32(tmp57, tmp58);
    float tmp59 = 0.0;
    float corner_x = left1_4351_43right_f32_f32(tmp59, point_size);
    float tmp60 = 0.0;
    float corner_y = left1_4351_43right_f32_f32(tmp60, point_size);
    float tmp61 = 1.0;
    if (left_1is_greater_than_or_equal_to4213_right_f32_f32(triangle_corner, tmp61))
    {
        corner_x = point_size;
    }
    float tmp64 = 2.0;
    if (left_1is_greater_than_or_equal_to4213_right_f32_f32(triangle_corner, tmp64))
    {
        corner_x = 0.0;
        float tmp65 = 1.4500000476837158203125;
        corner_y = left1_4321_43right_f32_f32(point_size, tmp65);
    }
    float tmp66 = left1_4321_43right_f32_f32(turned_x, horizontal_scale);
    float tmp67 = left1_4321_43right_f32_f32(figure_offset_x, depth);
    float tmp68 = left1_4331_43right_f32_f32(tmp66, tmp67);
    float tmp69 = left1_4321_43right_f32_f32(corner_x, depth);
    float tmp70 = 1.86000001430511474609375;
    float tmp71 = left1_4321_43right_f32_f32(point_y, tmp70);
    float tmp72 = 0.039999999105930328369140625;
    float tmp73 = left1_4351_43right_f32_f32(tmp71, tmp72);
    float tmp74 = left1_4321_43right_f32_f32(corner_y, depth);
    float tmp77 = 0.001000000047497451305389404296875;
    float _243 = 0.0;
    if (left_0_right_f32_f32(visibility, tmp77))
    {
        float tmp78 = 3.0;
        _243 = left1_4321_43right_f32_f32(depth, tmp78);
    }
    else
    {
        _243 = left1_4331_43right_f32_f32(tmp68, tmp69);
    }
    float tmp79 = 0.4199999868869781494140625;
    vec4 _246 = vec4(0.0);
    _246.w = depth;
    _246.z = left1_4321_43right_f32_f32(depth, tmp79);
    _246.y = left1_4331_43right_f32_f32(tmp73, tmp74);
    _246.x = _243;
    gl_Position = _246;
}
