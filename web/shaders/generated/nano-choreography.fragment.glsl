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

float _the43_floor_of_value_f32(float value)
{
    return floor(value);
}

float fractional_part_of_number_f32(float number)
{
    float tmp = _the43_floor_of_value_f32(number);
    return left1_4351_43right_f32_f32(number, tmp);
}

bool left_2_right_f32_f32(float left, float right)
{
    return left > right;
}

float _the_431negative_1of_34opposite_1of_3453value_f32(float value)
{
    return -value;
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

float _the43_minimum_of_a_and_b_f32_f32(float a, float b)
{
    return isnan(b) ? a : (isnan(a) ? b : min(a, b));
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

bool left_1is_greater_than_or_equal_to4213_right_f32_f32(float left, float right)
{
    return left >= right;
}

bool _boolean8left5_and_3boolean8right5_bool_bool(bool left, bool right)
{
    return left && right;
}

bool value_as_destinationtype_i32_type_ct_destinationtype_type(uint value)
{
    return value != 0u;
}

bool left_1is_less_than_or_equal_to4013_right_f32_f32(float left, float right)
{
    return left <= right;
}

bool left_or_right_bool_bool(bool left, bool right)
{
    return left || right;
}

bool left_0_right_i32_i32(uint left, uint right)
{
    return int(left) < int(right);
}

float nano_drone_field_at_x_y_z_phase_f32_f32_f32_f32(float x, float y, float z, float phase)
{
    float tmp = 43.130001068115234375;
    float tmp1 = left1_4321_43right_f32_f32(x, tmp);
    float tmp2 = 17.70999908447265625;
    float tmp3 = left1_4321_43right_f32_f32(y, tmp2);
    float tmp4 = 7.190000057220458984375;
    float tmp5 = left1_4321_43right_f32_f32(z, tmp4);
    float tmp6 = left1_4331_43right_f32_f32(tmp5, phase);
    float tmp7 = left1_4331_43right_f32_f32(tmp3, tmp6);
    float tmp8 = left1_4331_43right_f32_f32(tmp1, tmp7);
    float tmp9 = _the43_sine_of_value_f32(tmp8);
    float wave_a = _the43_absolute_value_of_magnitude_f32(tmp9);
    float tmp10 = 13.36999988555908203125;
    float tmp11 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp10);
    float tmp12 = left1_4321_43right_f32_f32(x, tmp11);
    float tmp13 = 47.06999969482421875;
    float tmp14 = left1_4321_43right_f32_f32(y, tmp13);
    float tmp15 = 21.409999847412109375;
    float tmp16 = left1_4321_43right_f32_f32(z, tmp15);
    float tmp17 = 0.730000019073486328125;
    float tmp18 = left1_4321_43right_f32_f32(phase, tmp17);
    float tmp19 = left1_4351_43right_f32_f32(tmp16, tmp18);
    float tmp20 = left1_4331_43right_f32_f32(tmp14, tmp19);
    float tmp21 = left1_4331_43right_f32_f32(tmp12, tmp20);
    float tmp22 = _the43_sine_of_value_f32(tmp21);
    float wave_b = _the43_absolute_value_of_magnitude_f32(tmp22);
    float tmp23 = 29.79000091552734375;
    float tmp24 = left1_4321_43right_f32_f32(x, tmp23);
    float tmp25 = 11.909999847412109375;
    float tmp26 = left1_4321_43right_f32_f32(y, tmp25);
    float tmp27 = 59.3300018310546875;
    float tmp28 = left1_4321_43right_f32_f32(z, tmp27);
    float tmp29 = 1.309999942779541015625;
    float tmp30 = left1_4321_43right_f32_f32(phase, tmp29);
    float tmp31 = left1_4331_43right_f32_f32(tmp28, tmp30);
    float tmp32 = left1_4331_43right_f32_f32(tmp26, tmp31);
    float tmp33 = left1_4351_43right_f32_f32(tmp24, tmp32);
    float tmp34 = _the43_sine_of_value_f32(tmp33);
    float wave_c = _the43_absolute_value_of_magnitude_f32(tmp34);
    float tmp35 = left1_4321_43right_f32_f32(wave_a, wave_a);
    float tmp36 = left1_4321_43right_f32_f32(wave_b, wave_b);
    float tmp37 = left1_4331_43right_f32_f32(tmp35, tmp36);
    float tmp38 = left1_4321_43right_f32_f32(wave_c, wave_c);
    float tmp39 = left1_4331_43right_f32_f32(tmp37, tmp38);
    float crossing_distance = _the43_square_root_of_value_f32(tmp39);
    float tmp40 = 0.0;
    float tmp41 = 0.4199999868869781494140625;
    float core = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp40, tmp41, crossing_distance);
    float tmp42 = 0.0;
    float tmp43 = 0.699999988079071044921875;
    float aura = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp42, tmp43, crossing_distance);
    float tmp44 = 4.19999980926513671875;
    float tmp45 = left1_4321_43right_f32_f32(core, tmp44);
    float tmp46 = 0.1599999964237213134765625;
    float tmp47 = left1_4321_43right_f32_f32(aura, tmp46);
    return left1_4331_43right_f32_f32(tmp45, tmp47);
}

float torus_distance_at_x_y_z_with_major_radius_major_radius_and_tube_radius_tube_radius_f32_f32_f32_f32_f32(float x, float y, float z, float major_radius, float tube_radius)
{
    float tmp = left1_4321_43right_f32_f32(x, x);
    float tmp1 = left1_4321_43right_f32_f32(y, y);
    float tmp2 = left1_4331_43right_f32_f32(tmp, tmp1);
    float tmp3 = _the43_square_root_of_value_f32(tmp2);
    float ring_offset = left1_4351_43right_f32_f32(tmp3, major_radius);
    float tmp4 = left1_4321_43right_f32_f32(ring_offset, ring_offset);
    float tmp5 = left1_4321_43right_f32_f32(z, z);
    float tmp6 = left1_4331_43right_f32_f32(tmp4, tmp5);
    float tmp7 = _the43_square_root_of_value_f32(tmp6);
    return left1_4351_43right_f32_f32(tmp7, tube_radius);
}

float ellipsoid_distance_at_x_y_z_with_radii_radius_x_radius_y_radius_z_f32_f32_f32_f32_f32_f32(float x, float y, float z, float radius_x, float radius_y, float radius_z)
{
    float scaled_x = left1_4371_43right_f32_f32(x, radius_x);
    float scaled_y = left1_4371_43right_f32_f32(y, radius_y);
    float scaled_z = left1_4371_43right_f32_f32(z, radius_z);
    float tmp = left1_4321_43right_f32_f32(scaled_x, scaled_x);
    float tmp1 = left1_4321_43right_f32_f32(scaled_y, scaled_y);
    float tmp2 = left1_4331_43right_f32_f32(tmp, tmp1);
    float tmp3 = left1_4321_43right_f32_f32(scaled_z, scaled_z);
    float tmp4 = left1_4331_43right_f32_f32(tmp2, tmp3);
    float scaled_length = _the43_square_root_of_value_f32(tmp4);
    float tmp5 = _the43_minimum_of_a_and_b_f32_f32(radius_y, radius_z);
    float smallest_radius = _the43_minimum_of_a_and_b_f32_f32(radius_x, tmp5);
    float tmp6 = 1.0;
    float tmp7 = left1_4351_43right_f32_f32(scaled_length, tmp6);
    return left1_4321_43right_f32_f32(tmp7, smallest_radius);
}

float distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(float x, float y, float z, float ax, float ay, float az, float bx, float by, float bz, float radius)
{
    float segment_x = left1_4351_43right_f32_f32(bx, ax);
    float segment_y = left1_4351_43right_f32_f32(by, ay);
    float segment_z = left1_4351_43right_f32_f32(bz, az);
    float point_x = left1_4351_43right_f32_f32(x, ax);
    float point_y = left1_4351_43right_f32_f32(y, ay);
    float point_z = left1_4351_43right_f32_f32(z, az);
    float tmp = left1_4321_43right_f32_f32(segment_x, segment_x);
    float tmp1 = left1_4321_43right_f32_f32(segment_y, segment_y);
    float tmp2 = left1_4331_43right_f32_f32(tmp, tmp1);
    float tmp3 = left1_4321_43right_f32_f32(segment_z, segment_z);
    float length_squared = left1_4331_43right_f32_f32(tmp2, tmp3);
    float tmp4 = left1_4321_43right_f32_f32(point_x, segment_x);
    float tmp5 = left1_4321_43right_f32_f32(point_y, segment_y);
    float tmp6 = left1_4331_43right_f32_f32(tmp4, tmp5);
    float tmp7 = left1_4321_43right_f32_f32(point_z, segment_z);
    float tmp8 = left1_4331_43right_f32_f32(tmp6, tmp7);
    float tmp9 = 9.9999999747524270787835121154785e-07;
    float tmp10 = _the43_maximum_of_a_and_b_f32_f32(length_squared, tmp9);
    float projection = left1_4371_43right_f32_f32(tmp8, tmp10);
    projection = saturate_number_f32(projection);
    float tmp11 = left1_4321_43right_f32_f32(segment_x, projection);
    float nearest_x = left1_4331_43right_f32_f32(ax, tmp11);
    float tmp12 = left1_4321_43right_f32_f32(segment_y, projection);
    float nearest_y = left1_4331_43right_f32_f32(ay, tmp12);
    float tmp13 = left1_4321_43right_f32_f32(segment_z, projection);
    float nearest_z = left1_4331_43right_f32_f32(az, tmp13);
    float delta_x = left1_4351_43right_f32_f32(x, nearest_x);
    float delta_y = left1_4351_43right_f32_f32(y, nearest_y);
    float delta_z = left1_4351_43right_f32_f32(z, nearest_z);
    float tmp14 = left1_4321_43right_f32_f32(delta_x, delta_x);
    float tmp15 = left1_4321_43right_f32_f32(delta_y, delta_y);
    float tmp16 = left1_4321_43right_f32_f32(delta_z, delta_z);
    float tmp17 = left1_4331_43right_f32_f32(tmp15, tmp16);
    float tmp18 = left1_4331_43right_f32_f32(tmp14, tmp17);
    float tmp19 = _the43_square_root_of_value_f32(tmp18);
    return left1_4351_43right_f32_f32(tmp19, radius);
}

