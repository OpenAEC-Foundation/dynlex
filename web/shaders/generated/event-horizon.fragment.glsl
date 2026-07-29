#version 300 es
precision highp float;
precision highp int;

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

float _the43_sine_of_value_f32(float value)
{
    return sin(value);
}

float _the43_cosine_of_value_f32(float value)
{
    return cos(value);
}

float _the43_square_root_of_value_f32(float value)
{
    return sqrt(value);
}

float _the43_minimum_of_a_and_b_f32_f32(float a, float b)
{
    return isnan(b) ? a : (isnan(a) ? b : min(a, b));
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

float _the43_floor_of_value_f32(float value)
{
    return floor(value);
}

float fractional_part_of_number_f32(float number)
{
    float tmp = _the43_floor_of_value_f32(number);
    return left1_4351_43right_f32_f32(number, tmp);
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

float moving_star_field_at_x_y_with_scale_phase_time_f32_f32_f32_f32_f32(float x, float y, float scale, float phase, float time)
{
    float tmp = 0.12999999523162841796875;
    float tmp1 = left1_4321_43right_f32_f32(time, tmp);
    float tmp2 = left1_4331_43right_f32_f32(tmp1, phase);
    float depth = fractional_part_of_number_f32(tmp2);
    float tmp3 = 0.4199999868869781494140625;
    float tmp4 = left1_4321_43right_f32_f32(depth, depth);
    float tmp5 = 3.599999904632568359375;
    float tmp6 = left1_4321_43right_f32_f32(tmp4, tmp5);
    float expansion = left1_4331_43right_f32_f32(tmp3, tmp6);
    float tmp7 = left1_4371_43right_f32_f32(x, expansion);
    float star_x = left1_4321_43right_f32_f32(tmp7, scale);
    float tmp8 = left1_4371_43right_f32_f32(y, expansion);
    float star_y = left1_4321_43right_f32_f32(tmp8, scale);
    float tmp9 = 13.69999980926513671875;
    float tmp10 = left1_4321_43right_f32_f32(phase, tmp9);
    float points = spark_field_at_x_y_phase_f32_f32_f32(star_x, star_y, tmp10);
    float tmp11 = 0.3300000131130218505859375;
    float tmp12 = left1_4321_43right_f32_f32(star_x, tmp11);
    float tmp13 = 13.69999980926513671875;
    float tmp14 = left1_4321_43right_f32_f32(phase, tmp13);
    float tmp15 = 0.0900000035762786865234375;
    float tmp16 = left1_4331_43right_f32_f32(tmp14, tmp15);
    float streak_x = spark_field_at_x_y_phase_f32_f32_f32(tmp12, star_y, tmp16);
    float tmp17 = 0.3300000131130218505859375;
    float tmp18 = left1_4321_43right_f32_f32(star_y, tmp17);
    float tmp19 = 13.69999980926513671875;
    float tmp20 = left1_4321_43right_f32_f32(phase, tmp19);
    float tmp21 = 0.10999999940395355224609375;
    float tmp22 = left1_4351_43right_f32_f32(tmp20, tmp21);
    float streak_y = spark_field_at_x_y_phase_f32_f32_f32(star_x, tmp18, tmp22);
    float tmp23 = left1_4331_43right_f32_f32(streak_x, streak_y);
    float tmp24 = 0.0;
    float tmp25 = 1.35000002384185791015625;
    float tmp26 = left1_4321_43right_f32_f32(x, x);
    float tmp27 = left1_4321_43right_f32_f32(y, y);
    float tmp28 = left1_4331_43right_f32_f32(tmp26, tmp27);
    float tmp29 = _the43_square_root_of_value_f32(tmp28);
    float tmp30 = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp24, tmp25, tmp29);
    float streaks = left1_4321_43right_f32_f32(tmp23, tmp30);
    float tmp31 = 2.7000000476837158203125;
    float tmp32 = left1_4321_43right_f32_f32(time, tmp31);
    float tmp33 = 29.0;
    float tmp34 = left1_4321_43right_f32_f32(phase, tmp33);
    float tmp35 = left1_4331_43right_f32_f32(tmp32, tmp34);
    float tmp36 = _the43_sine_of_value_f32(tmp35);
    float tmp37 = 0.12999999523162841796875;
    float tmp38 = left1_4321_43right_f32_f32(tmp36, tmp37);
    float tmp39 = 0.87000000476837158203125;
    float twinkle = left1_4331_43right_f32_f32(tmp38, tmp39);
    float tmp40 = 2.7999999523162841796875;
    float tmp41 = left1_4321_43right_f32_f32(points, tmp40);
    float tmp42 = 0.17000000178813934326171875;
    float tmp43 = left1_4321_43right_f32_f32(streaks, tmp42);
    float tmp44 = left1_4331_43right_f32_f32(tmp41, tmp43);
    float tmp45 = 0.2800000011920928955078125;
    float tmp46 = 1.25;
    float tmp47 = left1_4321_43right_f32_f32(depth, tmp46);
    float tmp48 = left1_4331_43right_f32_f32(tmp45, tmp47);
    float tmp49 = left1_4321_43right_f32_f32(tmp44, tmp48);
    return left1_4321_43right_f32_f32(tmp49, twinkle);
}

void main()
{
    float pixel_x = gl_FragCoord.x;
    float pixel_y = gl_FragCoord.y;
    float time = dynlexUniform0.value;
    float tmp = dynlexUniform1.value;
    float tmp3 = 1.0;
    float width = _the43_maximum_of_a_and_b_f32_f32(tmp, tmp3);
    float tmp4 = dynlexUniform2.value;
    float tmp5 = 1.0;
    float height = _the43_maximum_of_a_and_b_f32_f32(tmp4, tmp5);
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
    float tmp15 = 0.62000000476837158203125;
    float tmp16 = 0.054999999701976776123046875;
    float tmp17 = left1_4321_43right_f32_f32(time, tmp16);
    float tmp18 = left1_4331_43right_f32_f32(tmp15, tmp17);
    float tmp19 = left1_4321_43right_f32_f32(time, time);
    float tmp20 = 0.00179999996908009052276611328125;
    float tmp21 = left1_4321_43right_f32_f32(tmp19, tmp20);
    float flight_scale = left1_4331_43right_f32_f32(tmp18, tmp21);
    float tmp22 = 0.2700000107288360595703125;
    float tmp23 = left1_4321_43right_f32_f32(time, tmp22);
    float tmp24 = _the43_sine_of_value_f32(tmp23);
    float tmp25 = 0.048000000417232513427734375;
    float drift_x = left1_4321_43right_f32_f32(tmp24, tmp25);
    float tmp26 = 0.20999999344348907470703125;
    float tmp27 = left1_4321_43right_f32_f32(time, tmp26);
    float tmp28 = _the43_cosine_of_value_f32(tmp27);
    float tmp29 = 0.026000000536441802978515625;
    float drift_y = left1_4321_43right_f32_f32(tmp28, tmp29);
    float tmp30 = left1_4331_43right_f32_f32(screen_x, drift_x);
    float x = left1_4371_43right_f32_f32(tmp30, flight_scale);
    float tmp31 = left1_4331_43right_f32_f32(screen_y, drift_y);
    float y = left1_4371_43right_f32_f32(tmp31, flight_scale);
    float tmp32 = left1_4321_43right_f32_f32(x, x);
    float tmp33 = left1_4321_43right_f32_f32(y, y);
    float tmp34 = left1_4331_43right_f32_f32(tmp32, tmp33);
    float radius = _the43_square_root_of_value_f32(tmp34);
    float tmp35 = 0.001000000047497451305389404296875;
    float safe_radius = _the43_maximum_of_a_and_b_f32_f32(radius, tmp35);
    float tmp36 = 0.189999997615814208984375;
    float tmp37 = left1_4321_43right_f32_f32(radius, radius);
    float tmp38 = 0.02099999971687793731689453125;
    float tmp39 = left1_4331_43right_f32_f32(tmp37, tmp38);
    float tmp40 = left1_4371_43right_f32_f32(tmp36, tmp39);
    float tmp41 = 0.0350000001490116119384765625;
    float tmp42 = left1_4321_43right_f32_f32(time, tmp41);
    float lens_angle = left1_4331_43right_f32_f32(tmp40, tmp42);
    float tmp43 = 4.599999904632568359375;
    lens_angle = _the43_minimum_of_a_and_b_f32_f32(lens_angle, tmp43);
    float lens_sine = _the43_sine_of_value_f32(lens_angle);
    float lens_cosine = _the43_cosine_of_value_f32(lens_angle);
    float tmp44 = 1.0;
    float tmp45 = 0.119999997317790985107421875;
    float tmp46 = left1_4321_43right_f32_f32(radius, radius);
    float tmp47 = 0.01200000010430812835693359375;
    float tmp48 = left1_4331_43right_f32_f32(tmp46, tmp47);
    float tmp49 = left1_4371_43right_f32_f32(tmp45, tmp48);
    float lens_scale = left1_4331_43right_f32_f32(tmp44, tmp49);
    float tmp50 = left1_4321_43right_f32_f32(x, lens_cosine);
    float tmp51 = left1_4321_43right_f32_f32(y, lens_sine);
    float tmp52 = left1_4351_43right_f32_f32(tmp50, tmp51);
    float universe_x = left1_4321_43right_f32_f32(tmp52, lens_scale);
    float tmp53 = left1_4321_43right_f32_f32(x, lens_sine);
    float tmp54 = left1_4321_43right_f32_f32(y, lens_cosine);
    float tmp55 = left1_4331_43right_f32_f32(tmp53, tmp54);
    float universe_y = left1_4321_43right_f32_f32(tmp55, lens_scale);
    float tmp56 = 1.7999999523162841796875;
    float tmp57 = left1_4321_43right_f32_f32(universe_x, tmp56);
    float tmp58 = 0.02500000037252902984619140625;
    float tmp59 = left1_4321_43right_f32_f32(time, tmp58);
    float tmp60 = left1_4331_43right_f32_f32(tmp57, tmp59);
    float tmp61 = 1.7999999523162841796875;
    float tmp62 = left1_4321_43right_f32_f32(universe_y, tmp61);
    float tmp63 = 4.0;
    float tmp64 = left1_4351_43right_f32_f32(tmp62, tmp63);
    float tmp65 = 1.7000000476837158203125;
    float cloud_one = flowing_field_at_x_y_phase_f32_f32_f32(tmp60, tmp64, tmp65);
    float tmp66 = 4.599999904632568359375;
    float tmp67 = left1_4321_43right_f32_f32(universe_x, tmp66);
    float tmp68 = 11.0;
    float tmp69 = left1_4351_43right_f32_f32(tmp67, tmp68);
    float tmp70 = 4.599999904632568359375;
    float tmp71 = left1_4321_43right_f32_f32(universe_y, tmp70);
    float tmp72 = 0.017999999225139617919921875;
    float tmp73 = left1_4321_43right_f32_f32(time, tmp72);
    float tmp74 = left1_4331_43right_f32_f32(tmp71, tmp73);
    float tmp75 = 5.30000019073486328125;
    float cloud_two = flowing_field_at_x_y_phase_f32_f32_f32(tmp69, tmp74, tmp75);
    float tmp76 = 2.400000095367431640625;
    float tmp77 = left1_4321_43right_f32_f32(universe_x, tmp76);
    float tmp78 = 7.0;
    float tmp79 = left1_4331_43right_f32_f32(tmp77, tmp78);
    float tmp80 = 2.400000095367431640625;
    float tmp81 = left1_4321_43right_f32_f32(universe_y, tmp80);
    float tmp82 = 9.0;
    float tmp83 = left1_4351_43right_f32_f32(tmp81, tmp82);
    float tmp84 = 0.04500000178813934326171875;
    float tmp85 = left1_4321_43right_f32_f32(time, tmp84);
    float cloud_ridge = ridged_field_at_x_y_phase_f32_f32_f32(tmp79, tmp83, tmp85);
    float tmp86 = 2.7000000476837158203125;
    float tmp87 = left1_4321_43right_f32_f32(universe_x, tmp86);
    float tmp88 = 3.599999904632568359375;
    float tmp89 = left1_4321_43right_f32_f32(universe_y, tmp88);
    float tmp90 = left1_4351_43right_f32_f32(tmp87, tmp89);
    float tmp91 = 8.0;
    float tmp92 = left1_4321_43right_f32_f32(cloud_one, tmp91);
    float aurora_phase = left1_4331_43right_f32_f32(tmp90, tmp92);
    float tmp93 = _the43_sine_of_value_f32(aurora_phase);
    float tmp94 = 0.5;
    float tmp95 = left1_4321_43right_f32_f32(tmp93, tmp94);
    float tmp96 = 0.5;
    float aurora = left1_4331_43right_f32_f32(tmp95, tmp96);
    float tmp97 = 0.449999988079071044921875;
    float tmp98 = 0.86000001430511474609375;
    float cloud_light = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp97, tmp98, cloud_one);
    float tmp99 = 0.519999980926513671875;
    float tmp100 = 0.89999997615814208984375;
    float dust_light = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp99, tmp100, cloud_two);
    float tmp101 = 0.00200000009499490261077880859375;
    float tmp102 = 0.02199999988079071044921875;
    float tmp103 = left1_4321_43right_f32_f32(cloud_light, tmp102);
    float tmp104 = left1_4331_43right_f32_f32(tmp101, tmp103);
    float tmp105 = left1_4321_43right_f32_f32(cloud_ridge, aurora);
    float tmp106 = 0.07500000298023223876953125;
    float tmp107 = left1_4321_43right_f32_f32(tmp105, tmp106);
    float red = left1_4331_43right_f32_f32(tmp104, tmp107);
    float tmp108 = 0.0040000001899898052215576171875;
    float tmp109 = 0.0280000008642673492431640625;
    float tmp110 = left1_4321_43right_f32_f32(cloud_light, tmp109);
    float tmp111 = left1_4331_43right_f32_f32(tmp108, tmp110);
    float tmp112 = left1_4321_43right_f32_f32(dust_light, aurora);
    float tmp113 = 0.05200000107288360595703125;
    float tmp114 = left1_4321_43right_f32_f32(tmp112, tmp113);
    float green = left1_4331_43right_f32_f32(tmp111, tmp114);
    float tmp115 = 0.01400000043213367462158203125;
    float tmp116 = 0.12999999523162841796875;
    float tmp117 = left1_4321_43right_f32_f32(cloud_light, tmp116);
    float tmp118 = left1_4331_43right_f32_f32(tmp115, tmp117);
    float tmp119 = 1.0;
    float tmp120 = left1_4351_43right_f32_f32(tmp119, aurora);
    float tmp121 = left1_4321_43right_f32_f32(dust_light, tmp120);
    float tmp122 = 0.23999999463558197021484375;
    float tmp123 = left1_4321_43right_f32_f32(tmp121, tmp122);
    float blue = left1_4331_43right_f32_f32(tmp118, tmp123);
    float tmp124 = 13.0;
    float tmp125 = 0.0900000035762786865234375;
    float stars_far = moving_star_field_at_x_y_with_scale_phase_time_f32_f32_f32_f32_f32(universe_x, universe_y, tmp124, tmp125, time);
    float tmp126 = 21.0;
    float tmp127 = 0.4099999964237213134765625;
    float stars_mid = moving_star_field_at_x_y_with_scale_phase_time_f32_f32_f32_f32_f32(universe_x, universe_y, tmp126, tmp127, time);
    float tmp128 = 31.0;
    float tmp129 = 0.769999980926513671875;
    float stars_near = moving_star_field_at_x_y_with_scale_phase_time_f32_f32_f32_f32_f32(universe_x, universe_y, tmp128, tmp129, time);
    float tmp130 = 0.4600000083446502685546875;
    float tmp131 = left1_4321_43right_f32_f32(stars_far, tmp130);
    float tmp132 = 0.7599999904632568359375;
    float tmp133 = left1_4321_43right_f32_f32(stars_mid, tmp132);
    float tmp134 = left1_4331_43right_f32_f32(tmp131, tmp133);
    float tmp135 = 1.13999998569488525390625;
    float tmp136 = left1_4321_43right_f32_f32(stars_near, tmp135);
    float stars = left1_4331_43right_f32_f32(tmp134, tmp136);
    float tmp137 = 5.099999904632568359375;
    float tmp138 = left1_4321_43right_f32_f32(universe_x, tmp137);
    float tmp139 = 5.099999904632568359375;
    float tmp140 = left1_4321_43right_f32_f32(universe_y, tmp139);
    float tmp141 = 8.19999980926513671875;
    float star_color = flowing_field_at_x_y_phase_f32_f32_f32(tmp138, tmp140, tmp141);
    float tmp142 = 0.579999983310699462890625;
    float tmp143 = 0.62000000476837158203125;
    float tmp144 = left1_4321_43right_f32_f32(star_color, tmp143);
    float tmp145 = left1_4331_43right_f32_f32(tmp142, tmp144);
    float tmp146 = left1_4321_43right_f32_f32(stars, tmp145);
    red = left1_4331_43right_f32_f32(red, tmp146);
    float tmp147 = 0.699999988079071044921875;
    float tmp148 = 0.310000002384185791015625;
    float tmp149 = left1_4321_43right_f32_f32(star_color, tmp148);
    float tmp150 = left1_4331_43right_f32_f32(tmp147, tmp149);
    float tmp151 = left1_4321_43right_f32_f32(stars, tmp150);
    green = left1_4331_43right_f32_f32(green, tmp151);
    float tmp152 = 1.25;
    float tmp153 = 0.180000007152557373046875;
    float tmp154 = left1_4321_43right_f32_f32(star_color, tmp153);
    float tmp155 = left1_4351_43right_f32_f32(tmp152, tmp154);
    float tmp156 = left1_4321_43right_f32_f32(stars, tmp155);
    blue = left1_4331_43right_f32_f32(blue, tmp156);
    float tmp157 = 0.119999997317790985107421875;
    float tmp158 = 0.17000000178813934326171875;
    float tmp159 = left1_4321_43right_f32_f32(time, tmp158);
    float tmp160 = _the43_sine_of_value_f32(tmp159);
    float tmp161 = 0.017999999225139617919921875;
    float tmp162 = left1_4321_43right_f32_f32(tmp160, tmp161);
    float tilt = left1_4331_43right_f32_f32(tmp157, tmp162);
    float tilt_sine = _the43_sine_of_value_f32(tilt);
    float tilt_cosine = _the43_cosine_of_value_f32(tilt);
    float tmp163 = left1_4321_43right_f32_f32(x, tilt_cosine);
    float tmp164 = left1_4321_43right_f32_f32(y, tilt_sine);
    float disk_x = left1_4331_43right_f32_f32(tmp163, tmp164);
    float tmp165 = left1_4321_43right_f32_f32(y, tilt_cosine);
    float tmp166 = left1_4321_43right_f32_f32(x, tilt_sine);
    float disk_y = left1_4351_43right_f32_f32(tmp165, tmp166);
    float tmp167 = 7.19999980926513671875;
    float stretched_y = left1_4321_43right_f32_f32(disk_y, tmp167);
    float tmp168 = left1_4321_43right_f32_f32(disk_x, disk_x);
    float tmp169 = left1_4321_43right_f32_f32(stretched_y, stretched_y);
    float tmp170 = left1_4331_43right_f32_f32(tmp168, tmp169);
    float disk_radius = _the43_square_root_of_value_f32(tmp170);
    float tmp171 = 0.23000000417232513427734375;
    float tmp172 = 0.2849999964237213134765625;
    float disk_inner = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp171, tmp172, disk_radius);
    float tmp173 = 1.0;
    float tmp174 = 0.7200000286102294921875;
    float tmp175 = 1.12000000476837158203125;
    float tmp176 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp174, tmp175, disk_radius);
    float disk_outer = left1_4351_43right_f32_f32(tmp173, tmp176);
    float disk_mask = left1_4321_43right_f32_f32(disk_inner, disk_outer);
    float tmp177 = 8.30000019073486328125;
    float tmp178 = left1_4321_43right_f32_f32(disk_x, tmp177);
    float tmp179 = 0.519999980926513671875;
    float tmp180 = left1_4321_43right_f32_f32(time, tmp179);
    float tmp181 = left1_4331_43right_f32_f32(tmp178, tmp180);
    float tmp182 = 1.7999999523162841796875;
    float tmp183 = left1_4321_43right_f32_f32(stretched_y, tmp182);
    float tmp184 = 3.099999904632568359375;
    float turbulence = signed_flow_at_x_y_phase_f32_f32_f32(tmp181, tmp183, tmp184);
    float tmp185 = 18.0;
    float tmp186 = left1_4321_43right_f32_f32(disk_x, tmp185);
    float tmp187 = 0.910000026226043701171875;
    float tmp188 = left1_4321_43right_f32_f32(time, tmp187);
    float tmp189 = left1_4351_43right_f32_f32(tmp186, tmp188);
    float tmp190 = 3.7000000476837158203125;
    float tmp191 = left1_4321_43right_f32_f32(stretched_y, tmp190);
    float tmp192 = 7.900000095367431640625;
    float fine_turbulence = signed_flow_at_x_y_phase_f32_f32_f32(tmp189, tmp191, tmp192);
    float tmp193 = 39.0;
    float tmp194 = left1_4321_43right_f32_f32(disk_radius, tmp193);
    float tmp195 = 6.80000019073486328125;
    float tmp196 = left1_4321_43right_f32_f32(time, tmp195);
    float tmp197 = left1_4351_43right_f32_f32(tmp194, tmp196);
    float tmp198 = 7.400000095367431640625;
    float tmp199 = left1_4321_43right_f32_f32(turbulence, tmp198);
    float tmp200 = left1_4331_43right_f32_f32(tmp197, tmp199);
    float tmp201 = 2.2000000476837158203125;
    float tmp202 = left1_4321_43right_f32_f32(fine_turbulence, tmp201);
    float spiral_phase = left1_4331_43right_f32_f32(tmp200, tmp202);
    float tmp203 = _the43_sine_of_value_f32(spiral_phase);
    float tmp204 = 0.5;
    float tmp205 = left1_4321_43right_f32_f32(tmp203, tmp204);
    float tmp206 = 0.5;
    float spiral = left1_4331_43right_f32_f32(tmp205, tmp206);
    float tmp207 = 71.0;
    float tmp208 = left1_4321_43right_f32_f32(disk_radius, tmp207);
    float tmp209 = 10.3999996185302734375;
    float tmp210 = left1_4321_43right_f32_f32(time, tmp209);
    float tmp211 = left1_4351_43right_f32_f32(tmp208, tmp210);
    float tmp212 = 4.0;
    float tmp213 = left1_4321_43right_f32_f32(turbulence, tmp212);
    float braided_phase = left1_4351_43right_f32_f32(tmp211, tmp213);
    float tmp214 = _the43_cosine_of_value_f32(braided_phase);
    float tmp215 = 0.5;
    float tmp216 = left1_4321_43right_f32_f32(tmp214, tmp215);
    float tmp217 = 0.5;
    float braided = left1_4331_43right_f32_f32(tmp216, tmp217);
    float tmp218 = 1.0;
    float tmp219 = 0.2899999916553497314453125;
    float tmp220 = 0.829999983310699462890625;
    float tmp221 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp219, tmp220, disk_radius);
    float hot_inner = left1_4351_43right_f32_f32(tmp218, tmp221);
    float tmp222 = 0.189999997615814208984375;
    float tmp223 = 0.579999983310699462890625;
    float tmp224 = left1_4321_43right_f32_f32(spiral, tmp223);
    float tmp225 = left1_4331_43right_f32_f32(tmp222, tmp224);
    float tmp226 = 0.23000000417232513427734375;
    float tmp227 = left1_4321_43right_f32_f32(braided, tmp226);
    float tmp228 = left1_4331_43right_f32_f32(tmp225, tmp227);
    float tmp229 = left1_4321_43right_f32_f32(disk_mask, tmp228);
    float tmp230 = 0.550000011920928955078125;
    float tmp231 = 4.400000095367431640625;
    float tmp232 = left1_4321_43right_f32_f32(hot_inner, tmp231);
    float tmp233 = left1_4331_43right_f32_f32(tmp230, tmp232);
    float disk_energy = left1_4321_43right_f32_f32(tmp229, tmp233);
    float tmp234 = 0.001000000047497451305389404296875;
    float tmp235 = _the43_maximum_of_a_and_b_f32_f32(disk_radius, tmp234);
    float tmp236 = left1_4371_43right_f32_f32(disk_x, tmp235);
    float tmp237 = 0.5;
    float tmp238 = left1_4321_43right_f32_f32(tmp236, tmp237);
    float tmp239 = 0.5;
    float tmp240 = left1_4331_43right_f32_f32(tmp238, tmp239);
    float approaching = saturate_number_f32(tmp240);
    float tmp241 = 1.7200000286102294921875;
    float tmp242 = 0.62000000476837158203125;
    float tmp243 = left1_4321_43right_f32_f32(approaching, tmp242);
    float tmp244 = left1_4351_43right_f32_f32(tmp241, tmp243);
    float tmp245 = left1_4321_43right_f32_f32(disk_energy, tmp244);
    red = left1_4331_43right_f32_f32(red, tmp245);
    float tmp246 = 0.300000011920928955078125;
    float tmp247 = 1.03999996185302734375;
    float tmp248 = left1_4321_43right_f32_f32(approaching, tmp247);
    float tmp249 = left1_4331_43right_f32_f32(tmp246, tmp248);
    float tmp250 = left1_4321_43right_f32_f32(disk_energy, tmp249);
    green = left1_4331_43right_f32_f32(green, tmp250);
    float tmp251 = 0.054999999701976776123046875;
    float tmp252 = 2.1800000667572021484375;
    float tmp253 = left1_4321_43right_f32_f32(approaching, tmp252);
    float tmp254 = left1_4331_43right_f32_f32(tmp251, tmp253);
    float tmp255 = left1_4321_43right_f32_f32(disk_energy, tmp254);
    blue = left1_4331_43right_f32_f32(blue, tmp255);
    float jet_axis = _the43_absolute_value_of_magnitude_f32(x);
    float tmp256 = 0.0;
    float tmp257 = 0.0320000015199184417724609375;
    float jet_core = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp256, tmp257, jet_axis);
    float tmp258 = 0.017999999225139617919921875;
    float tmp259 = 0.1599999964237213134765625;
    float jet_halo = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp258, tmp259, jet_axis);
    float tmp260 = 0.189999997615814208984375;
    float tmp261 = 0.7200000286102294921875;
    float tmp262 = _the43_absolute_value_of_magnitude_f32(y);
    float jet_reach = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp260, tmp261, tmp262);
    float tmp263 = 15.0;
    float tmp264 = left1_4321_43right_f32_f32(x, tmp263);
    float tmp265 = 3.0;
    float tmp266 = left1_4321_43right_f32_f32(y, tmp265);
    float tmp267 = 0.800000011920928955078125;
    float tmp268 = left1_4321_43right_f32_f32(time, tmp267);
    float tmp269 = left1_4351_43right_f32_f32(tmp266, tmp268);
    float tmp270 = 10.0;
    float jet_flicker = flowing_field_at_x_y_phase_f32_f32_f32(tmp264, tmp269, tmp270);
    float tmp271 = 0.519999980926513671875;
    float tmp272 = left1_4321_43right_f32_f32(jet_core, tmp271);
    float tmp273 = 0.12999999523162841796875;
    float tmp274 = left1_4321_43right_f32_f32(jet_halo, tmp273);
    float tmp275 = left1_4331_43right_f32_f32(tmp272, tmp274);
    float tmp276 = left1_4321_43right_f32_f32(tmp275, jet_reach);
    float tmp277 = 0.3499999940395355224609375;
    float tmp278 = 0.64999997615814208984375;
    float tmp279 = left1_4321_43right_f32_f32(jet_flicker, tmp278);
    float tmp280 = left1_4331_43right_f32_f32(tmp277, tmp279);
    float jet = left1_4321_43right_f32_f32(tmp276, tmp280);
    float tmp281 = 0.20999999344348907470703125;
    float tmp282 = left1_4321_43right_f32_f32(jet, tmp281);
    red = left1_4331_43right_f32_f32(red, tmp282);
    float tmp283 = 0.4799999892711639404296875;
    float tmp284 = left1_4321_43right_f32_f32(jet, tmp283);
    green = left1_4331_43right_f32_f32(green, tmp284);
    float tmp285 = 1.36000001430511474609375;
    float tmp286 = left1_4321_43right_f32_f32(jet, tmp285);
    blue = left1_4331_43right_f32_f32(blue, tmp286);
    float tmp287 = 1.0;
    float tmp288 = 0.17599999904632568359375;
    float tmp289 = 0.20200000703334808349609375;
    float tmp290 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp288, tmp289, radius);
    float horizon = left1_4351_43right_f32_f32(tmp287, tmp290);
    float tmp291 = 1.0;
    float tmp292 = left1_4351_43right_f32_f32(tmp291, horizon);
    red = left1_4321_43right_f32_f32(red, tmp292);
    float tmp293 = 1.0;
    float tmp294 = left1_4351_43right_f32_f32(tmp293, horizon);
    green = left1_4321_43right_f32_f32(green, tmp294);
    float tmp295 = 1.0;
    float tmp296 = left1_4351_43right_f32_f32(tmp295, horizon);
    blue = left1_4321_43right_f32_f32(blue, tmp296);
    float tmp297 = 1.0;
    float tmp298 = 0.01899999938905239105224609375;
    float tmp299 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp298);
    float tmp300 = 0.014999999664723873138427734375;
    float tmp301 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp299, tmp300, disk_y);
    float near_side = left1_4351_43right_f32_f32(tmp297, tmp301);
    float foreground_disk = left1_4321_43right_f32_f32(disk_energy, near_side);
    float tmp302 = 1.61000001430511474609375;
    float tmp303 = 0.4799999892711639404296875;
    float tmp304 = left1_4321_43right_f32_f32(approaching, tmp303);
    float tmp305 = left1_4351_43right_f32_f32(tmp302, tmp304);
    float tmp306 = left1_4321_43right_f32_f32(foreground_disk, tmp305);
    red = left1_4331_43right_f32_f32(red, tmp306);
    float tmp307 = 0.3400000035762786865234375;
    float tmp308 = 0.930000007152557373046875;
    float tmp309 = left1_4321_43right_f32_f32(approaching, tmp308);
    float tmp310 = left1_4331_43right_f32_f32(tmp307, tmp309);
    float tmp311 = left1_4321_43right_f32_f32(foreground_disk, tmp310);
    green = left1_4331_43right_f32_f32(green, tmp311);
    float tmp312 = 0.070000000298023223876953125;
    float tmp313 = 1.940000057220458984375;
    float tmp314 = left1_4321_43right_f32_f32(approaching, tmp313);
    float tmp315 = left1_4331_43right_f32_f32(tmp312, tmp314);
    float tmp316 = left1_4321_43right_f32_f32(foreground_disk, tmp315);
    blue = left1_4331_43right_f32_f32(blue, tmp316);
    float tmp317 = 0.21600000560283660888671875;
    float tmp318 = left1_4351_43right_f32_f32(radius, tmp317);
    float photon_distance = _the43_absolute_value_of_magnitude_f32(tmp318);
    float tmp319 = 0.001000000047497451305389404296875;
    float tmp320 = 0.0170000009238719940185546875;
    float photon_ring = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp319, tmp320, photon_distance);
    float tmp321 = left1_4371_43right_f32_f32(y, safe_radius);
    float polar_amount = _the43_absolute_value_of_magnitude_f32(tmp321);
    float tmp322 = 0.1599999964237213134765625;
    float tmp323 = 0.86000001430511474609375;
    float tmp324 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp322, tmp323, polar_amount);
    float polar_arcs = left1_4321_43right_f32_f32(photon_ring, tmp324);
    float tmp325 = 2.099999904632568359375;
    float tmp326 = left1_4321_43right_f32_f32(time, tmp325);
    float tmp327 = 5.0;
    float tmp328 = left1_4321_43right_f32_f32(turbulence, tmp327);
    float tmp329 = left1_4331_43right_f32_f32(tmp326, tmp328);
    float tmp330 = _the43_sine_of_value_f32(tmp329);
    float tmp331 = 0.100000001490116119384765625;
    float tmp332 = left1_4321_43right_f32_f32(tmp330, tmp331);
    float tmp333 = 0.89999997615814208984375;
    float ring_flicker = left1_4331_43right_f32_f32(tmp332, tmp333);
    float tmp334 = 1.34000003337860107421875;
    float tmp335 = left1_4321_43right_f32_f32(photon_ring, tmp334);
    float tmp336 = left1_4321_43right_f32_f32(tmp335, ring_flicker);
    float tmp337 = left1_4331_43right_f32_f32(red, tmp336);
    float tmp338 = 2.2000000476837158203125;
    float tmp339 = left1_4321_43right_f32_f32(polar_arcs, tmp338);
    red = left1_4331_43right_f32_f32(tmp337, tmp339);
    float tmp340 = 0.7200000286102294921875;
    float tmp341 = left1_4321_43right_f32_f32(photon_ring, tmp340);
    float tmp342 = left1_4321_43right_f32_f32(tmp341, ring_flicker);
    float tmp343 = left1_4331_43right_f32_f32(green, tmp342);
    float tmp344 = 0.930000007152557373046875;
    float tmp345 = left1_4321_43right_f32_f32(polar_arcs, tmp344);
    green = left1_4331_43right_f32_f32(tmp343, tmp345);
    float tmp346 = 0.439999997615814208984375;
    float tmp347 = left1_4321_43right_f32_f32(photon_ring, tmp346);
    float tmp348 = left1_4321_43right_f32_f32(tmp347, ring_flicker);
    float tmp349 = left1_4331_43right_f32_f32(blue, tmp348);
    float tmp350 = 1.480000019073486328125;
    float tmp351 = left1_4321_43right_f32_f32(polar_arcs, tmp350);
    blue = left1_4331_43right_f32_f32(tmp349, tmp351);
    float tmp352 = 0.20200000703334808349609375;
    float tmp353 = left1_4351_43right_f32_f32(radius, tmp352);
    float horizon_gap = _the43_absolute_value_of_magnitude_f32(tmp353);
    float tmp354 = 0.039999999105930328369140625;
    float tmp355 = 0.039999999105930328369140625;
    float tmp356 = left1_4331_43right_f32_f32(horizon_gap, tmp355);
    float tmp357 = left1_4371_43right_f32_f32(tmp354, tmp356);
    float tmp358 = 1.0;
    float tmp359 = left1_4351_43right_f32_f32(tmp358, horizon);
    float halo = left1_4321_43right_f32_f32(tmp357, tmp359);
    float tmp360 = 0.189999997615814208984375;
    float tmp361 = left1_4321_43right_f32_f32(halo, tmp360);
    red = left1_4331_43right_f32_f32(red, tmp361);
    float tmp362 = 0.070000000298023223876953125;
    float tmp363 = left1_4321_43right_f32_f32(halo, tmp362);
    green = left1_4331_43right_f32_f32(green, tmp363);
    float tmp364 = 0.310000002384185791015625;
    float tmp365 = left1_4321_43right_f32_f32(halo, tmp364);
    blue = left1_4331_43right_f32_f32(blue, tmp365);
    float tmp366 = left1_4371_43right_f32_f32(screen_x, aspect);
    float tmp367 = left1_4371_43right_f32_f32(screen_x, aspect);
    float tmp368 = left1_4321_43right_f32_f32(tmp366, tmp367);
    float tmp369 = left1_4321_43right_f32_f32(screen_y, screen_y);
    float tmp370 = left1_4331_43right_f32_f32(tmp368, tmp369);
    float vignette_radius = _the43_square_root_of_value_f32(tmp370);
    float tmp371 = 0.180000007152557373046875;
    float tmp372 = 1.0;
    float tmp373 = 0.4799999892711639404296875;
    float tmp374 = 1.34000003337860107421875;
    float tmp375 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp373, tmp374, vignette_radius);
    float tmp376 = left1_4351_43right_f32_f32(tmp372, tmp375);
    float tmp377 = 0.819999992847442626953125;
    float tmp378 = left1_4321_43right_f32_f32(tmp376, tmp377);
    float vignette = left1_4331_43right_f32_f32(tmp371, tmp378);
    float tmp379 = left1_4321_43right_f32_f32(red, vignette);
    float tmp380 = 1.0;
    float tmp381 = left1_4331_43right_f32_f32(tmp380, red);
    float tmp382 = left1_4371_43right_f32_f32(tmp379, tmp381);
    red = _the43_square_root_of_value_f32(tmp382);
    float tmp383 = left1_4321_43right_f32_f32(green, vignette);
    float tmp384 = 1.0;
    float tmp385 = left1_4331_43right_f32_f32(tmp384, green);
    float tmp386 = left1_4371_43right_f32_f32(tmp383, tmp385);
    green = _the43_square_root_of_value_f32(tmp386);
    float tmp387 = left1_4321_43right_f32_f32(blue, vignette);
    float tmp388 = 1.0;
    float tmp389 = left1_4331_43right_f32_f32(tmp388, blue);
    float tmp390 = left1_4371_43right_f32_f32(tmp387, tmp389);
    blue = _the43_square_root_of_value_f32(tmp390);
    vec4 _967 = vec4(0.0, 0.0, 0.0, 1.0);
    _967.z = blue;
    _967.y = green;
    _967.x = red;
    dynlexColor = _967;
}
