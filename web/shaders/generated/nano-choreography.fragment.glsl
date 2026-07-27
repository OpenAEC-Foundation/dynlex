#version 300 es
precision highp float;
precision highp int;

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

layout(location = 0) out vec4 dynlexColor;

float _the43_maximum_of_a_and_b_f32_f32(float a, float b)
{
    return isnan(b) ? a : (isnan(a) ? b : max(a, b));
}

float left1_4371_43right_f32_f32(float left, float right)
{
    return left / right;
}

float left1_4321_43right_f32_f32(float left, float right)
{
    return left * right;
}

float left1_4351_43right_f32_f32(float left, float right)
{
    return left - right;
}

float left1_4331_43right_f32_f32(float left, float right)
{
    return left + right;
}

float _the43_square_root_of_value_f32(float value)
{
    return sqrt(value);
}

float _the43_minimum_of_a_and_b_f32_f32(float a, float b)
{
    return isnan(b) ? a : (isnan(a) ? b : min(a, b));
}

bool left_2_right_f32_f32(float left, float right)
{
    return left > right;
}

bool left_0_right_f32_f32(float left, float right)
{
    return left < right;
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

float _the43_sine_of_value_f32(float value)
{
    return sin(value);
}

float _the43_cosine_of_value_f32(float value)
{
    return cos(value);
}

float _the_431negative_1of_34opposite_1of_3453value_f32(float value)
{
    return -value;
}

float signed_flow_at_x_y_phase_f32_f32_f32(float x, float y, float phase)
{
    float tmp = 0.730000019073486328125;
    float tmp1 = left1_4321_43right_f32_f32(x, tmp);
    float tmp2 = 0.4099999964237213134765625;
    float tmp3 = left1_4321_43right_f32_f32(y, tmp2);
    float tmp4 = left1_4351_43right_f32_f32(tmp1, tmp3);
    float tmp5 = left1_4331_43right_f32_f32(tmp4, phase);
    float warp_x = _the43_sine_of_value_f32(tmp5);
    float tmp6 = 0.37000000476837158203125;
    float tmp7 = left1_4321_43right_f32_f32(x, tmp6);
    float tmp8 = 0.88999998569488525390625;
    float tmp9 = left1_4321_43right_f32_f32(y, tmp8);
    float tmp10 = left1_4331_43right_f32_f32(tmp7, tmp9);
    float tmp11 = 0.709999978542327880859375;
    float tmp12 = left1_4321_43right_f32_f32(phase, tmp11);
    float tmp13 = left1_4351_43right_f32_f32(tmp10, tmp12);
    float warp_y = _the43_cosine_of_value_f32(tmp13);
    float tmp14 = 0.579999983310699462890625;
    float tmp15 = left1_4321_43right_f32_f32(warp_x, tmp14);
    float bent_x = left1_4331_43right_f32_f32(x, tmp15);
    float tmp16 = 0.579999983310699462890625;
    float tmp17 = left1_4321_43right_f32_f32(warp_y, tmp16);
    float bent_y = left1_4331_43right_f32_f32(y, tmp17);
    float tmp18 = 1.309999942779541015625;
    float tmp19 = left1_4321_43right_f32_f32(bent_x, tmp18);
    float tmp20 = 0.87000000476837158203125;
    float tmp21 = left1_4321_43right_f32_f32(bent_y, tmp20);
    float tmp22 = left1_4331_43right_f32_f32(tmp19, tmp21);
    float tmp23 = 0.430000007152557373046875;
    float tmp24 = left1_4321_43right_f32_f32(phase, tmp23);
    float tmp25 = left1_4331_43right_f32_f32(tmp22, tmp24);
    float broad = _the43_sine_of_value_f32(tmp25);
    float tmp26 = 0.790000021457672119140625;
    float tmp27 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp26);
    float tmp28 = left1_4321_43right_f32_f32(bent_x, tmp27);
    float tmp29 = 1.730000019073486328125;
    float tmp30 = left1_4321_43right_f32_f32(bent_y, tmp29);
    float tmp31 = left1_4331_43right_f32_f32(tmp28, tmp30);
    float tmp32 = 0.310000002384185791015625;
    float tmp33 = left1_4321_43right_f32_f32(phase, tmp32);
    float tmp34 = left1_4351_43right_f32_f32(tmp31, tmp33);
    float crossing = _the43_cosine_of_value_f32(tmp34);
    float tmp35 = 2.4700000286102294921875;
    float tmp36 = left1_4321_43right_f32_f32(bent_x, tmp35);
    float tmp37 = 2.1099998950958251953125;
    float tmp38 = left1_4321_43right_f32_f32(bent_y, tmp37);
    float tmp39 = left1_4351_43right_f32_f32(tmp36, tmp38);
    float tmp40 = 1.7999999523162841796875;
    float tmp41 = left1_4321_43right_f32_f32(broad, tmp40);
    float tmp42 = left1_4331_43right_f32_f32(tmp39, tmp41);
    float curl = _the43_sine_of_value_f32(tmp42);
    float tmp43 = 4.030000209808349609375;
    float tmp44 = left1_4321_43right_f32_f32(bent_x, tmp43);
    float tmp45 = 3.1700000762939453125;
    float tmp46 = left1_4321_43right_f32_f32(bent_y, tmp45);
    float tmp47 = left1_4331_43right_f32_f32(tmp44, tmp46);
    float tmp48 = 1.39999997615814208984375;
    float tmp49 = left1_4321_43right_f32_f32(crossing, tmp48);
    float tmp50 = left1_4331_43right_f32_f32(tmp47, tmp49);
    float detail = _the43_cosine_of_value_f32(tmp50);
    float tmp51 = 0.4600000083446502685546875;
    float tmp52 = left1_4321_43right_f32_f32(broad, tmp51);
    float tmp53 = 0.2899999916553497314453125;
    float tmp54 = left1_4321_43right_f32_f32(crossing, tmp53);
    float tmp55 = left1_4331_43right_f32_f32(tmp52, tmp54);
    float tmp56 = 0.17000000178813934326171875;
    float tmp57 = left1_4321_43right_f32_f32(curl, tmp56);
    float tmp58 = 0.07999999821186065673828125;
    float tmp59 = left1_4321_43right_f32_f32(detail, tmp58);
    float tmp60 = left1_4331_43right_f32_f32(tmp57, tmp59);
    return left1_4331_43right_f32_f32(tmp55, tmp60);
}