float motorcycle_distance_at_x_y_z_with_progress_wheel_spin_motorcycle_yaw_f32_f32_f32_f32_f32_f32(float x, float y, float z, float progress, float wheel_spin, float motorcycle_yaw)
{
    float tmp = 0.0;
    float tmp1 = 1.2599999904632568359375;
    float tmp2 = left1_4351_43right_f32_f32(tmp, tmp1);
    float tmp3 = 1.17999994754791259765625;
    float tmp4 = left1_4321_43right_f32_f32(progress, tmp3);
    float center_x = left1_4331_43right_f32_f32(tmp2, tmp4);
    float tmp5 = 0.0;
    float tmp6 = 0.189999997615814208984375;
    float tmp7 = left1_4351_43right_f32_f32(tmp5, tmp6);
    float tmp8 = 0.119999997317790985107421875;
    float tmp9 = left1_4321_43right_f32_f32(progress, tmp8);
    float center_y = left1_4331_43right_f32_f32(tmp7, tmp9);
    float tmp10 = 2.7999999523162841796875;
    float tmp11 = 4.900000095367431640625;
    float tmp12 = left1_4321_43right_f32_f32(progress, tmp11);
    float center_z = left1_4351_43right_f32_f32(tmp10, tmp12);
    float translated_x = left1_4351_43right_f32_f32(x, center_x);
    float translated_z = left1_4351_43right_f32_f32(z, center_z);
    float yaw_sine = _the43_sine_of_value_f32(motorcycle_yaw);
    float yaw_cosine = _the43_cosine_of_value_f32(motorcycle_yaw);
    float tmp13 = left1_4321_43right_f32_f32(translated_x, yaw_cosine);
    float tmp14 = left1_4321_43right_f32_f32(translated_z, yaw_sine);
    float local_x = left1_4331_43right_f32_f32(tmp13, tmp14);
    float local_y = left1_4351_43right_f32_f32(y, center_y);
    float tmp15 = left1_4321_43right_f32_f32(translated_z, yaw_cosine);
    float tmp16 = left1_4321_43right_f32_f32(translated_x, yaw_sine);
    float local_z = left1_4351_43right_f32_f32(tmp15, tmp16);
    float wheel_depth = 0.12999999523162841796875;
    float tmp17 = 0.7200000286102294921875;
    float rear_x = left1_4331_43right_f32_f32(local_x, tmp17);
    float tmp18 = 0.7200000286102294921875;
    float front_x = left1_4351_43right_f32_f32(local_x, tmp18);
    float tmp19 = 0.4199999868869781494140625;
    float wheel_y = left1_4331_43right_f32_f32(local_y, tmp19);
    float tmp20 = 0.300000011920928955078125;
    float tmp21 = 0.054999999701976776123046875;
    float rear_wheel = torus_distance_at_x_y_z_with_major_radius_major_radius_and_tube_radius_tube_radius_f32_f32_f32_f32_f32(rear_x, wheel_y, local_z, tmp20, tmp21);
    float tmp22 = 0.300000011920928955078125;
    float tmp23 = 0.054999999701976776123046875;
    float front_wheel = torus_distance_at_x_y_z_with_major_radius_major_radius_and_tube_radius_tube_radius_f32_f32_f32_f32_f32(front_x, wheel_y, local_z, tmp22, tmp23);
    float model_distance = _the43_minimum_of_a_and_b_f32_f32(rear_wheel, front_wheel);
    float tmp24 = 0.0949999988079071044921875;
    float tmp25 = 0.0949999988079071044921875;
    float tmp26 = ellipsoid_distance_at_x_y_z_with_radii_radius_x_radius_y_radius_z_f32_f32_f32_f32_f32_f32(rear_x, wheel_y, local_z, tmp24, tmp25, wheel_depth);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp26);
    float tmp27 = 0.0949999988079071044921875;
    float tmp28 = 0.0949999988079071044921875;
    float tmp29 = ellipsoid_distance_at_x_y_z_with_radii_radius_x_radius_y_radius_z_f32_f32_f32_f32_f32_f32(front_x, wheel_y, local_z, tmp27, tmp28, wheel_depth);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp29);
    float spoke_cosine = _the43_cosine_of_value_f32(wheel_spin);
    float spoke_sine = _the43_sine_of_value_f32(wheel_spin);
    float tmp30 = 0.26499998569488525390625;
    float rear_spoke_x = left1_4321_43right_f32_f32(spoke_cosine, tmp30);
    float tmp31 = 0.26499998569488525390625;
    float rear_spoke_y = left1_4321_43right_f32_f32(spoke_sine, tmp31);
    float tmp32 = 0.26499998569488525390625;
    float front_spoke_x = left1_4321_43right_f32_f32(spoke_sine, tmp32);
    float tmp33 = 0.26499998569488525390625;
    float front_spoke_y = left1_4321_43right_f32_f32(spoke_cosine, tmp33);
    float tmp34 = 0.0;
    float tmp35 = left1_4351_43right_f32_f32(tmp34, rear_spoke_x);
    float tmp36 = 0.0;
    float tmp37 = left1_4351_43right_f32_f32(tmp36, rear_spoke_y);
    float tmp38 = 0.0;
    float tmp39 = 0.0;
    float tmp40 = 0.017999999225139617919921875;
    float tmp41 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(rear_x, wheel_y, local_z, tmp35, tmp37, tmp38, rear_spoke_x, rear_spoke_y, tmp39, tmp40);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp41);
    float tmp42 = 0.0;
    float tmp43 = left1_4351_43right_f32_f32(tmp42, rear_spoke_y);
    float tmp44 = 0.0;
    float tmp45 = 0.0;
    float tmp46 = left1_4351_43right_f32_f32(tmp45, rear_spoke_x);
    float tmp47 = 0.0;
    float tmp48 = 0.017999999225139617919921875;
    float tmp49 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(rear_x, wheel_y, local_z, tmp43, rear_spoke_x, tmp44, rear_spoke_y, tmp46, tmp47, tmp48);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp49);
    float tmp50 = 0.0;
    float tmp51 = left1_4351_43right_f32_f32(tmp50, front_spoke_x);
    float tmp52 = 0.0;
    float tmp53 = left1_4351_43right_f32_f32(tmp52, front_spoke_y);
    float tmp54 = 0.0;
    float tmp55 = 0.0;
    float tmp56 = 0.017999999225139617919921875;
    float tmp57 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(front_x, wheel_y, local_z, tmp51, tmp53, tmp54, front_spoke_x, front_spoke_y, tmp55, tmp56);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp57);
    float tmp58 = 0.0;
    float tmp59 = left1_4351_43right_f32_f32(tmp58, front_spoke_y);
    float tmp60 = 0.0;
    float tmp61 = 0.0;
    float tmp62 = left1_4351_43right_f32_f32(tmp61, front_spoke_x);
    float tmp63 = 0.0;
    float tmp64 = 0.017999999225139617919921875;
    float tmp65 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(front_x, wheel_y, local_z, tmp59, front_spoke_x, tmp60, front_spoke_y, tmp62, tmp63, tmp64);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp65);
    float tmp66 = 0.7200000286102294921875;
    float tmp67 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp66);
    float tmp68 = 0.4199999868869781494140625;
    float tmp69 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp68);
    float tmp70 = 0.0;
    float tmp71 = 0.180000007152557373046875;
    float tmp72 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp71);
    float tmp73 = 0.07999999821186065673828125;
    float tmp74 = 0.0;
    float tmp75 = 0.04500000178813934326171875;
    float tmp76 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp67, tmp69, tmp70, tmp72, tmp73, tmp74, tmp75);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp76);
    float tmp77 = 0.180000007152557373046875;
    float tmp78 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp77);
    float tmp79 = 0.07999999821186065673828125;
    float tmp80 = 0.0;
    float tmp81 = 0.7200000286102294921875;
    float tmp82 = 0.4199999868869781494140625;
    float tmp83 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp82);
    float tmp84 = 0.0;
    float tmp85 = 0.04500000178813934326171875;
    float tmp86 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp78, tmp79, tmp80, tmp81, tmp83, tmp84, tmp85);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp86);
    float tmp87 = 0.7200000286102294921875;
    float tmp88 = 0.4199999868869781494140625;
    float tmp89 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp88);
    float tmp90 = 0.0;
    float tmp91 = 0.3400000035762786865234375;
    float tmp92 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp91);
    float tmp93 = 0.4199999868869781494140625;
    float tmp94 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp93);
    float tmp95 = 0.0;
    float tmp96 = 0.04500000178813934326171875;
    float tmp97 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp87, tmp89, tmp90, tmp92, tmp94, tmp95, tmp96);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp97);
    float tmp98 = 0.3400000035762786865234375;
    float tmp99 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp98);
    float tmp100 = 0.4199999868869781494140625;
    float tmp101 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp100);
    float tmp102 = 0.0;
    float tmp103 = 0.180000007152557373046875;
    float tmp104 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp103);
    float tmp105 = 0.07999999821186065673828125;
    float tmp106 = 0.0;
    float tmp107 = 0.04500000178813934326171875;
    float tmp108 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp99, tmp101, tmp102, tmp104, tmp105, tmp106, tmp107);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp108);
    float tmp109 = 0.550000011920928955078125;
    float tmp110 = 0.2800000011920928955078125;
    float tmp111 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp110);
    float tmp112 = 0.070000000298023223876953125;
    float tmp113 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp112);
    float tmp114 = 0.769999980926513671875;
    float tmp115 = 0.2800000011920928955078125;
    float tmp116 = 0.070000000298023223876953125;
    float tmp117 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp116);
    float tmp118 = 0.0320000015199184417724609375;
    float tmp119 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp109, tmp111, tmp113, tmp114, tmp115, tmp117, tmp118);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp119);
    float tmp120 = 0.550000011920928955078125;
    float tmp121 = 0.2800000011920928955078125;
    float tmp122 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp121);
    float tmp123 = 0.070000000298023223876953125;
    float tmp124 = 0.769999980926513671875;
    float tmp125 = 0.2800000011920928955078125;
    float tmp126 = 0.070000000298023223876953125;
    float tmp127 = 0.0320000015199184417724609375;
    float tmp128 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp120, tmp122, tmp123, tmp124, tmp125, tmp126, tmp127);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp128);
    float tmp129 = 0.75;
    float tmp130 = 0.2800000011920928955078125;
    float tmp131 = 0.2800000011920928955078125;
    float tmp132 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp131);
    float tmp133 = 0.75;
    float tmp134 = 0.2800000011920928955078125;
    float tmp135 = 0.2800000011920928955078125;
    float tmp136 = 0.0280000008642673492431640625;
    float tmp137 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp129, tmp130, tmp132, tmp133, tmp134, tmp135, tmp136);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp137);
    float tmp138 = 0.07999999821186065673828125;
    float tmp139 = left1_4331_43right_f32_f32(local_x, tmp138);
    float tmp140 = 0.02999999932944774627685546875;
    float tmp141 = left1_4351_43right_f32_f32(local_y, tmp140);
    float tmp142 = 0.3400000035762786865234375;
    float tmp143 = 0.2199999988079071044921875;
    float tmp144 = 0.25;
    float tmp145 = ellipsoid_distance_at_x_y_z_with_radii_radius_x_radius_y_radius_z_f32_f32_f32_f32_f32_f32(tmp139, tmp141, local_z, tmp142, tmp143, tmp144);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp145);
    float tmp146 = 0.0199999995529651641845703125;
    float tmp147 = left1_4351_43right_f32_f32(local_x, tmp146);
    float tmp148 = 0.2199999988079071044921875;
    float tmp149 = left1_4331_43right_f32_f32(local_y, tmp148);
    float tmp150 = 0.3400000035762786865234375;
    float tmp151 = 0.180000007152557373046875;
    float tmp152 = 0.2199999988079071044921875;
    float tmp153 = ellipsoid_distance_at_x_y_z_with_radii_radius_x_radius_y_radius_z_f32_f32_f32_f32_f32_f32(tmp147, tmp149, local_z, tmp150, tmp151, tmp152);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp153);
    float tmp154 = 0.4000000059604644775390625;
    float tmp155 = left1_4331_43right_f32_f32(local_x, tmp154);
    float tmp156 = 0.23999999463558197021484375;
    float tmp157 = left1_4351_43right_f32_f32(local_y, tmp156);
    float tmp158 = 0.310000002384185791015625;
    float tmp159 = 0.07500000298023223876953125;
    float tmp160 = 0.2199999988079071044921875;
    float tmp161 = ellipsoid_distance_at_x_y_z_with_radii_radius_x_radius_y_radius_z_f32_f32_f32_f32_f32_f32(tmp155, tmp157, local_z, tmp158, tmp159, tmp160);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp161);
    float tmp162 = 0.7799999713897705078125;
    float tmp163 = left1_4351_43right_f32_f32(local_x, tmp162);
    float tmp164 = 0.07999999821186065673828125;
    float tmp165 = left1_4351_43right_f32_f32(local_y, tmp164);
    float tmp166 = 0.085000000894069671630859375;
    float tmp167 = 0.085000000894069671630859375;
    float tmp168 = 0.104999996721744537353515625;
    float tmp169 = ellipsoid_distance_at_x_y_z_with_radii_radius_x_radius_y_radius_z_f32_f32_f32_f32_f32_f32(tmp163, tmp165, local_z, tmp166, tmp167, tmp168);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp169);
    float tmp170 = 0.100000001490116119384765625;
    float tmp171 = left1_4331_43right_f32_f32(local_x, tmp170);
    float tmp172 = 0.7799999713897705078125;
    float tmp173 = left1_4351_43right_f32_f32(local_y, tmp172);
    float tmp174 = 0.1500000059604644775390625;
    float tmp175 = 0.17000000178813934326171875;
    float tmp176 = 0.1500000059604644775390625;
    float tmp177 = ellipsoid_distance_at_x_y_z_with_radii_radius_x_radius_y_radius_z_f32_f32_f32_f32_f32_f32(tmp171, tmp173, local_z, tmp174, tmp175, tmp176);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp177);
    float tmp178 = 0.180000007152557373046875;
    float tmp179 = left1_4331_43right_f32_f32(local_x, tmp178);
    float tmp180 = 0.4799999892711639404296875;
    float tmp181 = left1_4351_43right_f32_f32(local_y, tmp180);
    float tmp182 = 0.23999999463558197021484375;
    float tmp183 = 0.3499999940395355224609375;
    float tmp184 = 0.180000007152557373046875;
    float tmp185 = ellipsoid_distance_at_x_y_z_with_radii_radius_x_radius_y_radius_z_f32_f32_f32_f32_f32_f32(tmp179, tmp181, local_z, tmp182, tmp183, tmp184);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp185);
    float tmp186 = 0.0199999995529651641845703125;
    float tmp187 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp186);
    float tmp188 = 0.589999973773956298828125;
    float tmp189 = 0.10999999940395355224609375;
    float tmp190 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp189);
    float tmp191 = 0.37000000476837158203125;
    float tmp192 = 0.300000011920928955078125;
    float tmp193 = 0.1500000059604644775390625;
    float tmp194 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp193);
    float tmp195 = 0.07500000298023223876953125;
    float tmp196 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp187, tmp188, tmp190, tmp191, tmp192, tmp194, tmp195);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp196);
    float tmp197 = 0.0199999995529651641845703125;
    float tmp198 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp197);
    float tmp199 = 0.589999973773956298828125;
    float tmp200 = 0.10999999940395355224609375;
    float tmp201 = 0.37000000476837158203125;
    float tmp202 = 0.300000011920928955078125;
    float tmp203 = 0.1500000059604644775390625;
    float tmp204 = 0.07500000298023223876953125;
    float tmp205 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp198, tmp199, tmp200, tmp201, tmp202, tmp203, tmp204);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp205);
    float tmp206 = 0.37000000476837158203125;
    float tmp207 = 0.300000011920928955078125;
    float tmp208 = 0.1500000059604644775390625;
    float tmp209 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp208);
    float tmp210 = 0.730000019073486328125;
    float tmp211 = 0.2700000107288360595703125;
    float tmp212 = 0.2199999988079071044921875;
    float tmp213 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp212);
    float tmp214 = 0.054999999701976776123046875;
    float tmp215 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp206, tmp207, tmp209, tmp210, tmp211, tmp213, tmp214);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp215);
    float tmp216 = 0.37000000476837158203125;
    float tmp217 = 0.300000011920928955078125;
    float tmp218 = 0.1500000059604644775390625;
    float tmp219 = 0.730000019073486328125;
    float tmp220 = 0.2700000107288360595703125;
    float tmp221 = 0.2199999988079071044921875;
    float tmp222 = 0.054999999701976776123046875;
    float tmp223 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp216, tmp217, tmp218, tmp219, tmp220, tmp221, tmp222);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp223);
    float tmp224 = 0.14000000059604644775390625;
    float tmp225 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp224);
    float tmp226 = 0.23999999463558197021484375;
    float tmp227 = 0.100000001490116119384765625;
    float tmp228 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp227);
    float tmp229 = 0.4000000059604644775390625;
    float tmp230 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp229);
    float tmp231 = 0.180000007152557373046875;
    float tmp232 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp231);
    float tmp233 = 0.12999999523162841796875;
    float tmp234 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp233);
    float tmp235 = 0.0949999988079071044921875;
    float tmp236 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp225, tmp226, tmp228, tmp230, tmp232, tmp234, tmp235);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp236);
    float tmp237 = 0.14000000059604644775390625;
    float tmp238 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp237);
    float tmp239 = 0.23999999463558197021484375;
    float tmp240 = 0.100000001490116119384765625;
    float tmp241 = 0.4000000059604644775390625;
    float tmp242 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp241);
    float tmp243 = 0.180000007152557373046875;
    float tmp244 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp243);
    float tmp245 = 0.12999999523162841796875;
    float tmp246 = 0.0949999988079071044921875;
    float tmp247 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp238, tmp239, tmp240, tmp242, tmp244, tmp245, tmp246);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp247);
    float tmp248 = 0.4000000059604644775390625;
    float tmp249 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp248);
    float tmp250 = 0.180000007152557373046875;
    float tmp251 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp250);
    float tmp252 = 0.12999999523162841796875;
    float tmp253 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp252);
    float tmp254 = 0.62000000476837158203125;
    float tmp255 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp254);
    float tmp256 = 0.3400000035762786865234375;
    float tmp257 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp256);
    float tmp258 = 0.100000001490116119384765625;
    float tmp259 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp258);
    float tmp260 = 0.07500000298023223876953125;
    float tmp261 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp249, tmp251, tmp253, tmp255, tmp257, tmp259, tmp260);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp261);
    float tmp262 = 0.4000000059604644775390625;
    float tmp263 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp262);
    float tmp264 = 0.180000007152557373046875;
    float tmp265 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp264);
    float tmp266 = 0.12999999523162841796875;
    float tmp267 = 0.62000000476837158203125;
    float tmp268 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp267);
    float tmp269 = 0.3400000035762786865234375;
    float tmp270 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp269);
    float tmp271 = 0.100000001490116119384765625;
    float tmp272 = 0.07500000298023223876953125;
    float tmp273 = distance_from_three_dimensional_point_x_y_z_to_capsule_ax_ay_az_bx_by_bz_with_radius_f32_f32_f32_f32_f32_f32_f32_f32_f32_f32(local_x, local_y, local_z, tmp263, tmp265, tmp266, tmp268, tmp270, tmp271, tmp272);
    model_distance = _the43_minimum_of_a_and_b_f32_f32(model_distance, tmp273);
    return model_distance;
}