float flowing_field_at_x_y_phase_f32_f32_f32(float x, float y, float phase)
{
    float tmp = signed_flow_at_x_y_phase_f32_f32_f32(x, y, phase);
    float tmp1 = 0.5;
    float tmp2 = left1_4321_43right_f32_f32(tmp, tmp1);
    float tmp3 = 0.5;
    return left1_4331_43right_f32_f32(tmp2, tmp3);
}

float _the43_absolute_value_of_magnitude_f32(float magnitude)
{
    return abs(magnitude);
}

float ridged_field_at_x_y_phase_f32_f32_f32(float x, float y, float phase)
{
    float tmp = signed_flow_at_x_y_phase_f32_f32_f32(x, y, phase);
    float wave = _the43_absolute_value_of_magnitude_f32(tmp);
    float tmp1 = 1.0;
    float tmp2 = 1.0;
    float tmp3 = _the43_minimum_of_a_and_b_f32_f32(wave, tmp2);
    float ridge = left1_4351_43right_f32_f32(tmp1, tmp3);
    return left1_4321_43right_f32_f32(ridge, ridge);
}

float glow_from_inner_to_outer_at_sample_f32_f32_f32(float inner, float outer, float _sample)
{
    float tmp = 1.0;
    float tmp1 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(inner, outer, _sample);
    return left1_4351_43right_f32_f32(tmp, tmp1);
}