float crystal_distance_at_x_y_z_with_yaw_pitch_f32_f32_f32_f32_f32(float x, float y, float z, float yaw, float pitch)
{
    float yaw_sine = _the43_sine_of_value_f32(yaw);
    float yaw_cosine = _the43_cosine_of_value_f32(yaw);
    float tmp = left1_4321_43right_f32_f32(x, yaw_cosine);
    float tmp1 = left1_4321_43right_f32_f32(z, yaw_sine);
    float turned_x = left1_4331_43right_f32_f32(tmp, tmp1);
    float tmp2 = left1_4321_43right_f32_f32(z, yaw_cosine);
    float tmp3 = left1_4321_43right_f32_f32(x, yaw_sine);
    float turned_z = left1_4351_43right_f32_f32(tmp2, tmp3);
    float pitch_sine = _the43_sine_of_value_f32(pitch);
    float pitch_cosine = _the43_cosine_of_value_f32(pitch);
    float tmp4 = left1_4321_43right_f32_f32(y, pitch_cosine);
    float tmp5 = left1_4321_43right_f32_f32(turned_z, pitch_sine);
    float turned_y = left1_4351_43right_f32_f32(tmp4, tmp5);
    float tmp6 = left1_4321_43right_f32_f32(turned_z, pitch_cosine);
    float tmp7 = left1_4321_43right_f32_f32(y, pitch_sine);
    turned_z = left1_4331_43right_f32_f32(tmp6, tmp7);
    float tmp8 = _the43_absolute_value_of_magnitude_f32(turned_x);
    float tmp9 = _the43_absolute_value_of_magnitude_f32(turned_y);
    float tmp10 = left1_4331_43right_f32_f32(tmp8, tmp9);
    float tmp11 = _the43_absolute_value_of_magnitude_f32(turned_z);
    float tmp12 = left1_4331_43right_f32_f32(tmp10, tmp11);
    float tmp13 = 1.08000004291534423828125;
    float tmp14 = left1_4351_43right_f32_f32(tmp12, tmp13);
    float tmp15 = 0.57700002193450927734375;
    float octahedron = left1_4321_43right_f32_f32(tmp14, tmp15);
    float tmp16 = 0.819999992847442626953125;
    float tmp17 = 1.13999998569488525390625;
    float tmp18 = 0.819999992847442626953125;
    float cut_core = ellipsoid_distance_at_x_y_z_with_radii_radius_x_radius_y_radius_z_f32_f32_f32_f32_f32_f32(turned_x, turned_y, turned_z, tmp16, tmp17, tmp18);
    return _the43_maximum_of_a_and_b_f32_f32(octahedron, cut_core);
}