float spark_field_at_x_y_phase_f32_f32_f32(float x, float y, float phase)
{
    float tmp = 0.189999997615814208984375;
    float tmp1 = left1_4321_43right_f32_f32(x, tmp);
    float tmp2 = 0.189999997615814208984375;
    float tmp3 = left1_4321_43right_f32_f32(y, tmp2);
    float warp = signed_flow_at_x_y_phase_f32_f32_f32(tmp1, tmp3, phase);
    float tmp4 = 1.7000000476837158203125;
    float tmp5 = left1_4321_43right_f32_f32(warp, tmp4);
    float bent_x = left1_4331_43right_f32_f32(x, tmp5);
    float tmp6 = 0.23000000417232513427734375;
    float tmp7 = left1_4321_43right_f32_f32(x, tmp6);
    float tmp8 = 7.0;
    float tmp9 = left1_4331_43right_f32_f32(tmp7, tmp8);
    float tmp10 = 0.23000000417232513427734375;
    float tmp11 = left1_4321_43right_f32_f32(y, tmp10);
    float tmp12 = 5.0;
    float tmp13 = left1_4351_43right_f32_f32(tmp11, tmp12);
    float tmp14 = 1.7000000476837158203125;
    float tmp15 = left1_4331_43right_f32_f32(phase, tmp14);
    float tmp16 = signed_flow_at_x_y_phase_f32_f32_f32(tmp9, tmp13, tmp15);
    float tmp17 = 1.7000000476837158203125;
    float tmp18 = left1_4321_43right_f32_f32(tmp16, tmp17);
    float bent_y = left1_4331_43right_f32_f32(y, tmp18);
    float tmp19 = 1.730000019073486328125;
    float tmp20 = left1_4321_43right_f32_f32(bent_x, tmp19);
    float tmp21 = 0.310000002384185791015625;
    float tmp22 = left1_4321_43right_f32_f32(bent_y, tmp21);
    float tmp23 = left1_4331_43right_f32_f32(tmp22, phase);
    float tmp24 = left1_4331_43right_f32_f32(tmp20, tmp23);
    float tmp25 = _the43_sine_of_value_f32(tmp24);
    float wave_a = _the43_absolute_value_of_magnitude_f32(tmp25);
    float tmp26 = 0.2700000107288360595703125;
    float tmp27 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp26);
    float tmp28 = left1_4321_43right_f32_f32(bent_x, tmp27);
    float tmp29 = 1.90999996662139892578125;
    float tmp30 = left1_4321_43right_f32_f32(bent_y, tmp29);
    float tmp31 = 0.829999983310699462890625;
    float tmp32 = left1_4321_43right_f32_f32(phase, tmp31);
    float tmp33 = left1_4351_43right_f32_f32(tmp30, tmp32);
    float tmp34 = left1_4331_43right_f32_f32(tmp28, tmp33);
    float tmp35 = _the43_sine_of_value_f32(tmp34);
    float wave_b = _the43_absolute_value_of_magnitude_f32(tmp35);
    float tmp36 = left1_4321_43right_f32_f32(wave_a, wave_a);
    float tmp37 = left1_4321_43right_f32_f32(wave_b, wave_b);
    float tmp38 = left1_4331_43right_f32_f32(tmp36, tmp37);
    float crossing_distance = _the43_square_root_of_value_f32(tmp38);
    float tmp39 = 0.0;
    float tmp40 = 0.115000002086162567138671875;
    float point = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp39, tmp40, crossing_distance);
    float tmp41 = 0.10999999940395355224609375;
    float tmp42 = left1_4321_43right_f32_f32(bent_x, tmp41);
    float tmp43 = 0.10999999940395355224609375;
    float tmp44 = left1_4321_43right_f32_f32(bent_y, tmp43);
    float tmp45 = 4.0;
    float tmp46 = left1_4331_43right_f32_f32(phase, tmp45);
    float rarity = flowing_field_at_x_y_phase_f32_f32_f32(tmp42, tmp44, tmp46);
    float tmp47 = 0.4799999892711639404296875;
    float tmp48 = 0.819999992847442626953125;
    float tmp49 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp47, tmp48, rarity);
    return left1_4321_43right_f32_f32(point, tmp49);
}

float distance_from_point_x_y_to_segment_ax_ay_bx_by_f32_f32_f32_f32_f32_f32(float x, float y, float ax, float ay, float bx, float by)
{
    float segment_x = left1_4351_43right_f32_f32(bx, ax);
    float segment_y = left1_4351_43right_f32_f32(by, ay);
    float point_x = left1_4351_43right_f32_f32(x, ax);
    float point_y = left1_4351_43right_f32_f32(y, ay);
    float tmp = left1_4321_43right_f32_f32(segment_x, segment_x);
    float tmp1 = left1_4321_43right_f32_f32(segment_y, segment_y);
    float length_squared = left1_4331_43right_f32_f32(tmp, tmp1);
    float tmp2 = left1_4321_43right_f32_f32(point_x, segment_x);
    float tmp3 = left1_4321_43right_f32_f32(point_y, segment_y);
    float tmp4 = left1_4331_43right_f32_f32(tmp2, tmp3);
    float tmp5 = 9.9999999747524270787835121154785e-07;
    float tmp6 = _the43_maximum_of_a_and_b_f32_f32(length_squared, tmp5);
    float projection = left1_4371_43right_f32_f32(tmp4, tmp6);
    projection = saturate_number_f32(projection);
    float tmp7 = left1_4321_43right_f32_f32(segment_x, projection);
    float nearest_x = left1_4331_43right_f32_f32(ax, tmp7);
    float tmp8 = left1_4321_43right_f32_f32(segment_y, projection);
    float nearest_y = left1_4331_43right_f32_f32(ay, tmp8);
    float delta_x = left1_4351_43right_f32_f32(x, nearest_x);
    float delta_y = left1_4351_43right_f32_f32(y, nearest_y);
    float tmp9 = left1_4321_43right_f32_f32(delta_x, delta_x);
    float tmp10 = left1_4321_43right_f32_f32(delta_y, delta_y);
    float tmp11 = left1_4331_43right_f32_f32(tmp9, tmp10);
    return _the43_square_root_of_value_f32(tmp11);
}