uint left1_4331_43right_i32_i32(uint left, uint right)
{
    return left + right;
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
    float tmp18 = 11.0;
    float tmp19 = left1_4371_43right_f32_f32(time, tmp18);
    float tmp20 = fractional_part_of_number_f32(tmp19);
    float tmp21 = 11.0;
    float moment = left1_4321_43right_f32_f32(tmp20, tmp21);
    float tmp22 = 0.5;
    if (left_2_right_f32_f32(render_pass, tmp22))
    {
        float tmp23 = 19.0;
        float tmp24 = left1_4321_43right_f32_f32(screen_x, tmp23);
        float tmp25 = 13.0;
        float tmp26 = left1_4321_43right_f32_f32(screen_y, tmp25);
        float tmp27 = 4.19999980926513671875;
        float tmp28 = left1_4321_43right_f32_f32(time, tmp27);
        float tmp29 = left1_4331_43right_f32_f32(tmp26, tmp28);
        float tmp30 = left1_4351_43right_f32_f32(tmp24, tmp29);
        float tmp31 = _the43_sine_of_value_f32(tmp30);
        float tmp32 = 0.5;
        float tmp33 = left1_4321_43right_f32_f32(tmp31, tmp32);
        float tmp34 = 0.5;
        float point_wave = left1_4331_43right_f32_f32(tmp33, tmp34);
        float tmp35 = 47.0;
        float tmp36 = left1_4321_43right_f32_f32(screen_y, tmp35);
        float tmp37 = 6.80000019073486328125;
        float tmp38 = left1_4321_43right_f32_f32(time, tmp37);
        float tmp39 = left1_4351_43right_f32_f32(tmp36, tmp38);
        float tmp40 = _the43_sine_of_value_f32(tmp39);
        float tmp41 = 0.5;
        float tmp42 = left1_4321_43right_f32_f32(tmp40, tmp41);
        float tmp43 = 0.5;
        float scan_wave = left1_4331_43right_f32_f32(tmp42, tmp43);
        float tmp44 = 0.0350000001490116119384765625;
        float tmp45 = 0.14000000059604644775390625;
        float tmp46 = left1_4321_43right_f32_f32(point_wave, tmp45);
        float tmp47 = 0.14000000059604644775390625;
        float tmp48 = 0.119999997317790985107421875;
        float tmp49 = left1_4321_43right_f32_f32(scan_wave, tmp48);
        float tmp50 = 0.3400000035762786865234375;
        float tmp51 = 0.20000000298023223876953125;
        float tmp52 = left1_4321_43right_f32_f32(point_wave, tmp51);
        vec4 _962 = vec4(0.0, 0.0, 0.0, 1.0);
        _962.z = left1_4331_43right_f32_f32(tmp50, tmp52);
        _962.y = left1_4331_43right_f32_f32(tmp47, tmp49);
        _962.x = left1_4331_43right_f32_f32(tmp44, tmp46);
        dynlexColor = _962;
    }
    else
    {
        float camera_x = 0.0;
        float camera_y = 0.0199999995529651641845703125;
        float tmp53 = 4.19999980926513671875;
        float camera_z = _the_431negative_1of_34opposite_1of_3453value_f32(tmp53);
        float camera_ray_x = screen_x;
        float camera_ray_y = screen_y;
        float camera_ray_z = 1.7200000286102294921875;
        float tmp54 = left1_4321_43right_f32_f32(camera_ray_x, camera_ray_x);
        float tmp55 = left1_4321_43right_f32_f32(camera_ray_y, camera_ray_y);
        float tmp56 = left1_4331_43right_f32_f32(tmp54, tmp55);
        float tmp57 = left1_4321_43right_f32_f32(camera_ray_z, camera_ray_z);
        float tmp58 = left1_4331_43right_f32_f32(tmp56, tmp57);
        float ray_length = _the43_square_root_of_value_f32(tmp58);
        camera_ray_x = left1_4371_43right_f32_f32(camera_ray_x, ray_length);
        camera_ray_y = left1_4371_43right_f32_f32(camera_ray_y, ray_length);
        camera_ray_z = left1_4371_43right_f32_f32(camera_ray_z, ray_length);
        float tmp59 = 0.0;
        float tmp60 = 3.650000095367431640625;
        float crystal_visibility = scene_window_from_opening_to_closing_at_moment_f32_f32_f32(tmp59, tmp60, moment);
        float tmp61 = 3.349999904632568359375;
        float tmp62 = 7.55000019073486328125;
        float motorcycle_visibility = scene_window_from_opening_to_closing_at_moment_f32_f32_f32(tmp61, tmp62, moment);
        float tmp63 = 7.179999828338623046875;
        float tmp64 = 11.0;
        float vitruvian_visibility = scene_window_from_opening_to_closing_at_moment_f32_f32_f32(tmp63, tmp64, moment);
        float tmp65 = 3.25;
        float tmp66 = 7.349999904632568359375;
        float motorcycle_progress = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp65, tmp66, moment);
        float tmp67 = 0.0;
        float tmp68 = 1.17999994754791259765625;
        float tmp69 = left1_4351_43right_f32_f32(tmp67, tmp68);
        float tmp70 = 0.07999999821186065673828125;
        float tmp71 = left1_4321_43right_f32_f32(motorcycle_progress, tmp70);
        float motorcycle_yaw = left1_4331_43right_f32_f32(tmp69, tmp71);
        float tmp72 = 9.3999996185302734375;
        float wheel_spin = left1_4321_43right_f32_f32(time, tmp72);
        float tmp73 = 2.2000000476837158203125;
        float tmp74 = left1_4321_43right_f32_f32(screen_x, tmp73);
        float tmp75 = 0.02099999971687793731689453125;
        float tmp76 = left1_4321_43right_f32_f32(time, tmp75);
        float tmp77 = left1_4331_43right_f32_f32(tmp74, tmp76);
        float tmp78 = 2.2000000476837158203125;
        float tmp79 = left1_4321_43right_f32_f32(screen_y, tmp78);
        float tmp80 = 0.01400000043213367462158203125;
        float tmp81 = left1_4321_43right_f32_f32(time, tmp80);
        float tmp82 = left1_4351_43right_f32_f32(tmp79, tmp81);
        float tmp83 = 1.89999997615814208984375;
        float chamber_one = flowing_field_at_x_y_phase_f32_f32_f32(tmp77, tmp82, tmp83);
        float tmp84 = 4.099999904632568359375;
        float tmp85 = left1_4321_43right_f32_f32(screen_x, tmp84);
        float tmp86 = 0.0170000009238719940185546875;
        float tmp87 = left1_4321_43right_f32_f32(time, tmp86);
        float tmp88 = left1_4351_43right_f32_f32(tmp85, tmp87);
        float tmp89 = 4.099999904632568359375;
        float tmp90 = left1_4321_43right_f32_f32(screen_y, tmp89);
        float tmp91 = 0.010999999940395355224609375;
        float tmp92 = left1_4321_43right_f32_f32(time, tmp91);
        float tmp93 = left1_4331_43right_f32_f32(tmp90, tmp92);
        float tmp94 = 5.400000095367431640625;
        float chamber_two = ridged_field_at_x_y_phase_f32_f32_f32(tmp88, tmp93, tmp94);
        float tmp95 = 27.0;
        float tmp96 = left1_4321_43right_f32_f32(screen_x, tmp95);
        float tmp97 = 0.23999999463558197021484375;
        float tmp98 = left1_4321_43right_f32_f32(time, tmp97);
        float tmp99 = left1_4331_43right_f32_f32(tmp96, tmp98);
        float tmp100 = 27.0;
        float tmp101 = left1_4321_43right_f32_f32(screen_y, tmp100);
        float tmp102 = 0.12999999523162841796875;
        float tmp103 = left1_4321_43right_f32_f32(time, tmp102);
        float tmp104 = left1_4351_43right_f32_f32(tmp101, tmp103);
        float tmp105 = 9.69999980926513671875;
        float free_drones = spark_field_at_x_y_phase_f32_f32_f32(tmp99, tmp104, tmp105);
        float tmp106 = 7.0;
        float tmp107 = left1_4321_43right_f32_f32(screen_x, tmp106);
        float tmp108 = 8.3999996185302734375;
        float tmp109 = left1_4321_43right_f32_f32(time, tmp108);
        float tmp110 = left1_4331_43right_f32_f32(tmp107, tmp109);
        float tmp111 = 46.0;
        float tmp112 = left1_4321_43right_f32_f32(screen_y, tmp111);
        float tmp113 = 14.19999980926513671875;
        float speed_particles = spark_field_at_x_y_phase_f32_f32_f32(tmp110, tmp112, tmp113);
        float tmp114 = 0.0;
        float tmp115 = 0.660000026226043701171875;
        float tmp116 = _the43_absolute_value_of_magnitude_f32(screen_y);
        float speed_core = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp114, tmp115, tmp116);
        float tmp117 = left1_4321_43right_f32_f32(speed_particles, speed_core);
        float speed_trails = left1_4321_43right_f32_f32(tmp117, motorcycle_visibility);
        float tmp118 = 0.0030000000260770320892333984375;
        float tmp119 = 0.01200000010430812835693359375;
        float tmp120 = left1_4321_43right_f32_f32(chamber_one, tmp119);
        float tmp121 = left1_4331_43right_f32_f32(tmp118, tmp120);
        float tmp122 = 0.017999999225139617919921875;
        float tmp123 = left1_4321_43right_f32_f32(chamber_two, tmp122);
        float tmp124 = left1_4331_43right_f32_f32(tmp121, tmp123);
        float tmp125 = 0.180000007152557373046875;
        float tmp126 = left1_4321_43right_f32_f32(free_drones, tmp125);
        float tmp127 = left1_4331_43right_f32_f32(tmp124, tmp126);
        float tmp128 = 0.319999992847442626953125;
        float tmp129 = left1_4321_43right_f32_f32(speed_trails, tmp128);
        float red = left1_4331_43right_f32_f32(tmp127, tmp129);
        float tmp130 = 0.006000000052154064178466796875;
        float tmp131 = 0.0240000002086162567138671875;
        float tmp132 = left1_4321_43right_f32_f32(chamber_one, tmp131);
        float tmp133 = left1_4331_43right_f32_f32(tmp130, tmp132);
        float tmp134 = 0.014999999664723873138427734375;
        float tmp135 = left1_4321_43right_f32_f32(chamber_two, tmp134);
        float tmp136 = left1_4331_43right_f32_f32(tmp133, tmp135);
        float tmp137 = 0.4600000083446502685546875;
        float tmp138 = left1_4321_43right_f32_f32(free_drones, tmp137);
        float tmp139 = left1_4331_43right_f32_f32(tmp136, tmp138);
        float tmp140 = 0.7799999713897705078125;
        float tmp141 = left1_4321_43right_f32_f32(speed_trails, tmp140);
        float green = left1_4331_43right_f32_f32(tmp139, tmp141);
        float tmp142 = 0.0240000002086162567138671875;
        float tmp143 = 0.08200000226497650146484375;
        float tmp144 = left1_4321_43right_f32_f32(chamber_one, tmp143);
        float tmp145 = left1_4331_43right_f32_f32(tmp142, tmp144);
        float tmp146 = 0.071000002324581146240234375;
        float tmp147 = left1_4321_43right_f32_f32(chamber_two, tmp146);
        float tmp148 = left1_4331_43right_f32_f32(tmp145, tmp147);
        float tmp149 = 1.019999980926513671875;
        float tmp150 = left1_4321_43right_f32_f32(free_drones, tmp149);
        float tmp151 = left1_4331_43right_f32_f32(tmp148, tmp150);
        float tmp152 = 1.46000003814697265625;
        float tmp153 = left1_4321_43right_f32_f32(speed_trails, tmp152);
        float blue = left1_4331_43right_f32_f32(tmp151, tmp153);
        float bound_center_x = 0.0;
        float bound_center_y = 0.0;
        float bound_center_z = 0.819999992847442626953125;
        float bound_radius = 1.2400000095367431640625;
        float tmp156 = 3.5;
        bool tmp157 = left_1is_greater_than_or_equal_to4213_right_f32_f32(moment, tmp156);
        float tmp158 = 7.400000095367431640625;
        bool tmp159 = left_0_right_f32_f32(moment, tmp158);
        if (_boolean8left5_and_3boolean8right5_bool_bool(tmp157, tmp159))
        {
            float tmp160 = 0.0;
            float tmp161 = 1.2599999904632568359375;
            float tmp162 = left1_4351_43right_f32_f32(tmp160, tmp161);
            float tmp163 = 1.17999994754791259765625;
            float tmp164 = left1_4321_43right_f32_f32(motorcycle_progress, tmp163);
            bound_center_x = left1_4331_43right_f32_f32(tmp162, tmp164);
            float tmp165 = 0.0;
            float tmp166 = 0.189999997615814208984375;
            float tmp167 = left1_4351_43right_f32_f32(tmp165, tmp166);
            float tmp168 = 0.119999997317790985107421875;
            float tmp169 = left1_4321_43right_f32_f32(motorcycle_progress, tmp168);
            bound_center_y = left1_4331_43right_f32_f32(tmp167, tmp169);
            float tmp170 = 2.7999999523162841796875;
            float tmp171 = 4.900000095367431640625;
            float tmp172 = left1_4321_43right_f32_f32(motorcycle_progress, tmp171);
            bound_center_z = left1_4351_43right_f32_f32(tmp170, tmp172);
            bound_radius = 1.36000001430511474609375;
        }
        float center_vector_x = left1_4351_43right_f32_f32(bound_center_x, camera_x);
        float center_vector_y = left1_4351_43right_f32_f32(bound_center_y, camera_y);
        float center_vector_z = left1_4351_43right_f32_f32(bound_center_z, camera_z);
        float tmp173 = left1_4321_43right_f32_f32(center_vector_x, camera_ray_x);
        float tmp174 = left1_4321_43right_f32_f32(center_vector_y, camera_ray_y);
        float tmp175 = left1_4331_43right_f32_f32(tmp173, tmp174);
        float tmp176 = left1_4321_43right_f32_f32(center_vector_z, camera_ray_z);
        float center_depth = left1_4331_43right_f32_f32(tmp175, tmp176);
        float tmp177 = left1_4321_43right_f32_f32(camera_ray_x, center_depth);
        float nearest_x = left1_4331_43right_f32_f32(camera_x, tmp177);
        float tmp178 = left1_4321_43right_f32_f32(camera_ray_y, center_depth);
        float nearest_y = left1_4331_43right_f32_f32(camera_y, tmp178);
        float tmp179 = left1_4321_43right_f32_f32(camera_ray_z, center_depth);
        float nearest_z = left1_4331_43right_f32_f32(camera_z, tmp179);
        float miss_x = left1_4351_43right_f32_f32(nearest_x, bound_center_x);
        float miss_y = left1_4351_43right_f32_f32(nearest_y, bound_center_y);
        float miss_z = left1_4351_43right_f32_f32(nearest_z, bound_center_z);
        float tmp180 = left1_4321_43right_f32_f32(miss_x, miss_x);
        float tmp181 = left1_4321_43right_f32_f32(miss_y, miss_y);
        float tmp182 = left1_4331_43right_f32_f32(tmp180, tmp181);
        float tmp183 = left1_4321_43right_f32_f32(miss_z, miss_z);
        float miss_squared = left1_4331_43right_f32_f32(tmp182, tmp183);
        float bound_squared = left1_4321_43right_f32_f32(bound_radius, bound_radius);
        float ray_depth = 0.07999999821186065673828125;
        float ray_end = 0.07999999821186065673828125;
        uint ray_step = 0u;
        uint tmp184 = 0u;
        float tmp187 = 7.400000095367431640625;
        bool tmp188 = left_1is_greater_than_or_equal_to4213_right_f32_f32(moment, tmp187);
        float tmp189 = 0.0;
        bool tmp190 = left_1is_less_than_or_equal_to4013_right_f32_f32(center_depth, tmp189);
        bool tmp191 = left_or_right_bool_bool(tmp188, tmp190);
        bool tmp192 = left_1is_greater_than_or_equal_to4213_right_f32_f32(miss_squared, bound_squared);
        bool _809 = false;
        if (left_or_right_bool_bool(tmp191, tmp192))
        {
            uint tmp193 = 1u;
            _809 = value_as_destinationtype_i32_type_ct_destinationtype_type(tmp193);
        }
        else
        {
            float tmp195 = left1_4351_43right_f32_f32(bound_squared, miss_squared);
            float bound_half_span = _the43_square_root_of_value_f32(tmp195);
            float tmp196 = left1_4351_43right_f32_f32(center_depth, bound_half_span);
            float tmp197 = 0.07999999821186065673828125;
            ray_depth = _the43_maximum_of_a_and_b_f32_f32(tmp196, tmp197);
            ray_end = left1_4331_43right_f32_f32(center_depth, bound_half_span);
            _809 = value_as_destinationtype_i32_type_ct_destinationtype_type(tmp184);
        }
        uint tmp198 = 0u;
        float surface_x = camera_x;
        float surface_y = camera_y;
        float surface_z = camera_z;
        float hologram_glow = 0.0;
        bool _814 = false;
        bool _816 = false;
        _814 = _809;
        _816 = value_as_destinationtype_i32_type_ct_destinationtype_type(tmp198);
        uint tmp238 = 0u;
        bool tmp201 = false;
        bool tmp200 = false;
        uint tmp199 = 0u;
        bool _815 = false;
        bool _817 = false;
        for (;;)
        {
            tmp199 = 44u;
            tmp200 = left_0_right_i32_i32(ray_step, tmp199);
            tmp201 = _814 != true;
            if (_boolean8left5_and_3boolean8right5_bool_bool(tmp200, tmp201))
            {
                float tmp202 = left1_4321_43right_f32_f32(camera_ray_x, ray_depth);
                float sample_x = left1_4331_43right_f32_f32(camera_x, tmp202);
                float tmp203 = left1_4321_43right_f32_f32(camera_ray_y, ray_depth);
                float sample_y = left1_4331_43right_f32_f32(camera_y, tmp203);
                float tmp204 = left1_4321_43right_f32_f32(camera_ray_z, ray_depth);
                float sample_z = left1_4331_43right_f32_f32(camera_z, tmp204);
                float scene_distance = 12.0;
                float tmp207 = 3.5;
                if (left_0_right_f32_f32(moment, tmp207))
                {
                    float tmp208 = 0.7200000286102294921875;
                    float crystal_yaw = left1_4321_43right_f32_f32(time, tmp208);
                    float tmp209 = 0.4699999988079071044921875;
                    float tmp210 = left1_4321_43right_f32_f32(time, tmp209);
                    float tmp211 = _the43_sine_of_value_f32(tmp210);
                    float tmp212 = 0.519999980926513671875;
                    float crystal_pitch = left1_4321_43right_f32_f32(tmp211, tmp212);
                    float tmp213 = 0.819999992847442626953125;
                    float tmp214 = left1_4351_43right_f32_f32(sample_z, tmp213);
                    scene_distance = crystal_distance_at_x_y_z_with_yaw_pitch_f32_f32_f32_f32_f32(sample_x, sample_y, tmp214, crystal_yaw, crystal_pitch);
                }
                else
                {
                    scene_distance = motorcycle_distance_at_x_y_z_with_progress_wheel_spin_motorcycle_yaw_f32_f32_f32_f32_f32_f32(sample_x, sample_y, sample_z, motorcycle_progress, wheel_spin, motorcycle_yaw);
                }
                float absolute_distance = _the43_absolute_value_of_magnitude_f32(scene_distance);
                float tmp216 = 0.01200000010430812835693359375;
                float tmp217 = 0.115000002086162567138671875;
                float proximity = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp216, tmp217, absolute_distance);
                float tmp218 = 1.0;
                float tmp219 = 0.3499999940395355224609375;
                float tmp220 = left1_4351_43right_f32_f32(ray_end, tmp219);
                float tmp221 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp220, ray_end, ray_depth);
                float depth_fade = left1_4351_43right_f32_f32(tmp218, tmp221);
                float tmp222 = left1_4321_43right_f32_f32(proximity, depth_fade);
                float tmp223 = 0.0035000001080334186553955078125;
                float tmp224 = left1_4321_43right_f32_f32(tmp222, tmp223);
                hologram_glow = left1_4331_43right_f32_f32(hologram_glow, tmp224);
                float tmp227 = 0.0089999996125698089599609375;
                if (left_0_right_f32_f32(absolute_distance, tmp227))
                {
                    uint tmp228 = 1u;
                    surface_x = sample_x;
                    surface_y = sample_y;
                    surface_z = sample_z;
                    uint tmp229 = 1u;
                    _815 = value_as_destinationtype_i32_type_ct_destinationtype_type(tmp229);
                    _817 = value_as_destinationtype_i32_type_ct_destinationtype_type(tmp228);
                }
                else
                {
                    float tmp231 = 0.63999998569488525390625;
                    float tmp232 = left1_4321_43right_f32_f32(absolute_distance, tmp231);
                    float tmp233 = 0.01400000043213367462158203125;
                    float tmp234 = _the43_maximum_of_a_and_b_f32_f32(tmp232, tmp233);
                    ray_depth = left1_4331_43right_f32_f32(ray_depth, tmp234);
                    bool _935 = false;
                    if (left_2_right_f32_f32(ray_depth, ray_end))
                    {
                        uint tmp237 = 1u;
                        _935 = value_as_destinationtype_i32_type_ct_destinationtype_type(tmp237);
                    }
                    else
                    {
                        _935 = _814;
                    }
                    _815 = _935;
                    _817 = _816;
                }
                tmp238 = 1u;
                ray_step = left1_4331_43right_i32_i32(ray_step, tmp238);
                _814 = _815;
                _816 = _817;
                continue;
            }
            else
            {
                break;
            }
        }
        float surface_light = 0.0;
        if (_816)
        {
            float tmp241 = 1.7000000476837158203125;
            float tmp242 = left1_4321_43right_f32_f32(time, tmp241);
            float surface_drones = nano_drone_field_at_x_y_z_phase_f32_f32_f32_f32(surface_x, surface_y, surface_z, tmp242);
            float tmp243 = 13.0;
            float tmp244 = left1_4321_43right_f32_f32(surface_z, tmp243);
            float tmp245 = 4.19999980926513671875;
            float tmp246 = left1_4321_43right_f32_f32(time, tmp245);
            float tmp247 = left1_4351_43right_f32_f32(tmp244, tmp246);
            float tmp248 = _the43_sine_of_value_f32(tmp247);
            float tmp249 = 0.5;
            float tmp250 = left1_4321_43right_f32_f32(tmp248, tmp249);
            float tmp251 = 0.5;
            float depth_pulse = left1_4331_43right_f32_f32(tmp250, tmp251);
            float tmp252 = 0.0;
            float tmp253 = 0.04500000178813934326171875;
            float tmp254 = 2.7000000476837158203125;
            float tmp255 = left1_4321_43right_f32_f32(surface_y, tmp254);
            float tmp256 = 0.310000002384185791015625;
            float tmp257 = left1_4321_43right_f32_f32(time, tmp256);
            float tmp258 = left1_4351_43right_f32_f32(tmp255, tmp257);
            float tmp259 = fractional_part_of_number_f32(tmp258);
            float tmp260 = 0.5;
            float tmp261 = left1_4351_43right_f32_f32(tmp259, tmp260);
            float tmp262 = _the43_absolute_value_of_magnitude_f32(tmp261);
            float scan_plane = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp252, tmp253, tmp262);
            float tmp263 = 1.82000005245208740234375;
            float tmp264 = 1.059999942779541015625;
            float tmp265 = left1_4321_43right_f32_f32(depth_pulse, tmp264);
            float tmp266 = left1_4331_43right_f32_f32(tmp263, tmp265);
            float tmp267 = left1_4321_43right_f32_f32(surface_drones, tmp266);
            float tmp268 = 0.0199999995529651641845703125;
            float tmp269 = left1_4321_43right_f32_f32(scan_plane, tmp268);
            surface_light = left1_4331_43right_f32_f32(tmp267, tmp269);
        }
        float tmp270 = 1.7999999523162841796875;
        float tmp271 = left1_4321_43right_f32_f32(hologram_glow, tmp270);
        float tmp272 = left1_4331_43right_f32_f32(surface_light, tmp271);
        float crystal_light = left1_4321_43right_f32_f32(tmp272, crystal_visibility);
        float tmp273 = 2.099999904632568359375;
        float tmp274 = left1_4321_43right_f32_f32(hologram_glow, tmp273);
        float tmp275 = left1_4331_43right_f32_f32(surface_light, tmp274);
        float motorcycle_light = left1_4321_43right_f32_f32(tmp275, motorcycle_visibility);
        float tmp276 = 5.30000019073486328125;
        float tmp277 = left1_4321_43right_f32_f32(surface_x, tmp276);
        float tmp278 = 3.7000000476837158203125;
        float tmp279 = left1_4321_43right_f32_f32(surface_y, tmp278);
        float tmp280 = 4.099999904632568359375;
        float tmp281 = left1_4321_43right_f32_f32(surface_z, tmp280);
        float tmp282 = 0.699999988079071044921875;
        float tmp283 = left1_4321_43right_f32_f32(time, tmp282);
        float tmp284 = left1_4351_43right_f32_f32(tmp281, tmp283);
        float tmp285 = left1_4331_43right_f32_f32(tmp279, tmp284);
        float tmp286 = left1_4331_43right_f32_f32(tmp277, tmp285);
        float tmp287 = _the43_sine_of_value_f32(tmp286);
        float tmp288 = 0.5;
        float tmp289 = left1_4321_43right_f32_f32(tmp287, tmp288);
        float tmp290 = 0.5;
        float drone_hue = left1_4331_43right_f32_f32(tmp289, tmp290);
        float tmp291 = 0.36000001430511474609375;
        float tmp292 = left1_4321_43right_f32_f32(crystal_light, tmp291);
        float tmp293 = left1_4331_43right_f32_f32(red, tmp292);
        float tmp294 = 0.7200000286102294921875;
        float tmp295 = 0.959999978542327880859375;
        float tmp296 = left1_4321_43right_f32_f32(drone_hue, tmp295);
        float tmp297 = left1_4331_43right_f32_f32(tmp294, tmp296);
        float tmp298 = left1_4321_43right_f32_f32(motorcycle_light, tmp297);
        red = left1_4331_43right_f32_f32(tmp293, tmp298);
        float tmp299 = 0.7799999713897705078125;
        float tmp300 = left1_4321_43right_f32_f32(crystal_light, tmp299);
        float tmp301 = left1_4331_43right_f32_f32(green, tmp300);
        float tmp302 = 0.920000016689300537109375;
        float tmp303 = 0.579999983310699462890625;
        float tmp304 = left1_4321_43right_f32_f32(drone_hue, tmp303);
        float tmp305 = left1_4351_43right_f32_f32(tmp302, tmp304);
        float tmp306 = left1_4321_43right_f32_f32(motorcycle_light, tmp305);
        green = left1_4331_43right_f32_f32(tmp301, tmp306);
        float tmp307 = 1.46000003814697265625;
        float tmp308 = left1_4321_43right_f32_f32(crystal_light, tmp307);
        float tmp309 = left1_4331_43right_f32_f32(blue, tmp308);
        float tmp310 = 1.17999994754791259765625;
        float tmp311 = 0.2800000011920928955078125;
        float tmp312 = left1_4321_43right_f32_f32(drone_hue, tmp311);
        float tmp313 = left1_4351_43right_f32_f32(tmp310, tmp312);
        float tmp314 = left1_4321_43right_f32_f32(motorcycle_light, tmp313);
        blue = left1_4331_43right_f32_f32(tmp309, tmp314);
        float tmp315 = 0.2800000011920928955078125;
        float tmp316 = 1.2599999904632568359375;
        float figure_aura = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp315, tmp316, radial);
        float tmp317 = left1_4321_43right_f32_f32(figure_aura, vitruvian_visibility);
        float tmp318 = 0.017999999225139617919921875;
        float tmp319 = left1_4321_43right_f32_f32(tmp317, tmp318);
        red = left1_4331_43right_f32_f32(red, tmp319);
        float tmp320 = left1_4321_43right_f32_f32(figure_aura, vitruvian_visibility);
        float tmp321 = 0.05200000107288360595703125;
        float tmp322 = left1_4321_43right_f32_f32(tmp320, tmp321);
        green = left1_4331_43right_f32_f32(green, tmp322);
        float tmp323 = left1_4321_43right_f32_f32(figure_aura, vitruvian_visibility);
        float tmp324 = 0.119999997317790985107421875;
        float tmp325 = left1_4321_43right_f32_f32(tmp323, tmp324);
        blue = left1_4331_43right_f32_f32(blue, tmp325);
        float tmp326 = 0.180000007152557373046875;
        float tmp327 = 1.0;
        float tmp328 = 0.519999980926513671875;
        float tmp329 = 1.62000000476837158203125;
        float tmp330 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp328, tmp329, radial);
        float tmp331 = left1_4351_43right_f32_f32(tmp327, tmp330);
        float tmp332 = 0.819999992847442626953125;
        float tmp333 = left1_4321_43right_f32_f32(tmp331, tmp332);
        float vignette = left1_4331_43right_f32_f32(tmp326, tmp333);
        float tmp334 = left1_4321_43right_f32_f32(red, vignette);
        float tmp335 = 1.0;
        float tmp336 = left1_4331_43right_f32_f32(tmp335, red);
        float tmp337 = left1_4371_43right_f32_f32(tmp334, tmp336);
        red = _the43_square_root_of_value_f32(tmp337);
        float tmp338 = left1_4321_43right_f32_f32(green, vignette);
        float tmp339 = 1.0;
        float tmp340 = left1_4331_43right_f32_f32(tmp339, green);
        float tmp341 = left1_4371_43right_f32_f32(tmp338, tmp340);
        green = _the43_square_root_of_value_f32(tmp341);
        float tmp342 = left1_4321_43right_f32_f32(blue, vignette);
        float tmp343 = 1.0;
        float tmp344 = left1_4331_43right_f32_f32(tmp343, blue);
        float tmp345 = left1_4371_43right_f32_f32(tmp342, tmp344);
        blue = _the43_square_root_of_value_f32(tmp345);
        vec4 _904 = vec4(0.0, 0.0, 0.0, 1.0);
        _904.z = blue;
        _904.y = green;
        _904.x = red;
        dynlexColor = _904;
    }
}