void main()
{
    float pixel_x = gl_FragCoord.x;
    float pixel_y = gl_FragCoord.y;
    float time = dynlexUniform0.value;
    float tmp = dynlexUniform2.value;
    float tmp3 = 1.0;
    float width = _the43_maximum_of_a_and_b_f32_f32(tmp, tmp3);
    float tmp4 = dynlexUniform3.value;
    float tmp5 = 1.0;
    float height = _the43_maximum_of_a_and_b_f32_f32(tmp4, tmp5);
    float render_pass = dynlexUniform1.value;
    float aspect = left1_4371_43right_f32_f32(width, height);
    float tmp6 = left1_4371_43right_f32_f32(pixel_x, width);
    float tmp7 = 2.0;
    float tmp8 = left1_4321_43right_f32_f32(tmp6, tmp7);
    float tmp9 = 1.0;
    float tmp10 = left1_4351_43right_f32_f32(tmp8, tmp9);
    float screen_x = left1_4321_43right_f32_f32(tmp10, aspect);
    float tmp11 = left1_4371_43right_f32_f32(pixel_y, height);
    float tmp12 = 2.0;
    float tmp13 = left1_4321_43right_f32_f32(tmp11, tmp12);
    float tmp14 = 1.0;
    float screen_y = left1_4351_43right_f32_f32(tmp13, tmp14);
    float tmp15 = left1_4321_43right_f32_f32(screen_x, screen_x);
    float tmp16 = left1_4321_43right_f32_f32(screen_y, screen_y);
    float tmp17 = left1_4331_43right_f32_f32(tmp15, tmp16);
    float radial = _the43_square_root_of_value_f32(tmp17);
    float tmp18 = 10.3999996185302734375;
    float moment = _the43_minimum_of_a_and_b_f32_f32(time, tmp18);
    float tmp19 = 0.5;
    if (left_2_right_f32_f32(render_pass, tmp19))
    {
        float tmp20 = 19.0;
        float tmp21 = left1_4321_43right_f32_f32(screen_x, tmp20);
        float tmp22 = 13.0;
        float tmp23 = left1_4321_43right_f32_f32(screen_y, tmp22);
        float tmp24 = left1_4351_43right_f32_f32(tmp21, tmp23);
        float tmp25 = 4.19999980926513671875;
        float tmp26 = left1_4321_43right_f32_f32(time, tmp25);
        float tmp27 = left1_4331_43right_f32_f32(tmp24, tmp26);
        float tmp28 = _the43_sine_of_value_f32(tmp27);
        float tmp29 = 0.5;
        float tmp30 = left1_4321_43right_f32_f32(tmp28, tmp29);
        float tmp31 = 0.5;
        float point_wave = left1_4331_43right_f32_f32(tmp30, tmp31);
        float tmp32 = 47.0;
        float tmp33 = left1_4321_43right_f32_f32(screen_y, tmp32);
        float tmp34 = 6.80000019073486328125;
        float tmp35 = left1_4321_43right_f32_f32(time, tmp34);
        float tmp36 = left1_4351_43right_f32_f32(tmp33, tmp35);
        float tmp37 = _the43_sine_of_value_f32(tmp36);
        float tmp38 = 0.5;
        float tmp39 = left1_4321_43right_f32_f32(tmp37, tmp38);
        float tmp40 = 0.5;
        float scan_wave = left1_4331_43right_f32_f32(tmp39, tmp40);
        float tmp41 = 1.0;
        float tmp42 = 4.44999980926513671875;
        float tmp43 = 7.400000095367431640625;
        float tmp44 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp42, tmp43, moment);
        float motorcycle_color = left1_4351_43right_f32_f32(tmp41, tmp44);
        float tmp45 = 0.07999999821186065673828125;
        float tmp46 = 0.2800000011920928955078125;
        float tmp47 = left1_4321_43right_f32_f32(point_wave, tmp46);
        float tmp48 = left1_4331_43right_f32_f32(tmp45, tmp47);
        float tmp49 = 0.37999999523162841796875;
        float tmp50 = left1_4321_43right_f32_f32(motorcycle_color, tmp49);
        float tmp51 = 0.300000011920928955078125;
        float tmp52 = 0.23999999463558197021484375;
        float tmp53 = left1_4321_43right_f32_f32(scan_wave, tmp52);
        float tmp54 = left1_4331_43right_f32_f32(tmp51, tmp53);
        float tmp55 = 0.119999997317790985107421875;
        float tmp56 = left1_4321_43right_f32_f32(motorcycle_color, tmp55);
        float tmp57 = 0.660000026226043701171875;
        float tmp58 = 0.300000011920928955078125;
        float tmp59 = left1_4321_43right_f32_f32(point_wave, tmp58);
        float tmp60 = left1_4331_43right_f32_f32(tmp57, tmp59);
        float tmp61 = 0.180000007152557373046875;
        float tmp62 = left1_4321_43right_f32_f32(motorcycle_color, tmp61);
        vec4 _691 = vec4(0.0, 0.0, 0.0, 1.0);
        _691.z = left1_4351_43right_f32_f32(tmp60, tmp62);
        _691.y = left1_4331_43right_f32_f32(tmp54, tmp56);
        _691.x = left1_4331_43right_f32_f32(tmp48, tmp50);
        dynlexColor = _691;
    }
    else
    {
        float tmp63 = 0.0;
        float tmp64 = 7.400000095367431640625;
        float speed_visibility = scene_window_from_opening_to_closing_at_moment_f32_f32_f32(tmp63, tmp64, moment);
        float tmp65 = 6.849999904632568359375;
        float tmp66 = 11.0;
        float vitruvian_visibility = scene_window_from_opening_to_closing_at_moment_f32_f32_f32(tmp65, tmp66, moment);
        float tmp67 = 2.2000000476837158203125;
        float tmp68 = left1_4321_43right_f32_f32(screen_x, tmp67);
        float tmp69 = 0.02099999971687793731689453125;
        float tmp70 = left1_4321_43right_f32_f32(time, tmp69);
        float tmp71 = left1_4331_43right_f32_f32(tmp68, tmp70);
        float tmp72 = 2.2000000476837158203125;
        float tmp73 = left1_4321_43right_f32_f32(screen_y, tmp72);
        float tmp74 = 0.01400000043213367462158203125;
        float tmp75 = left1_4321_43right_f32_f32(time, tmp74);
        float tmp76 = left1_4351_43right_f32_f32(tmp73, tmp75);
        float tmp77 = 1.89999997615814208984375;
        float chamber_one = flowing_field_at_x_y_phase_f32_f32_f32(tmp71, tmp76, tmp77);
        float tmp78 = 4.099999904632568359375;
        float tmp79 = left1_4321_43right_f32_f32(screen_x, tmp78);
        float tmp80 = 0.0170000009238719940185546875;
        float tmp81 = left1_4321_43right_f32_f32(time, tmp80);
        float tmp82 = left1_4351_43right_f32_f32(tmp79, tmp81);
        float tmp83 = 4.099999904632568359375;
        float tmp84 = left1_4321_43right_f32_f32(screen_y, tmp83);
        float tmp85 = 0.010999999940395355224609375;
        float tmp86 = left1_4321_43right_f32_f32(time, tmp85);
        float tmp87 = left1_4331_43right_f32_f32(tmp84, tmp86);
        float tmp88 = 5.400000095367431640625;
        float chamber_two = ridged_field_at_x_y_phase_f32_f32_f32(tmp82, tmp87, tmp88);
        float tmp89 = 27.0;
        float tmp90 = left1_4321_43right_f32_f32(screen_x, tmp89);
        float tmp91 = 0.23999999463558197021484375;
        float tmp92 = left1_4321_43right_f32_f32(time, tmp91);
        float tmp93 = left1_4331_43right_f32_f32(tmp90, tmp92);
        float tmp94 = 27.0;
        float tmp95 = left1_4321_43right_f32_f32(screen_y, tmp94);
        float tmp96 = 0.12999999523162841796875;
        float tmp97 = left1_4321_43right_f32_f32(time, tmp96);
        float tmp98 = left1_4351_43right_f32_f32(tmp95, tmp97);
        float tmp99 = 9.69999980926513671875;
        float free_drones = spark_field_at_x_y_phase_f32_f32_f32(tmp93, tmp98, tmp99);
        float tmp100 = 7.0;
        float tmp101 = left1_4321_43right_f32_f32(screen_x, tmp100);
        float tmp102 = 8.3999996185302734375;
        float tmp103 = left1_4321_43right_f32_f32(time, tmp102);
        float tmp104 = left1_4331_43right_f32_f32(tmp101, tmp103);
        float tmp105 = 46.0;
        float tmp106 = left1_4321_43right_f32_f32(screen_y, tmp105);
        float tmp107 = 14.19999980926513671875;
        float speed_particles = spark_field_at_x_y_phase_f32_f32_f32(tmp104, tmp106, tmp107);
        float tmp108 = 0.0;
        float tmp109 = 0.660000026226043701171875;
        float tmp110 = _the43_absolute_value_of_magnitude_f32(screen_y);
        float speed_core = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp108, tmp109, tmp110);
        float tmp111 = left1_4321_43right_f32_f32(speed_particles, speed_core);
        float speed_trails = left1_4321_43right_f32_f32(tmp111, speed_visibility);
        float tmp112 = 0.0030000000260770320892333984375;
        float tmp113 = 0.01200000010430812835693359375;
        float tmp114 = left1_4321_43right_f32_f32(chamber_one, tmp113);
        float tmp115 = left1_4331_43right_f32_f32(tmp112, tmp114);
        float tmp116 = 0.017999999225139617919921875;
        float tmp117 = left1_4321_43right_f32_f32(chamber_two, tmp116);
        float tmp118 = left1_4331_43right_f32_f32(tmp115, tmp117);
        float tmp119 = 0.180000007152557373046875;
        float tmp120 = left1_4321_43right_f32_f32(free_drones, tmp119);
        float tmp121 = left1_4331_43right_f32_f32(tmp118, tmp120);
        float tmp122 = 0.319999992847442626953125;
        float tmp123 = left1_4321_43right_f32_f32(speed_trails, tmp122);
        float red = left1_4331_43right_f32_f32(tmp121, tmp123);
        float tmp124 = 0.006000000052154064178466796875;
        float tmp125 = 0.0240000002086162567138671875;
        float tmp126 = left1_4321_43right_f32_f32(chamber_one, tmp125);
        float tmp127 = left1_4331_43right_f32_f32(tmp124, tmp126);
        float tmp128 = 0.014999999664723873138427734375;
        float tmp129 = left1_4321_43right_f32_f32(chamber_two, tmp128);
        float tmp130 = left1_4331_43right_f32_f32(tmp127, tmp129);
        float tmp131 = 0.4600000083446502685546875;
        float tmp132 = left1_4321_43right_f32_f32(free_drones, tmp131);
        float tmp133 = left1_4331_43right_f32_f32(tmp130, tmp132);
        float tmp134 = 0.7799999713897705078125;
        float tmp135 = left1_4321_43right_f32_f32(speed_trails, tmp134);
        float green = left1_4331_43right_f32_f32(tmp133, tmp135);
        float tmp136 = 0.0240000002086162567138671875;
        float tmp137 = 0.08200000226497650146484375;
        float tmp138 = left1_4321_43right_f32_f32(chamber_one, tmp137);
        float tmp139 = left1_4331_43right_f32_f32(tmp136, tmp138);
        float tmp140 = 0.071000002324581146240234375;
        float tmp141 = left1_4321_43right_f32_f32(chamber_two, tmp140);
        float tmp142 = left1_4331_43right_f32_f32(tmp139, tmp141);
        float tmp143 = 1.019999980926513671875;
        float tmp144 = left1_4321_43right_f32_f32(free_drones, tmp143);
        float tmp145 = left1_4331_43right_f32_f32(tmp142, tmp144);
        float tmp146 = 1.46000003814697265625;
        float tmp147 = left1_4321_43right_f32_f32(speed_trails, tmp146);
        float blue = left1_4331_43right_f32_f32(tmp145, tmp147);
        float tmp148 = 0.2800000011920928955078125;
        float tmp149 = 1.2599999904632568359375;
        float figure_aura = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp148, tmp149, radial);
        float tmp150 = left1_4321_43right_f32_f32(figure_aura, vitruvian_visibility);
        float tmp151 = 0.0320000015199184417724609375;
        float tmp152 = left1_4321_43right_f32_f32(tmp150, tmp151);
        red = left1_4331_43right_f32_f32(red, tmp152);
        float tmp153 = left1_4321_43right_f32_f32(figure_aura, vitruvian_visibility);
        float tmp154 = 0.0949999988079071044921875;
        float tmp155 = left1_4321_43right_f32_f32(tmp153, tmp154);
        green = left1_4331_43right_f32_f32(green, tmp155);
        float tmp156 = left1_4321_43right_f32_f32(figure_aura, vitruvian_visibility);
        float tmp157 = 0.20999999344348907470703125;
        float tmp158 = left1_4321_43right_f32_f32(tmp156, tmp157);
        blue = left1_4331_43right_f32_f32(blue, tmp158);
        float tmp159 = 7.55000019073486328125;
        float tmp160 = 8.1000003814697265625;
        float measurement_visibility = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp159, tmp160, moment);
        float tmp161 = 7.55000019073486328125;
        float tmp162 = left1_4351_43right_f32_f32(time, tmp161);
        float tmp163 = 1.32000005245208740234375;
        float measurement_angle = left1_4321_43right_f32_f32(tmp162, tmp163);
        float measurement_radius = 1.08000004291534423828125;
        float tmp164 = _the43_cosine_of_value_f32(measurement_angle);
        float circle_source_x = left1_4321_43right_f32_f32(tmp164, measurement_radius);
        float tmp165 = _the43_sine_of_value_f32(measurement_angle);
        float circle_source_y = left1_4321_43right_f32_f32(tmp165, measurement_radius);
        float tmp166 = 0.310000002384185791015625;
        float tmp167 = left1_4321_43right_f32_f32(time, tmp166);
        float tmp168 = _the43_sine_of_value_f32(tmp167);
        float tmp169 = 0.3400000035762786865234375;
        float measurement_yaw = left1_4321_43right_f32_f32(tmp168, tmp169);
        float tmp170 = _the43_cosine_of_value_f32(measurement_yaw);
        float circle_turned_x = left1_4321_43right_f32_f32(circle_source_x, tmp170);
        float tmp171 = 0.0;
        float tmp172 = _the43_sine_of_value_f32(measurement_yaw);
        float tmp173 = left1_4321_43right_f32_f32(circle_source_x, tmp172);
        float circle_turned_z = left1_4351_43right_f32_f32(tmp171, tmp173);
        float tmp174 = 3.25;
        float tmp175 = 0.519999980926513671875;
        float tmp176 = left1_4321_43right_f32_f32(circle_turned_z, tmp175);
        float circle_depth = left1_4331_43right_f32_f32(tmp174, tmp176);
        float tmp177 = 0.0;
        float tmp178 = 0.039999999105930328369140625;
        float tmp179 = left1_4351_43right_f32_f32(tmp177, tmp178);
        float tmp180 = 3.25;
        float radius_center_y = left1_4371_43right_f32_f32(tmp179, tmp180);
        float tmp181 = 1.65999996662139892578125;
        float tmp182 = left1_4321_43right_f32_f32(circle_turned_x, tmp181);
        float radius_end_x = left1_4371_43right_f32_f32(tmp182, circle_depth);
        float tmp183 = 1.86000001430511474609375;
        float tmp184 = left1_4321_43right_f32_f32(circle_source_y, tmp183);
        float tmp185 = 0.039999999105930328369140625;
        float tmp186 = left1_4351_43right_f32_f32(tmp184, tmp185);
        float radius_end_y = left1_4371_43right_f32_f32(tmp186, circle_depth);
        float tmp187 = 0.0;
        float radius_distance = distance_from_point_x_y_to_segment_ax_ay_bx_by_f32_f32_f32_f32_f32_f32(screen_x, screen_y, tmp187, radius_center_y, radius_end_x, radius_end_y);
        float tmp188 = 0.00200000009499490261077880859375;
        float tmp189 = 0.0089999996125698089599609375;
        float radius_line = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp188, tmp189, radius_distance);
        float radius_vector_x = radius_end_x;
        float radius_vector_y = left1_4351_43right_f32_f32(radius_end_y, radius_center_y);
        float tmp190 = left1_4321_43right_f32_f32(radius_vector_x, radius_vector_x);
        float tmp191 = left1_4321_43right_f32_f32(radius_vector_y, radius_vector_y);
        float tmp192 = left1_4331_43right_f32_f32(tmp190, tmp191);
        float tmp193 = _the43_square_root_of_value_f32(tmp192);
        float tmp194 = 9.9999997473787516355514526367188e-05;
        float radius_length = _the43_maximum_of_a_and_b_f32_f32(tmp193, tmp194);
        float tmp195 = 0.0;
        float tmp196 = left1_4351_43right_f32_f32(tmp195, radius_vector_y);
        float tangent_x = left1_4371_43right_f32_f32(tmp196, radius_length);
        float tangent_y = left1_4371_43right_f32_f32(radius_vector_x, radius_length);
        float tmp197 = 0.026000000536441802978515625;
        float tmp198 = left1_4321_43right_f32_f32(tangent_x, tmp197);
        float tick_start_x = left1_4351_43right_f32_f32(radius_end_x, tmp198);
        float tmp199 = 0.026000000536441802978515625;
        float tmp200 = left1_4321_43right_f32_f32(tangent_y, tmp199);
        float tick_start_y = left1_4351_43right_f32_f32(radius_end_y, tmp200);
        float tmp201 = 0.026000000536441802978515625;
        float tmp202 = left1_4321_43right_f32_f32(tangent_x, tmp201);
        float tick_end_x = left1_4331_43right_f32_f32(radius_end_x, tmp202);
        float tmp203 = 0.026000000536441802978515625;
        float tmp204 = left1_4321_43right_f32_f32(tangent_y, tmp203);
        float tick_end_y = left1_4331_43right_f32_f32(radius_end_y, tmp204);
        float radius_tick_distance = distance_from_point_x_y_to_segment_ax_ay_bx_by_f32_f32_f32_f32_f32_f32(screen_x, screen_y, tick_start_x, tick_start_y, tick_end_x, tick_end_y);
        float tmp205 = 0.00200000009499490261077880859375;
        float tmp206 = 0.010999999940395355224609375;
        float radius_tick = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp205, tmp206, radius_tick_distance);
        float highlight_x = left1_4351_43right_f32_f32(screen_x, radius_end_x);
        float highlight_y = left1_4351_43right_f32_f32(screen_y, radius_end_y);
        float tmp207 = left1_4321_43right_f32_f32(highlight_x, highlight_x);
        float tmp208 = left1_4321_43right_f32_f32(highlight_y, highlight_y);
        float tmp209 = left1_4331_43right_f32_f32(tmp207, tmp208);
        float highlight_distance = _the43_square_root_of_value_f32(tmp209);
        float tmp210 = 0.0;
        float tmp211 = 0.04500000178813934326171875;
        float circle_highlight = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp210, tmp211, highlight_distance);
        float tmp212 = left1_4321_43right_f32_f32(screen_x, screen_x);
        float tmp213 = left1_4351_43right_f32_f32(screen_y, radius_center_y);
        float tmp214 = left1_4351_43right_f32_f32(screen_y, radius_center_y);
        float tmp215 = left1_4321_43right_f32_f32(tmp213, tmp214);
        float tmp216 = left1_4331_43right_f32_f32(tmp212, tmp215);
        float center_distance = _the43_square_root_of_value_f32(tmp216);
        float tmp217 = 0.0;
        float tmp218 = 0.02500000037252902984619140625;
        float center_highlight = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp217, tmp218, center_distance);
        float tmp219 = 0.62000000476837158203125;
        float tmp220 = left1_4321_43right_f32_f32(radius_line, tmp219);
        float tmp221 = 0.819999992847442626953125;
        float tmp222 = left1_4321_43right_f32_f32(radius_tick, tmp221);
        float tmp223 = left1_4331_43right_f32_f32(tmp220, tmp222);
        float tmp224 = 0.4799999892711639404296875;
        float tmp225 = left1_4321_43right_f32_f32(circle_highlight, tmp224);
        float tmp226 = left1_4331_43right_f32_f32(tmp223, tmp225);
        float tmp227 = 0.36000001430511474609375;
        float tmp228 = left1_4321_43right_f32_f32(center_highlight, tmp227);
        float measurement_light = left1_4331_43right_f32_f32(tmp226, tmp228);
        measurement_light = left1_4321_43right_f32_f32(measurement_light, measurement_visibility);
        float tmp229 = 0.4600000083446502685546875;
        float tmp230 = left1_4321_43right_f32_f32(measurement_light, tmp229);
        red = left1_4331_43right_f32_f32(red, tmp230);
        float tmp231 = 0.87999999523162841796875;
        float tmp232 = left1_4321_43right_f32_f32(measurement_light, tmp231);
        green = left1_4331_43right_f32_f32(green, tmp232);
        float tmp233 = 1.2400000095367431640625;
        float tmp234 = left1_4321_43right_f32_f32(measurement_light, tmp233);
        blue = left1_4331_43right_f32_f32(blue, tmp234);
        float tmp235 = 0.180000007152557373046875;
        float tmp236 = 1.0;
        float tmp237 = 0.519999980926513671875;
        float tmp238 = 1.62000000476837158203125;
        float tmp239 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp237, tmp238, radial);
        float tmp240 = left1_4351_43right_f32_f32(tmp236, tmp239);
        float tmp241 = 0.819999992847442626953125;
        float tmp242 = left1_4321_43right_f32_f32(tmp240, tmp241);
        float vignette = left1_4331_43right_f32_f32(tmp235, tmp242);
        float tmp243 = left1_4321_43right_f32_f32(red, vignette);
        float tmp244 = 1.0;
        float tmp245 = left1_4331_43right_f32_f32(tmp244, red);
        float tmp246 = left1_4371_43right_f32_f32(tmp243, tmp245);
        red = _the43_square_root_of_value_f32(tmp246);
        float tmp247 = left1_4321_43right_f32_f32(green, vignette);
        float tmp248 = 1.0;
        float tmp249 = left1_4331_43right_f32_f32(tmp248, green);
        float tmp250 = left1_4371_43right_f32_f32(tmp247, tmp249);
        green = _the43_square_root_of_value_f32(tmp250);
        float tmp251 = left1_4321_43right_f32_f32(blue, vignette);
        float tmp252 = 1.0;
        float tmp253 = left1_4331_43right_f32_f32(tmp252, blue);
        float tmp254 = left1_4371_43right_f32_f32(tmp251, tmp253);
        blue = _the43_square_root_of_value_f32(tmp254);
        vec4 _660 = vec4(0.0, 0.0, 0.0, 1.0);
        _660.z = blue;
        _660.y = green;
        _660.x = red;
        dynlexColor = _660;
    }
}
