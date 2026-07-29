#version 300 es
precision highp float;
precision highp int;

struct class_8
{
    float _m0;
    float _m1;
    float _m2;
};

struct class_5
{
    bool _m0;
    class_8 _m1;
    float _m2;
};

struct class_4
{
    float _m0;
    float _m1;
    uint _m2;
    bool _m3;
};

struct class_3
{
    class_8 _m0;
    float _m1;
};

struct class_6
{
    float _m0;
    float _m1;
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

layout(location = 0) out vec4 dynlexColor;

float the_maximum_of_left_and_right_f32_f32(float left, float right)
{
    return isnan(right) ? left : (isnan(left) ? right : max(left, right));
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

float the_square_root_of_value_f32(float value)
{
    return sqrt(value);
}

float the_floor_of_value_f32(float value)
{
    return floor(value);
}

float the_fractional_part_of_number_f32(float number)
{
    float tmp = the_floor_of_value_f32(number);
    return left1_4351_43right_f32_f32(number, tmp);
}

bool left_2_right_f32_f32(float left, float right)
{
    return left > right;
}

float _the_negative_of_4the_opposite_of_453value_f32(float value)
{
    return -value;
}

bool left_0_right_f32_f32(float left, float right)
{
    return left < right;
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

float the_sine_of_value_f32(float value)
{
    return sin(value);
}

float the_cosine_of_value_f32(float value)
{
    return cos(value);
}

float the_signed_flow_at_3shader_point8point5_during_3any8phase5_shader_point_f32(class_6 point, float phase)
{
    float tmp = 0.730000019073486328125;
    float tmp1 = left1_4321_43right_f32_f32(point._m0, tmp);
    float tmp3 = 0.4099999964237213134765625;
    float tmp4 = left1_4321_43right_f32_f32(point._m1, tmp3);
    float tmp5 = left1_4351_43right_f32_f32(tmp1, tmp4);
    float tmp6 = left1_4331_43right_f32_f32(tmp5, phase);
    float sway = the_sine_of_value_f32(tmp6);
    float tmp8 = 0.37000000476837158203125;
    float tmp9 = left1_4321_43right_f32_f32(point._m0, tmp8);
    float tmp11 = 0.88999998569488525390625;
    float tmp12 = left1_4321_43right_f32_f32(point._m1, tmp11);
    float tmp13 = left1_4331_43right_f32_f32(tmp9, tmp12);
    float tmp14 = 0.709999978542327880859375;
    float tmp15 = left1_4321_43right_f32_f32(phase, tmp14);
    float tmp16 = left1_4351_43right_f32_f32(tmp13, tmp15);
    float drift = the_cosine_of_value_f32(tmp16);
    float tmp18 = 0.579999983310699462890625;
    float tmp19 = left1_4321_43right_f32_f32(sway, tmp18);
    float longitude = left1_4331_43right_f32_f32(point._m0, tmp19);
    float tmp21 = 0.579999983310699462890625;
    float tmp22 = left1_4321_43right_f32_f32(drift, tmp21);
    float latitude = left1_4331_43right_f32_f32(point._m1, tmp22);
    float tmp23 = 1.309999942779541015625;
    float tmp24 = left1_4321_43right_f32_f32(longitude, tmp23);
    float tmp25 = 0.87000000476837158203125;
    float tmp26 = left1_4321_43right_f32_f32(latitude, tmp25);
    float tmp27 = left1_4331_43right_f32_f32(tmp24, tmp26);
    float tmp28 = 0.430000007152557373046875;
    float tmp29 = left1_4321_43right_f32_f32(phase, tmp28);
    float tmp30 = left1_4331_43right_f32_f32(tmp27, tmp29);
    float broad = the_sine_of_value_f32(tmp30);
    float tmp31 = 0.790000021457672119140625;
    float tmp32 = _the_negative_of_4the_opposite_of_453value_f32(tmp31);
    float tmp33 = left1_4321_43right_f32_f32(longitude, tmp32);
    float tmp34 = 1.730000019073486328125;
    float tmp35 = left1_4321_43right_f32_f32(latitude, tmp34);
    float tmp36 = left1_4331_43right_f32_f32(tmp33, tmp35);
    float tmp37 = 0.310000002384185791015625;
    float tmp38 = left1_4321_43right_f32_f32(phase, tmp37);
    float tmp39 = left1_4351_43right_f32_f32(tmp36, tmp38);
    float crossing = the_cosine_of_value_f32(tmp39);
    float tmp40 = 2.4700000286102294921875;
    float tmp41 = left1_4321_43right_f32_f32(longitude, tmp40);
    float tmp42 = 2.1099998950958251953125;
    float tmp43 = left1_4321_43right_f32_f32(latitude, tmp42);
    float tmp44 = left1_4351_43right_f32_f32(tmp41, tmp43);
    float tmp45 = 1.7999999523162841796875;
    float tmp46 = left1_4321_43right_f32_f32(broad, tmp45);
    float tmp47 = left1_4331_43right_f32_f32(tmp44, tmp46);
    float curl = the_sine_of_value_f32(tmp47);
    float tmp48 = 4.030000209808349609375;
    float tmp49 = left1_4321_43right_f32_f32(longitude, tmp48);
    float tmp50 = 3.1700000762939453125;
    float tmp51 = left1_4321_43right_f32_f32(latitude, tmp50);
    float tmp52 = left1_4331_43right_f32_f32(tmp49, tmp51);
    float tmp53 = 1.39999997615814208984375;
    float tmp54 = left1_4321_43right_f32_f32(crossing, tmp53);
    float tmp55 = left1_4331_43right_f32_f32(tmp52, tmp54);
    float detail = the_cosine_of_value_f32(tmp55);
    float tmp56 = 0.4600000083446502685546875;
    float tmp57 = left1_4321_43right_f32_f32(broad, tmp56);
    float tmp58 = 0.2899999916553497314453125;
    float tmp59 = left1_4321_43right_f32_f32(crossing, tmp58);
    float tmp60 = left1_4331_43right_f32_f32(tmp57, tmp59);
    float tmp61 = 0.17000000178813934326171875;
    float tmp62 = left1_4321_43right_f32_f32(curl, tmp61);
    float tmp63 = 0.07999999821186065673828125;
    float tmp64 = left1_4321_43right_f32_f32(detail, tmp63);
    float tmp65 = left1_4331_43right_f32_f32(tmp62, tmp64);
    return left1_4331_43right_f32_f32(tmp60, tmp65);
}

float the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(class_6 point, float phase)
{
    float tmp = the_signed_flow_at_3shader_point8point5_during_3any8phase5_shader_point_f32(point, phase);
    float tmp1 = 0.5;
    float tmp2 = left1_4321_43right_f32_f32(tmp, tmp1);
    float tmp3 = 0.5;
    return left1_4331_43right_f32_f32(tmp2, tmp3);
}

float the_absolute_value_of_magnitude_f32(float magnitude)
{
    return abs(magnitude);
}

float the_minimum_of_left_and_right_f32_f32(float left, float right)
{
    return isnan(right) ? left : (isnan(left) ? right : min(left, right));
}

float the_ridged_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(class_6 point, float phase)
{
    float tmp = the_signed_flow_at_3shader_point8point5_during_3any8phase5_shader_point_f32(point, phase);
    float wave = the_absolute_value_of_magnitude_f32(tmp);
    float tmp1 = 1.0;
    float tmp2 = 1.0;
    float tmp3 = the_minimum_of_left_and_right_f32_f32(wave, tmp2);
    float ridge = left1_4351_43right_f32_f32(tmp1, tmp3);
    return left1_4321_43right_f32_f32(ridge, ridge);
}

float the_glow_from_inner_to_outer_at_sample_f32_f32_f32(float inner, float outer, float _sample)
{
    float tmp = 1.0;
    float tmp1 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(inner, outer, _sample);
    return left1_4351_43right_f32_f32(tmp, tmp1);
}

float the_spark_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(class_6 point, float phase)
{
    float tmp = 0.189999997615814208984375;
    class_6 class_tmp = class_6(0.0, 0.0);
    class_tmp._m0 = left1_4321_43right_f32_f32(point._m0, tmp);
    float tmp3 = 0.189999997615814208984375;
    class_tmp._m1 = left1_4321_43right_f32_f32(point._m1, tmp3);
    class_6 _sample = class_tmp;
    float warp = the_signed_flow_at_3shader_point8point5_during_3any8phase5_shader_point_f32(_sample, phase);
    float tmp7 = 0.23000000417232513427734375;
    float tmp8 = left1_4321_43right_f32_f32(point._m0, tmp7);
    float tmp9 = 7.0;
    class_6 class_tmp5 = class_6(0.0, 0.0);
    class_tmp5._m0 = left1_4331_43right_f32_f32(tmp8, tmp9);
    float tmp12 = 0.23000000417232513427734375;
    float tmp13 = left1_4321_43right_f32_f32(point._m1, tmp12);
    float tmp14 = 5.0;
    class_tmp5._m1 = left1_4351_43right_f32_f32(tmp13, tmp14);
    class_6 shifted = class_tmp5;
    float tmp18 = 1.7000000476837158203125;
    float tmp19 = left1_4321_43right_f32_f32(warp, tmp18);
    float longitude = left1_4331_43right_f32_f32(point._m0, tmp19);
    float tmp21 = 1.7000000476837158203125;
    float tmp22 = left1_4331_43right_f32_f32(phase, tmp21);
    float tmp23 = the_signed_flow_at_3shader_point8point5_during_3any8phase5_shader_point_f32(shifted, tmp22);
    float tmp24 = 1.7000000476837158203125;
    float tmp25 = left1_4321_43right_f32_f32(tmp23, tmp24);
    float latitude = left1_4331_43right_f32_f32(point._m1, tmp25);
    float tmp26 = 1.730000019073486328125;
    float tmp27 = left1_4321_43right_f32_f32(longitude, tmp26);
    float tmp28 = 0.310000002384185791015625;
    float tmp29 = left1_4321_43right_f32_f32(latitude, tmp28);
    float tmp30 = left1_4331_43right_f32_f32(tmp29, phase);
    float tmp31 = left1_4331_43right_f32_f32(tmp27, tmp30);
    float tmp32 = the_sine_of_value_f32(tmp31);
    float primary = the_absolute_value_of_magnitude_f32(tmp32);
    float tmp33 = 0.2700000107288360595703125;
    float tmp34 = _the_negative_of_4the_opposite_of_453value_f32(tmp33);
    float tmp35 = left1_4321_43right_f32_f32(longitude, tmp34);
    float tmp36 = 1.90999996662139892578125;
    float tmp37 = left1_4321_43right_f32_f32(latitude, tmp36);
    float tmp38 = 0.829999983310699462890625;
    float tmp39 = left1_4321_43right_f32_f32(phase, tmp38);
    float tmp40 = left1_4351_43right_f32_f32(tmp37, tmp39);
    float tmp41 = left1_4331_43right_f32_f32(tmp35, tmp40);
    float tmp42 = the_sine_of_value_f32(tmp41);
    float secondary = the_absolute_value_of_magnitude_f32(tmp42);
    float tmp43 = left1_4321_43right_f32_f32(primary, primary);
    float tmp44 = left1_4321_43right_f32_f32(secondary, secondary);
    float tmp45 = left1_4331_43right_f32_f32(tmp43, tmp44);
    float crossing = the_square_root_of_value_f32(tmp45);
    float tmp46 = 0.0;
    float tmp47 = 0.115000002086162567138671875;
    float brightness = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp46, tmp47, crossing);
    float tmp49 = 0.10999999940395355224609375;
    class_6 class_tmp48 = class_6(0.0, 0.0);
    class_tmp48._m0 = left1_4321_43right_f32_f32(longitude, tmp49);
    float tmp51 = 0.10999999940395355224609375;
    class_tmp48._m1 = left1_4321_43right_f32_f32(latitude, tmp51);
    class_6 location = class_tmp48;
    float tmp54 = 4.0;
    float tmp55 = left1_4331_43right_f32_f32(phase, tmp54);
    float rarity = the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(location, tmp55);
    float tmp56 = 0.4799999892711639404296875;
    float tmp57 = 0.819999992847442626953125;
    float tmp58 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp56, tmp57, rarity);
    return left1_4321_43right_f32_f32(brightness, tmp58);
}

bool left_1is_greater_than_or_equal_to4213_right_f32_f32(float left, float right)
{
    return left >= right;
}

bool _boolean8left5_and_3boolean8right5_bool_bool(bool left, bool right)
{
    return left && right;
}

bool value_as_3type8destination5_i32_type_ct_destination_type(uint value)
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

bool _ray_traversal8traversal5_continues_ray_traversal(class_4 traversal)
{
    return traversal._m3;
}

bool _surface_result8surface5_was_hit_surface_result(class_5 surface)
{
    return surface._m0;
}

float the_nano_drone_field_at_3spatial_shader_point8point5_with_phase_3any8phase5_spatial_shader_point_f32(class_8 point, float phase)
{
    float tmp = 43.130001068115234375;
    float tmp1 = left1_4321_43right_f32_f32(point._m0, tmp);
    float tmp3 = 17.70999908447265625;
    float tmp4 = left1_4321_43right_f32_f32(point._m1, tmp3);
    float tmp6 = 7.190000057220458984375;
    float tmp7 = left1_4321_43right_f32_f32(point._m2, tmp6);
    float tmp8 = left1_4331_43right_f32_f32(tmp7, phase);
    float tmp9 = left1_4331_43right_f32_f32(tmp4, tmp8);
    float first = left1_4331_43right_f32_f32(tmp1, tmp9);
    float tmp11 = 13.36999988555908203125;
    float tmp12 = _the_negative_of_4the_opposite_of_453value_f32(tmp11);
    float tmp13 = left1_4321_43right_f32_f32(point._m0, tmp12);
    float tmp15 = 47.06999969482421875;
    float tmp16 = left1_4321_43right_f32_f32(point._m1, tmp15);
    float tmp18 = 21.409999847412109375;
    float tmp19 = left1_4321_43right_f32_f32(point._m2, tmp18);
    float tmp20 = 0.730000019073486328125;
    float tmp21 = left1_4321_43right_f32_f32(phase, tmp20);
    float tmp22 = left1_4351_43right_f32_f32(tmp19, tmp21);
    float tmp23 = left1_4331_43right_f32_f32(tmp16, tmp22);
    float second = left1_4331_43right_f32_f32(tmp13, tmp23);
    float tmp25 = 29.79000091552734375;
    float tmp26 = left1_4321_43right_f32_f32(point._m0, tmp25);
    float tmp28 = 11.909999847412109375;
    float tmp29 = left1_4321_43right_f32_f32(point._m1, tmp28);
    float tmp31 = 59.3300018310546875;
    float tmp32 = left1_4321_43right_f32_f32(point._m2, tmp31);
    float tmp33 = 1.309999942779541015625;
    float tmp34 = left1_4321_43right_f32_f32(phase, tmp33);
    float tmp35 = left1_4331_43right_f32_f32(tmp32, tmp34);
    float tmp36 = left1_4351_43right_f32_f32(tmp29, tmp35);
    float third = left1_4351_43right_f32_f32(tmp26, tmp36);
    float tmp37 = the_sine_of_value_f32(first);
    float primary = the_absolute_value_of_magnitude_f32(tmp37);
    float tmp38 = the_sine_of_value_f32(second);
    float secondary = the_absolute_value_of_magnitude_f32(tmp38);
    float tmp39 = the_sine_of_value_f32(third);
    float tertiary = the_absolute_value_of_magnitude_f32(tmp39);
    float tmp40 = left1_4321_43right_f32_f32(primary, primary);
    float tmp41 = left1_4321_43right_f32_f32(secondary, secondary);
    float tmp42 = left1_4331_43right_f32_f32(tmp40, tmp41);
    float tmp43 = left1_4321_43right_f32_f32(tertiary, tertiary);
    float tmp44 = left1_4331_43right_f32_f32(tmp42, tmp43);
    float crossing = the_square_root_of_value_f32(tmp44);
    float tmp45 = 0.0;
    float tmp46 = 0.4199999868869781494140625;
    float core = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp45, tmp46, crossing);
    float tmp47 = 0.0;
    float tmp48 = 0.699999988079071044921875;
    float aura = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp47, tmp48, crossing);
    float tmp49 = 4.19999980926513671875;
    float tmp50 = left1_4321_43right_f32_f32(core, tmp49);
    float tmp51 = 0.1599999964237213134765625;
    float tmp52 = left1_4321_43right_f32_f32(aura, tmp51);
    return left1_4331_43right_f32_f32(tmp50, tmp52);
}

float the_torus_distance_from_3spatial_shader_point8point5_with_major_radius_3any8outer5_and_tube_radius_3any8inner5_spatial_shader_point_f32_f32(class_8 point, float outer, float inner)
{
    float tmp = left1_4321_43right_f32_f32(point._m0, point._m0);
    float tmp4 = left1_4321_43right_f32_f32(point._m1, point._m1);
    float tmp5 = left1_4331_43right_f32_f32(tmp, tmp4);
    float tmp6 = the_square_root_of_value_f32(tmp5);
    float offset = left1_4351_43right_f32_f32(tmp6, outer);
    float tmp7 = left1_4321_43right_f32_f32(offset, offset);
    float tmp10 = left1_4321_43right_f32_f32(point._m2, point._m2);
    float tmp11 = left1_4331_43right_f32_f32(tmp7, tmp10);
    float tmp12 = the_square_root_of_value_f32(tmp11);
    return left1_4351_43right_f32_f32(tmp12, inner);
}

float the_ellipsoid_distance_from_3spatial_shader_point8point5_with_radii_3shader_dimensions8radii5_spatial_shader_point_shader_dimensions(class_8 point, class_8 radii)
{
    float horizontal = left1_4371_43right_f32_f32(point._m0, radii._m0);
    float vertical = left1_4371_43right_f32_f32(point._m1, radii._m1);
    float forward = left1_4371_43right_f32_f32(point._m2, radii._m2);
    float tmp = left1_4321_43right_f32_f32(horizontal, horizontal);
    float tmp6 = left1_4321_43right_f32_f32(vertical, vertical);
    float tmp7 = left1_4331_43right_f32_f32(tmp, tmp6);
    float tmp8 = left1_4321_43right_f32_f32(forward, forward);
    float tmp9 = left1_4331_43right_f32_f32(tmp7, tmp8);
    float result = the_square_root_of_value_f32(tmp9);
    float tmp13 = the_minimum_of_left_and_right_f32_f32(radii._m1, radii._m2);
    float smallest = the_minimum_of_left_and_right_f32_f32(radii._m0, tmp13);
    float tmp14 = 1.0;
    float tmp15 = left1_4351_43right_f32_f32(result, tmp14);
    return left1_4321_43right_f32_f32(tmp15, smallest);
}

float the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(class_8 point, class_8 start, class_8 end, float radius)
{
    float span = left1_4351_43right_f32_f32(end._m0, start._m0);
    float rise = left1_4351_43right_f32_f32(end._m1, start._m1);
    float depth = left1_4351_43right_f32_f32(end._m2, start._m2);
    float horizontal = left1_4351_43right_f32_f32(point._m0, start._m0);
    float vertical = left1_4351_43right_f32_f32(point._m1, start._m1);
    float forward = left1_4351_43right_f32_f32(point._m2, start._m2);
    float tmp = left1_4321_43right_f32_f32(span, span);
    float tmp12 = left1_4321_43right_f32_f32(rise, rise);
    float tmp13 = left1_4331_43right_f32_f32(tmp, tmp12);
    float tmp14 = left1_4321_43right_f32_f32(depth, depth);
    float measure = left1_4331_43right_f32_f32(tmp13, tmp14);
    float tmp15 = left1_4321_43right_f32_f32(horizontal, span);
    float tmp16 = left1_4321_43right_f32_f32(vertical, rise);
    float tmp17 = left1_4331_43right_f32_f32(tmp15, tmp16);
    float tmp18 = left1_4321_43right_f32_f32(forward, depth);
    float tmp19 = left1_4331_43right_f32_f32(tmp17, tmp18);
    float tmp20 = 9.9999999747524270787835121154785e-07;
    float tmp21 = the_maximum_of_left_and_right_f32_f32(measure, tmp20);
    float projection = left1_4371_43right_f32_f32(tmp19, tmp21);
    projection = number_saturated_f32(projection);
    float tmp23 = left1_4321_43right_f32_f32(span, projection);
    float longitude = left1_4331_43right_f32_f32(start._m0, tmp23);
    float tmp25 = left1_4321_43right_f32_f32(rise, projection);
    float latitude = left1_4331_43right_f32_f32(start._m1, tmp25);
    float tmp27 = left1_4321_43right_f32_f32(depth, projection);
    float altitude = left1_4331_43right_f32_f32(start._m2, tmp27);
    float difference = left1_4351_43right_f32_f32(point._m0, longitude);
    float deviation = left1_4351_43right_f32_f32(point._m1, latitude);
    float separation = left1_4351_43right_f32_f32(point._m2, altitude);
    float tmp31 = left1_4321_43right_f32_f32(difference, difference);
    float tmp32 = left1_4321_43right_f32_f32(deviation, deviation);
    float tmp33 = left1_4321_43right_f32_f32(separation, separation);
    float tmp34 = left1_4331_43right_f32_f32(tmp32, tmp33);
    float tmp35 = left1_4331_43right_f32_f32(tmp31, tmp34);
    float tmp36 = the_square_root_of_value_f32(tmp35);
    return left1_4351_43right_f32_f32(tmp36, radius);
}

float the_motorcycle_distance_at_3spatial_coordinate8position5_with_3motorcycle_motion8motion5_f32_spatial_coordinate_motorcycle_motion(inout float _distance, class_8 position, class_8 motion)
{
    float tmp = 0.0;
    float tmp1 = 1.2599999904632568359375;
    float tmp2 = left1_4351_43right_f32_f32(tmp, tmp1);
    float tmp3 = 1.17999994754791259765625;
    float tmp4 = left1_4321_43right_f32_f32(motion._m0, tmp3);
    class_8 class_tmp = class_8(0.0, 0.0, 0.0);
    class_tmp._m0 = left1_4331_43right_f32_f32(tmp2, tmp4);
    float tmp6 = 0.0;
    float tmp7 = 0.189999997615814208984375;
    float tmp8 = left1_4351_43right_f32_f32(tmp6, tmp7);
    float tmp10 = 0.119999997317790985107421875;
    float tmp11 = left1_4321_43right_f32_f32(motion._m0, tmp10);
    class_tmp._m1 = left1_4331_43right_f32_f32(tmp8, tmp11);
    float tmp13 = 2.7999999523162841796875;
    float tmp15 = 4.900000095367431640625;
    float tmp16 = left1_4321_43right_f32_f32(motion._m0, tmp15);
    class_tmp._m2 = left1_4351_43right_f32_f32(tmp13, tmp16);
    class_8 center = class_tmp;
    class_8 class_tmp18 = class_8(0.0, 0.0, 0.0);
    class_tmp18._m0 = left1_4351_43right_f32_f32(position._m0, center._m0);
    class_tmp18._m1 = left1_4351_43right_f32_f32(position._m1, center._m1);
    class_tmp18._m2 = left1_4351_43right_f32_f32(position._m2, center._m2);
    class_8 translated = class_tmp18;
    float sine = the_sine_of_value_f32(motion._m2);
    float cosine = the_cosine_of_value_f32(motion._m2);
    float tmp33 = left1_4321_43right_f32_f32(translated._m0, cosine);
    float tmp35 = left1_4321_43right_f32_f32(translated._m2, sine);
    class_8 class_tmp31 = class_8(0.0, 0.0, 0.0);
    class_tmp31._m0 = left1_4331_43right_f32_f32(tmp33, tmp35);
    class_tmp31._m1 = translated._m1;
    float tmp40 = left1_4321_43right_f32_f32(translated._m2, cosine);
    float tmp42 = left1_4321_43right_f32_f32(translated._m0, sine);
    class_tmp31._m2 = left1_4351_43right_f32_f32(tmp40, tmp42);
    class_8 local = class_tmp31;
    float tmp47 = 0.7200000286102294921875;
    class_8 class_tmp45 = class_8(0.0, 0.0, 0.0);
    class_tmp45._m0 = left1_4331_43right_f32_f32(local._m0, tmp47);
    float tmp50 = 0.4199999868869781494140625;
    class_tmp45._m1 = left1_4331_43right_f32_f32(local._m1, tmp50);
    class_tmp45._m2 = local._m2;
    class_8 back = class_tmp45;
    float tmp57 = 0.7200000286102294921875;
    class_8 class_tmp55 = class_8(0.0, 0.0, 0.0);
    class_tmp55._m0 = left1_4351_43right_f32_f32(local._m0, tmp57);
    float tmp60 = 0.4199999868869781494140625;
    class_tmp55._m1 = left1_4331_43right_f32_f32(local._m1, tmp60);
    class_tmp55._m2 = local._m2;
    class_8 front = class_tmp55;
    class_8 class_tmp66 = class_8(0.0, 0.0, 0.0);
    class_tmp66._m0 = local._m0;
    class_tmp66._m1 = local._m1;
    class_tmp66._m2 = local._m2;
    class_8 _sample = class_tmp66;
    class_8 class_tmp76 = class_8(0.0, 0.0, 0.0);
    class_tmp76._m0 = back._m0;
    class_tmp76._m1 = back._m1;
    class_tmp76._m2 = back._m2;
    class_8 aft = class_tmp76;
    class_8 class_tmp87 = class_8(0.0, 0.0, 0.0);
    class_tmp87._m0 = front._m0;
    class_tmp87._m1 = front._m1;
    class_tmp87._m2 = front._m2;
    class_8 fore = class_tmp87;
    class_8 class_tmp98 = class_8(0.0, 0.0, 0.0);
    class_tmp98._m0 = 0.0949999988079071044921875;
    class_tmp98._m1 = 0.0949999988079071044921875;
    class_tmp98._m2 = 0.12999999523162841796875;
    class_8 dimensions = class_tmp98;
    float tmp103 = 0.300000011920928955078125;
    float tmp104 = 0.054999999701976776123046875;
    float tmp105 = the_torus_distance_from_3spatial_shader_point8point5_with_major_radius_3any8outer5_and_tube_radius_3any8inner5_spatial_shader_point_f32_f32(aft, tmp103, tmp104);
    float tmp106 = 0.300000011920928955078125;
    float tmp107 = 0.054999999701976776123046875;
    float tmp108 = the_torus_distance_from_3spatial_shader_point8point5_with_major_radius_3any8outer5_and_tube_radius_3any8inner5_spatial_shader_point_f32_f32(fore, tmp106, tmp107);
    _distance = the_minimum_of_left_and_right_f32_f32(tmp105, tmp108);
    float tmp109 = the_ellipsoid_distance_from_3spatial_shader_point8point5_with_radii_3shader_dimensions8radii5_spatial_shader_point_shader_dimensions(aft, dimensions);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp109);
    float tmp110 = the_ellipsoid_distance_from_3spatial_shader_point8point5_with_radii_3shader_dimensions8radii5_spatial_shader_point_shader_dimensions(fore, dimensions);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp110);
    float tmp113 = the_cosine_of_value_f32(motion._m1);
    float tmp114 = 0.26499998569488525390625;
    class_6 class_tmp111 = class_6(0.0, 0.0);
    class_tmp111._m0 = left1_4321_43right_f32_f32(tmp113, tmp114);
    float tmp117 = the_sine_of_value_f32(motion._m1);
    float tmp118 = 0.26499998569488525390625;
    class_tmp111._m1 = left1_4321_43right_f32_f32(tmp117, tmp118);
    class_6 spoke = class_tmp111;
    class_6 class_tmp121 = class_6(0.0, 0.0);
    class_tmp121._m0 = spoke._m1;
    class_tmp121._m1 = spoke._m0;
    class_6 crossing = class_tmp121;
    float tmp130 = 0.0;
    class_8 class_tmp129 = class_8(0.0, 0.0, 0.0);
    class_tmp129._m0 = left1_4351_43right_f32_f32(tmp130, spoke._m0);
    float tmp133 = 0.0;
    class_tmp129._m1 = left1_4351_43right_f32_f32(tmp133, spoke._m1);
    class_tmp129._m2 = 0.0;
    class_8 tmp138 = class_tmp129;
    class_8 class_tmp139 = class_8(0.0, 0.0, 0.0);
    class_tmp139._m0 = spoke._m0;
    class_tmp139._m1 = spoke._m1;
    class_tmp139._m2 = 0.0;
    class_8 tmp148 = class_tmp139;
    float tmp149 = 0.017999999225139617919921875;
    float tmp150 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(aft, tmp138, tmp148, tmp149);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp150);
    float tmp152 = 0.0;
    class_8 class_tmp151 = class_8(0.0, 0.0, 0.0);
    class_tmp151._m0 = left1_4351_43right_f32_f32(tmp152, spoke._m1);
    class_tmp151._m1 = spoke._m0;
    class_tmp151._m2 = 0.0;
    class_8 tmp160 = class_tmp151;
    class_8 class_tmp161 = class_8(0.0, 0.0, 0.0);
    class_tmp161._m0 = spoke._m1;
    float tmp165 = 0.0;
    class_tmp161._m1 = left1_4351_43right_f32_f32(tmp165, spoke._m0);
    class_tmp161._m2 = 0.0;
    class_8 tmp170 = class_tmp161;
    float tmp171 = 0.017999999225139617919921875;
    float tmp172 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(aft, tmp160, tmp170, tmp171);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp172);
    float tmp174 = 0.0;
    class_8 class_tmp173 = class_8(0.0, 0.0, 0.0);
    class_tmp173._m0 = left1_4351_43right_f32_f32(tmp174, crossing._m0);
    float tmp177 = 0.0;
    class_tmp173._m1 = left1_4351_43right_f32_f32(tmp177, crossing._m1);
    class_tmp173._m2 = 0.0;
    class_8 tmp182 = class_tmp173;
    class_8 class_tmp183 = class_8(0.0, 0.0, 0.0);
    class_tmp183._m0 = crossing._m0;
    class_tmp183._m1 = crossing._m1;
    class_tmp183._m2 = 0.0;
    class_8 tmp192 = class_tmp183;
    float tmp193 = 0.017999999225139617919921875;
    float tmp194 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(fore, tmp182, tmp192, tmp193);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp194);
    float tmp196 = 0.0;
    class_8 class_tmp195 = class_8(0.0, 0.0, 0.0);
    class_tmp195._m0 = left1_4351_43right_f32_f32(tmp196, crossing._m1);
    class_tmp195._m1 = crossing._m0;
    class_tmp195._m2 = 0.0;
    class_8 tmp204 = class_tmp195;
    class_8 class_tmp205 = class_8(0.0, 0.0, 0.0);
    class_tmp205._m0 = crossing._m1;
    float tmp209 = 0.0;
    class_tmp205._m1 = left1_4351_43right_f32_f32(tmp209, crossing._m0);
    class_tmp205._m2 = 0.0;
    class_8 tmp214 = class_tmp205;
    float tmp215 = 0.017999999225139617919921875;
    float tmp216 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(fore, tmp204, tmp214, tmp215);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp216);
    float tmp218 = 0.7200000286102294921875;
    class_8 class_tmp217 = class_8(0.0, 0.0, 0.0);
    class_tmp217._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp218);
    float tmp220 = 0.4199999868869781494140625;
    class_tmp217._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp220);
    class_tmp217._m2 = 0.0;
    class_8 tmp224 = class_tmp217;
    float tmp226 = 0.180000007152557373046875;
    class_8 class_tmp225 = class_8(0.0, 0.0, 0.0);
    class_tmp225._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp226);
    class_tmp225._m1 = 0.07999999821186065673828125;
    class_tmp225._m2 = 0.0;
    class_8 tmp231 = class_tmp225;
    float tmp232 = 0.04500000178813934326171875;
    float tmp233 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp224, tmp231, tmp232);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp233);
    float tmp235 = 0.180000007152557373046875;
    class_8 class_tmp234 = class_8(0.0, 0.0, 0.0);
    class_tmp234._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp235);
    class_tmp234._m1 = 0.07999999821186065673828125;
    class_tmp234._m2 = 0.0;
    class_8 tmp240 = class_tmp234;
    class_8 class_tmp241 = class_8(0.0, 0.0, 0.0);
    class_tmp241._m0 = 0.7200000286102294921875;
    float tmp243 = 0.4199999868869781494140625;
    class_tmp241._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp243);
    class_tmp241._m2 = 0.0;
    class_8 tmp247 = class_tmp241;
    float tmp248 = 0.04500000178813934326171875;
    float tmp249 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp240, tmp247, tmp248);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp249);
    class_8 class_tmp250 = class_8(0.0, 0.0, 0.0);
    class_tmp250._m0 = 0.7200000286102294921875;
    float tmp252 = 0.4199999868869781494140625;
    class_tmp250._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp252);
    class_tmp250._m2 = 0.0;
    class_8 tmp256 = class_tmp250;
    float tmp258 = 0.3400000035762786865234375;
    class_8 class_tmp257 = class_8(0.0, 0.0, 0.0);
    class_tmp257._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp258);
    float tmp260 = 0.4199999868869781494140625;
    class_tmp257._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp260);
    class_tmp257._m2 = 0.0;
    class_8 tmp264 = class_tmp257;
    float tmp265 = 0.04500000178813934326171875;
    float tmp266 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp256, tmp264, tmp265);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp266);
    float tmp268 = 0.3400000035762786865234375;
    class_8 class_tmp267 = class_8(0.0, 0.0, 0.0);
    class_tmp267._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp268);
    float tmp270 = 0.4199999868869781494140625;
    class_tmp267._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp270);
    class_tmp267._m2 = 0.0;
    class_8 tmp274 = class_tmp267;
    float tmp276 = 0.180000007152557373046875;
    class_8 class_tmp275 = class_8(0.0, 0.0, 0.0);
    class_tmp275._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp276);
    class_tmp275._m1 = 0.07999999821186065673828125;
    class_tmp275._m2 = 0.0;
    class_8 tmp281 = class_tmp275;
    float tmp282 = 0.04500000178813934326171875;
    float tmp283 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp274, tmp281, tmp282);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp283);
    class_8 class_tmp284 = class_8(0.0, 0.0, 0.0);
    class_tmp284._m0 = 0.550000011920928955078125;
    float tmp286 = 0.2800000011920928955078125;
    class_tmp284._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp286);
    float tmp288 = 0.070000000298023223876953125;
    class_tmp284._m2 = _the_negative_of_4the_opposite_of_453value_f32(tmp288);
    class_8 tmp291 = class_tmp284;
    class_8 class_tmp292 = class_8(0.0, 0.0, 0.0);
    class_tmp292._m0 = 0.769999980926513671875;
    class_tmp292._m1 = 0.2800000011920928955078125;
    float tmp295 = 0.070000000298023223876953125;
    class_tmp292._m2 = _the_negative_of_4the_opposite_of_453value_f32(tmp295);
    class_8 tmp298 = class_tmp292;
    float tmp299 = 0.0320000015199184417724609375;
    float tmp300 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp291, tmp298, tmp299);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp300);
    class_8 class_tmp301 = class_8(0.0, 0.0, 0.0);
    class_tmp301._m0 = 0.550000011920928955078125;
    float tmp303 = 0.2800000011920928955078125;
    class_tmp301._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp303);
    class_tmp301._m2 = 0.070000000298023223876953125;
    class_8 tmp307 = class_tmp301;
    class_8 class_tmp308 = class_8(0.0, 0.0, 0.0);
    class_tmp308._m0 = 0.769999980926513671875;
    class_tmp308._m1 = 0.2800000011920928955078125;
    class_tmp308._m2 = 0.070000000298023223876953125;
    class_8 tmp313 = class_tmp308;
    float tmp314 = 0.0320000015199184417724609375;
    float tmp315 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp307, tmp313, tmp314);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp315);
    class_8 class_tmp316 = class_8(0.0, 0.0, 0.0);
    class_tmp316._m0 = 0.75;
    class_tmp316._m1 = 0.2800000011920928955078125;
    float tmp319 = 0.2800000011920928955078125;
    class_tmp316._m2 = _the_negative_of_4the_opposite_of_453value_f32(tmp319);
    class_8 tmp322 = class_tmp316;
    class_8 class_tmp323 = class_8(0.0, 0.0, 0.0);
    class_tmp323._m0 = 0.75;
    class_tmp323._m1 = 0.2800000011920928955078125;
    class_tmp323._m2 = 0.2800000011920928955078125;
    class_8 tmp328 = class_tmp323;
    float tmp329 = 0.0280000008642673492431640625;
    float tmp330 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp322, tmp328, tmp329);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp330);
    float tmp333 = 0.07999999821186065673828125;
    class_8 class_tmp331 = class_8(0.0, 0.0, 0.0);
    class_tmp331._m0 = left1_4331_43right_f32_f32(local._m0, tmp333);
    float tmp336 = 0.02999999932944774627685546875;
    class_tmp331._m1 = left1_4351_43right_f32_f32(local._m1, tmp336);
    class_tmp331._m2 = local._m2;
    class_8 tmp342 = class_tmp331;
    class_8 class_tmp343 = class_8(0.0, 0.0, 0.0);
    class_tmp343._m0 = 0.3400000035762786865234375;
    class_tmp343._m1 = 0.2199999988079071044921875;
    class_tmp343._m2 = 0.25;
    class_8 tmp348 = class_tmp343;
    float tmp349 = the_ellipsoid_distance_from_3spatial_shader_point8point5_with_radii_3shader_dimensions8radii5_spatial_shader_point_shader_dimensions(tmp342, tmp348);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp349);
    float tmp352 = 0.0199999995529651641845703125;
    class_8 class_tmp350 = class_8(0.0, 0.0, 0.0);
    class_tmp350._m0 = left1_4351_43right_f32_f32(local._m0, tmp352);
    float tmp355 = 0.2199999988079071044921875;
    class_tmp350._m1 = left1_4331_43right_f32_f32(local._m1, tmp355);
    class_tmp350._m2 = local._m2;
    class_8 tmp361 = class_tmp350;
    class_8 class_tmp362 = class_8(0.0, 0.0, 0.0);
    class_tmp362._m0 = 0.3400000035762786865234375;
    class_tmp362._m1 = 0.180000007152557373046875;
    class_tmp362._m2 = 0.2199999988079071044921875;
    class_8 tmp367 = class_tmp362;
    float tmp368 = the_ellipsoid_distance_from_3spatial_shader_point8point5_with_radii_3shader_dimensions8radii5_spatial_shader_point_shader_dimensions(tmp361, tmp367);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp368);
    float tmp371 = 0.4000000059604644775390625;
    class_8 class_tmp369 = class_8(0.0, 0.0, 0.0);
    class_tmp369._m0 = left1_4331_43right_f32_f32(local._m0, tmp371);
    float tmp374 = 0.23999999463558197021484375;
    class_tmp369._m1 = left1_4351_43right_f32_f32(local._m1, tmp374);
    class_tmp369._m2 = local._m2;
    class_8 tmp380 = class_tmp369;
    class_8 class_tmp381 = class_8(0.0, 0.0, 0.0);
    class_tmp381._m0 = 0.310000002384185791015625;
    class_tmp381._m1 = 0.07500000298023223876953125;
    class_tmp381._m2 = 0.2199999988079071044921875;
    class_8 tmp386 = class_tmp381;
    float tmp387 = the_ellipsoid_distance_from_3spatial_shader_point8point5_with_radii_3shader_dimensions8radii5_spatial_shader_point_shader_dimensions(tmp380, tmp386);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp387);
    float tmp390 = 0.7799999713897705078125;
    class_8 class_tmp388 = class_8(0.0, 0.0, 0.0);
    class_tmp388._m0 = left1_4351_43right_f32_f32(local._m0, tmp390);
    float tmp393 = 0.07999999821186065673828125;
    class_tmp388._m1 = left1_4351_43right_f32_f32(local._m1, tmp393);
    class_tmp388._m2 = local._m2;
    class_8 tmp399 = class_tmp388;
    class_8 class_tmp400 = class_8(0.0, 0.0, 0.0);
    class_tmp400._m0 = 0.085000000894069671630859375;
    class_tmp400._m1 = 0.085000000894069671630859375;
    class_tmp400._m2 = 0.104999996721744537353515625;
    class_8 tmp405 = class_tmp400;
    float tmp406 = the_ellipsoid_distance_from_3spatial_shader_point8point5_with_radii_3shader_dimensions8radii5_spatial_shader_point_shader_dimensions(tmp399, tmp405);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp406);
    float tmp409 = 0.100000001490116119384765625;
    class_8 class_tmp407 = class_8(0.0, 0.0, 0.0);
    class_tmp407._m0 = left1_4331_43right_f32_f32(local._m0, tmp409);
    float tmp412 = 0.7799999713897705078125;
    class_tmp407._m1 = left1_4351_43right_f32_f32(local._m1, tmp412);
    class_tmp407._m2 = local._m2;
    class_8 tmp418 = class_tmp407;
    class_8 class_tmp419 = class_8(0.0, 0.0, 0.0);
    class_tmp419._m0 = 0.1500000059604644775390625;
    class_tmp419._m1 = 0.17000000178813934326171875;
    class_tmp419._m2 = 0.1500000059604644775390625;
    class_8 tmp424 = class_tmp419;
    float tmp425 = the_ellipsoid_distance_from_3spatial_shader_point8point5_with_radii_3shader_dimensions8radii5_spatial_shader_point_shader_dimensions(tmp418, tmp424);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp425);
    float tmp428 = 0.180000007152557373046875;
    class_8 class_tmp426 = class_8(0.0, 0.0, 0.0);
    class_tmp426._m0 = left1_4331_43right_f32_f32(local._m0, tmp428);
    float tmp431 = 0.4799999892711639404296875;
    class_tmp426._m1 = left1_4351_43right_f32_f32(local._m1, tmp431);
    class_tmp426._m2 = local._m2;
    class_8 tmp437 = class_tmp426;
    class_8 class_tmp438 = class_8(0.0, 0.0, 0.0);
    class_tmp438._m0 = 0.23999999463558197021484375;
    class_tmp438._m1 = 0.3499999940395355224609375;
    class_tmp438._m2 = 0.180000007152557373046875;
    class_8 tmp443 = class_tmp438;
    float tmp444 = the_ellipsoid_distance_from_3spatial_shader_point8point5_with_radii_3shader_dimensions8radii5_spatial_shader_point_shader_dimensions(tmp437, tmp443);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp444);
    float tmp446 = 0.0199999995529651641845703125;
    class_8 class_tmp445 = class_8(0.0, 0.0, 0.0);
    class_tmp445._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp446);
    class_tmp445._m1 = 0.589999973773956298828125;
    float tmp449 = 0.10999999940395355224609375;
    class_tmp445._m2 = _the_negative_of_4the_opposite_of_453value_f32(tmp449);
    class_8 tmp452 = class_tmp445;
    class_8 class_tmp453 = class_8(0.0, 0.0, 0.0);
    class_tmp453._m0 = 0.37000000476837158203125;
    class_tmp453._m1 = 0.300000011920928955078125;
    float tmp456 = 0.1500000059604644775390625;
    class_tmp453._m2 = _the_negative_of_4the_opposite_of_453value_f32(tmp456);
    class_8 tmp459 = class_tmp453;
    float tmp460 = 0.07500000298023223876953125;
    float tmp461 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp452, tmp459, tmp460);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp461);
    float tmp463 = 0.0199999995529651641845703125;
    class_8 class_tmp462 = class_8(0.0, 0.0, 0.0);
    class_tmp462._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp463);
    class_tmp462._m1 = 0.589999973773956298828125;
    class_tmp462._m2 = 0.10999999940395355224609375;
    class_8 tmp468 = class_tmp462;
    class_8 class_tmp469 = class_8(0.0, 0.0, 0.0);
    class_tmp469._m0 = 0.37000000476837158203125;
    class_tmp469._m1 = 0.300000011920928955078125;
    class_tmp469._m2 = 0.1500000059604644775390625;
    class_8 tmp474 = class_tmp469;
    float tmp475 = 0.07500000298023223876953125;
    float tmp476 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp468, tmp474, tmp475);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp476);
    class_8 class_tmp477 = class_8(0.0, 0.0, 0.0);
    class_tmp477._m0 = 0.37000000476837158203125;
    class_tmp477._m1 = 0.300000011920928955078125;
    float tmp480 = 0.1500000059604644775390625;
    class_tmp477._m2 = _the_negative_of_4the_opposite_of_453value_f32(tmp480);
    class_8 tmp483 = class_tmp477;
    class_8 class_tmp484 = class_8(0.0, 0.0, 0.0);
    class_tmp484._m0 = 0.730000019073486328125;
    class_tmp484._m1 = 0.2700000107288360595703125;
    float tmp487 = 0.2199999988079071044921875;
    class_tmp484._m2 = _the_negative_of_4the_opposite_of_453value_f32(tmp487);
    class_8 tmp490 = class_tmp484;
    float tmp491 = 0.054999999701976776123046875;
    float tmp492 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp483, tmp490, tmp491);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp492);
    class_8 class_tmp493 = class_8(0.0, 0.0, 0.0);
    class_tmp493._m0 = 0.37000000476837158203125;
    class_tmp493._m1 = 0.300000011920928955078125;
    class_tmp493._m2 = 0.1500000059604644775390625;
    class_8 tmp498 = class_tmp493;
    class_8 class_tmp499 = class_8(0.0, 0.0, 0.0);
    class_tmp499._m0 = 0.730000019073486328125;
    class_tmp499._m1 = 0.2700000107288360595703125;
    class_tmp499._m2 = 0.2199999988079071044921875;
    class_8 tmp504 = class_tmp499;
    float tmp505 = 0.054999999701976776123046875;
    float tmp506 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp498, tmp504, tmp505);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp506);
    float tmp508 = 0.14000000059604644775390625;
    class_8 class_tmp507 = class_8(0.0, 0.0, 0.0);
    class_tmp507._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp508);
    class_tmp507._m1 = 0.23999999463558197021484375;
    float tmp511 = 0.100000001490116119384765625;
    class_tmp507._m2 = _the_negative_of_4the_opposite_of_453value_f32(tmp511);
    class_8 tmp514 = class_tmp507;
    float tmp516 = 0.4000000059604644775390625;
    class_8 class_tmp515 = class_8(0.0, 0.0, 0.0);
    class_tmp515._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp516);
    float tmp518 = 0.180000007152557373046875;
    class_tmp515._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp518);
    float tmp520 = 0.12999999523162841796875;
    class_tmp515._m2 = _the_negative_of_4the_opposite_of_453value_f32(tmp520);
    class_8 tmp523 = class_tmp515;
    float tmp524 = 0.0949999988079071044921875;
    float tmp525 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp514, tmp523, tmp524);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp525);
    float tmp527 = 0.14000000059604644775390625;
    class_8 class_tmp526 = class_8(0.0, 0.0, 0.0);
    class_tmp526._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp527);
    class_tmp526._m1 = 0.23999999463558197021484375;
    class_tmp526._m2 = 0.100000001490116119384765625;
    class_8 tmp532 = class_tmp526;
    float tmp534 = 0.4000000059604644775390625;
    class_8 class_tmp533 = class_8(0.0, 0.0, 0.0);
    class_tmp533._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp534);
    float tmp536 = 0.180000007152557373046875;
    class_tmp533._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp536);
    class_tmp533._m2 = 0.12999999523162841796875;
    class_8 tmp540 = class_tmp533;
    float tmp541 = 0.0949999988079071044921875;
    float tmp542 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp532, tmp540, tmp541);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp542);
    float tmp544 = 0.4000000059604644775390625;
    class_8 class_tmp543 = class_8(0.0, 0.0, 0.0);
    class_tmp543._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp544);
    float tmp546 = 0.180000007152557373046875;
    class_tmp543._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp546);
    float tmp548 = 0.12999999523162841796875;
    class_tmp543._m2 = _the_negative_of_4the_opposite_of_453value_f32(tmp548);
    class_8 tmp551 = class_tmp543;
    float tmp553 = 0.62000000476837158203125;
    class_8 class_tmp552 = class_8(0.0, 0.0, 0.0);
    class_tmp552._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp553);
    float tmp555 = 0.3400000035762786865234375;
    class_tmp552._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp555);
    float tmp557 = 0.100000001490116119384765625;
    class_tmp552._m2 = _the_negative_of_4the_opposite_of_453value_f32(tmp557);
    class_8 tmp560 = class_tmp552;
    float tmp561 = 0.07500000298023223876953125;
    float tmp562 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp551, tmp560, tmp561);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp562);
    float tmp564 = 0.4000000059604644775390625;
    class_8 class_tmp563 = class_8(0.0, 0.0, 0.0);
    class_tmp563._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp564);
    float tmp566 = 0.180000007152557373046875;
    class_tmp563._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp566);
    class_tmp563._m2 = 0.12999999523162841796875;
    class_8 tmp570 = class_tmp563;
    float tmp572 = 0.62000000476837158203125;
    class_8 class_tmp571 = class_8(0.0, 0.0, 0.0);
    class_tmp571._m0 = _the_negative_of_4the_opposite_of_453value_f32(tmp572);
    float tmp574 = 0.3400000035762786865234375;
    class_tmp571._m1 = _the_negative_of_4the_opposite_of_453value_f32(tmp574);
    class_tmp571._m2 = 0.100000001490116119384765625;
    class_8 tmp578 = class_tmp571;
    float tmp579 = 0.07500000298023223876953125;
    float tmp580 = the_distance_from_3spatial_shader_point8point5_to_capsule_from_3spatial_shader_point8start5_to_3spatial_shader_point8end5_with_radius_3any8radius5_spatial_shader_point_spatial_shader_point_spatial_shader_point_f32(_sample, tmp570, tmp578, tmp579);
    _distance = the_minimum_of_left_and_right_f32_f32(_distance, tmp580);
    return _distance;
}

float the_crystal_distance_at_3spatial_coordinate8position5_rotated_by_3float8yaw5_and_3float8pitch5_spatial_coordinate_f32_f32(class_8 position, float yaw, float pitch)
{
    float sine = the_sine_of_value_f32(yaw);
    float cosine = the_cosine_of_value_f32(yaw);
    float tmp = left1_4321_43right_f32_f32(position._m0, cosine);
    float tmp2 = left1_4321_43right_f32_f32(position._m2, sine);
    class_8 class_tmp = class_8(0.0, 0.0, 0.0);
    class_tmp._m0 = left1_4331_43right_f32_f32(tmp, tmp2);
    class_tmp._m1 = position._m1;
    float tmp7 = left1_4321_43right_f32_f32(position._m2, cosine);
    float tmp9 = left1_4321_43right_f32_f32(position._m0, sine);
    class_tmp._m2 = left1_4351_43right_f32_f32(tmp7, tmp9);
    class_8 turned = class_tmp;
    sine = the_sine_of_value_f32(pitch);
    cosine = the_cosine_of_value_f32(pitch);
    float tmp12 = left1_4321_43right_f32_f32(turned._m1, cosine);
    float tmp14 = left1_4321_43right_f32_f32(turned._m2, sine);
    float tmp16 = left1_4321_43right_f32_f32(turned._m2, cosine);
    float tmp18 = left1_4321_43right_f32_f32(turned._m1, sine);
    turned._m2 = left1_4331_43right_f32_f32(tmp16, tmp18);
    turned._m1 = left1_4351_43right_f32_f32(tmp12, tmp14);
    float tmp22 = the_absolute_value_of_magnitude_f32(turned._m0);
    float tmp24 = the_absolute_value_of_magnitude_f32(turned._m1);
    float tmp25 = left1_4331_43right_f32_f32(tmp22, tmp24);
    float tmp27 = the_absolute_value_of_magnitude_f32(turned._m2);
    float tmp28 = left1_4331_43right_f32_f32(tmp25, tmp27);
    float tmp29 = 1.08000004291534423828125;
    float tmp30 = left1_4351_43right_f32_f32(tmp28, tmp29);
    float tmp31 = 0.57700002193450927734375;
    float octahedron = left1_4321_43right_f32_f32(tmp30, tmp31);
    class_8 class_tmp32 = class_8(0.0, 0.0, 0.0);
    class_tmp32._m0 = turned._m0;
    class_tmp32._m1 = turned._m1;
    class_tmp32._m2 = turned._m2;
    class_8 tmp41 = class_tmp32;
    class_8 class_tmp42 = class_8(0.0, 0.0, 0.0);
    class_tmp42._m0 = 0.819999992847442626953125;
    class_tmp42._m1 = 1.13999998569488525390625;
    class_tmp42._m2 = 0.819999992847442626953125;
    class_8 tmp47 = class_tmp42;
    float core = the_ellipsoid_distance_from_3spatial_shader_point8point5_with_radii_3shader_dimensions8radii5_spatial_shader_point_shader_dimensions(tmp41, tmp47);
    return the_maximum_of_left_and_right_f32_f32(octahedron, core);
}

uint left1_4331_43right_i32_i32(uint left, uint right)
{
    return left + right;
}

void main()
{
    class_6 class_tmp = class_6(0.0, 0.0);
    class_tmp._m0 = gl_FragCoord.x;
    class_tmp._m1 = gl_FragCoord.y;
    class_6 pixel = class_tmp;
    float time = dynlexUniform0.value;
    float tmp = dynlexUniform2.value;
    float tmp5 = 1.0;
    class_6 class_tmp4 = class_6(0.0, 0.0);
    class_tmp4._m0 = the_maximum_of_left_and_right_f32_f32(tmp, tmp5);
    float tmp7 = dynlexUniform3.value;
    float tmp8 = 1.0;
    class_tmp4._m1 = the_maximum_of_left_and_right_f32_f32(tmp7, tmp8);
    class_6 frame = class_tmp4;
    float pass = dynlexUniform1.value;
    float aspect = left1_4371_43right_f32_f32(frame._m0, frame._m1);
    float tmp16 = left1_4371_43right_f32_f32(pixel._m0, frame._m0);
    float tmp17 = 2.0;
    float tmp18 = left1_4321_43right_f32_f32(tmp16, tmp17);
    float tmp19 = 1.0;
    float tmp20 = left1_4351_43right_f32_f32(tmp18, tmp19);
    class_6 class_tmp13 = class_6(0.0, 0.0);
    class_tmp13._m0 = left1_4321_43right_f32_f32(tmp20, aspect);
    float tmp24 = left1_4371_43right_f32_f32(pixel._m1, frame._m1);
    float tmp25 = 2.0;
    float tmp26 = left1_4321_43right_f32_f32(tmp24, tmp25);
    float tmp27 = 1.0;
    class_tmp13._m1 = left1_4351_43right_f32_f32(tmp26, tmp27);
    class_6 screen = class_tmp13;
    float tmp32 = left1_4321_43right_f32_f32(screen._m0, screen._m0);
    float tmp35 = left1_4321_43right_f32_f32(screen._m1, screen._m1);
    float tmp36 = left1_4331_43right_f32_f32(tmp32, tmp35);
    float radial = the_square_root_of_value_f32(tmp36);
    float tmp37 = 11.0;
    float tmp38 = left1_4371_43right_f32_f32(time, tmp37);
    float tmp39 = the_fractional_part_of_number_f32(tmp38);
    float tmp40 = 11.0;
    float moment = left1_4321_43right_f32_f32(tmp39, tmp40);
    float tmp41 = 0.5;
    if (left_2_right_f32_f32(pass, tmp41))
    {
        float tmp43 = 19.0;
        float tmp44 = left1_4321_43right_f32_f32(screen._m0, tmp43);
        float tmp46 = 13.0;
        float tmp47 = left1_4321_43right_f32_f32(screen._m1, tmp46);
        float tmp48 = left1_4351_43right_f32_f32(tmp44, tmp47);
        float tmp49 = 4.19999980926513671875;
        float tmp50 = left1_4321_43right_f32_f32(time, tmp49);
        float tmp51 = left1_4331_43right_f32_f32(tmp48, tmp50);
        float tmp52 = the_sine_of_value_f32(tmp51);
        float tmp53 = 0.5;
        float tmp54 = left1_4321_43right_f32_f32(tmp52, tmp53);
        float tmp55 = 0.5;
        float wave = left1_4331_43right_f32_f32(tmp54, tmp55);
        float tmp57 = 47.0;
        float tmp58 = left1_4321_43right_f32_f32(screen._m1, tmp57);
        float tmp59 = 6.80000019073486328125;
        float tmp60 = left1_4321_43right_f32_f32(time, tmp59);
        float tmp61 = left1_4351_43right_f32_f32(tmp58, tmp60);
        float tmp62 = the_sine_of_value_f32(tmp61);
        float tmp63 = 0.5;
        float tmp64 = left1_4321_43right_f32_f32(tmp62, tmp63);
        float tmp65 = 0.5;
        float scan = left1_4331_43right_f32_f32(tmp64, tmp65);
        float tmp67 = 0.0350000001490116119384765625;
        float tmp68 = 0.14000000059604644775390625;
        float tmp69 = left1_4321_43right_f32_f32(wave, tmp68);
        class_8 class_tmp66 = class_8(0.0, 0.0, 0.0);
        class_tmp66._m0 = left1_4331_43right_f32_f32(tmp67, tmp69);
        float tmp71 = 0.14000000059604644775390625;
        float tmp72 = 0.119999997317790985107421875;
        float tmp73 = left1_4321_43right_f32_f32(scan, tmp72);
        class_tmp66._m1 = left1_4331_43right_f32_f32(tmp71, tmp73);
        float tmp75 = 0.3400000035762786865234375;
        float tmp76 = 0.20000000298023223876953125;
        float tmp77 = left1_4321_43right_f32_f32(wave, tmp76);
        class_tmp66._m2 = left1_4331_43right_f32_f32(tmp75, tmp77);
        class_8 color = class_tmp66;
        vec4 _1206 = vec4(0.0, 0.0, 0.0, 1.0);
        _1206.z = color._m2;
        _1206.y = color._m1;
        _1206.x = color._m0;
        dynlexColor = _1206;
    }
    else
    {
        class_8 class_tmp84 = class_8(0.0, 0.0, 0.0);
        class_tmp84._m0 = 0.0;
        class_tmp84._m1 = 0.0199999995529651641845703125;
        float tmp87 = 4.19999980926513671875;
        class_tmp84._m2 = _the_negative_of_4the_opposite_of_453value_f32(tmp87);
        class_8 camera = class_tmp84;
        class_8 class_tmp90 = class_8(0.0, 0.0, 0.0);
        class_tmp90._m0 = screen._m0;
        class_tmp90._m1 = screen._m1;
        class_tmp90._m2 = 1.7200000286102294921875;
        class_8 ray = class_tmp90;
        float tmp99 = left1_4321_43right_f32_f32(ray._m0, ray._m0);
        float tmp102 = left1_4321_43right_f32_f32(ray._m1, ray._m1);
        float tmp103 = left1_4331_43right_f32_f32(tmp99, tmp102);
        float tmp106 = left1_4321_43right_f32_f32(ray._m2, ray._m2);
        float tmp107 = left1_4331_43right_f32_f32(tmp103, tmp106);
        float _length = the_square_root_of_value_f32(tmp107);
        ray._m0 = left1_4371_43right_f32_f32(ray._m0, _length);
        ray._m1 = left1_4371_43right_f32_f32(ray._m1, _length);
        ray._m2 = left1_4371_43right_f32_f32(ray._m2, _length);
        float tmp114 = 0.0;
        float tmp115 = 3.650000095367431640625;
        float crystal = the_scene_window_from_3float8opening5_to_3float8closing5_at_3float8moment5_f32_f32_f32(tmp114, tmp115, moment);
        float tmp116 = 3.349999904632568359375;
        float tmp117 = 7.55000019073486328125;
        float motorcycle = the_scene_window_from_3float8opening5_to_3float8closing5_at_3float8moment5_f32_f32_f32(tmp116, tmp117, moment);
        float tmp118 = 7.179999828338623046875;
        float tmp119 = 11.0;
        float vitruvian = the_scene_window_from_3float8opening5_to_3float8closing5_at_3float8moment5_f32_f32_f32(tmp118, tmp119, moment);
        float tmp120 = 3.25;
        float tmp121 = 7.349999904632568359375;
        float progress = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp120, tmp121, moment);
        class_8 class_tmp122 = class_8(0.0, 0.0, 0.0);
        class_tmp122._m0 = progress;
        float tmp124 = 9.3999996185302734375;
        class_tmp122._m1 = left1_4321_43right_f32_f32(time, tmp124);
        float tmp126 = 0.0;
        float tmp127 = 1.17999994754791259765625;
        float tmp128 = left1_4351_43right_f32_f32(tmp126, tmp127);
        float tmp129 = 0.07999999821186065673828125;
        float tmp130 = left1_4321_43right_f32_f32(progress, tmp129);
        class_tmp122._m2 = left1_4331_43right_f32_f32(tmp128, tmp130);
        class_8 motion = class_tmp122;
        float tmp135 = 2.2000000476837158203125;
        float tmp136 = left1_4321_43right_f32_f32(screen._m0, tmp135);
        float tmp137 = 0.02099999971687793731689453125;
        float tmp138 = left1_4321_43right_f32_f32(time, tmp137);
        class_6 class_tmp133 = class_6(0.0, 0.0);
        class_tmp133._m0 = left1_4331_43right_f32_f32(tmp136, tmp138);
        float tmp141 = 2.2000000476837158203125;
        float tmp142 = left1_4321_43right_f32_f32(screen._m1, tmp141);
        float tmp143 = 0.01400000043213367462158203125;
        float tmp144 = left1_4321_43right_f32_f32(time, tmp143);
        class_tmp133._m1 = left1_4351_43right_f32_f32(tmp142, tmp144);
        class_6 tmp147 = class_tmp133;
        float tmp148 = 1.89999997615814208984375;
        float chamber = the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp147, tmp148);
        float tmp151 = 4.099999904632568359375;
        float tmp152 = left1_4321_43right_f32_f32(screen._m0, tmp151);
        float tmp153 = 0.0170000009238719940185546875;
        float tmp154 = left1_4321_43right_f32_f32(time, tmp153);
        class_6 class_tmp149 = class_6(0.0, 0.0);
        class_tmp149._m0 = left1_4351_43right_f32_f32(tmp152, tmp154);
        float tmp157 = 4.099999904632568359375;
        float tmp158 = left1_4321_43right_f32_f32(screen._m1, tmp157);
        float tmp159 = 0.010999999940395355224609375;
        float tmp160 = left1_4321_43right_f32_f32(time, tmp159);
        class_tmp149._m1 = left1_4331_43right_f32_f32(tmp158, tmp160);
        class_6 tmp163 = class_tmp149;
        float tmp164 = 5.400000095367431640625;
        float ridges = the_ridged_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp163, tmp164);
        float tmp167 = 27.0;
        float tmp168 = left1_4321_43right_f32_f32(screen._m0, tmp167);
        float tmp169 = 0.23999999463558197021484375;
        float tmp170 = left1_4321_43right_f32_f32(time, tmp169);
        class_6 class_tmp165 = class_6(0.0, 0.0);
        class_tmp165._m0 = left1_4331_43right_f32_f32(tmp168, tmp170);
        float tmp173 = 27.0;
        float tmp174 = left1_4321_43right_f32_f32(screen._m1, tmp173);
        float tmp175 = 0.12999999523162841796875;
        float tmp176 = left1_4321_43right_f32_f32(time, tmp175);
        class_tmp165._m1 = left1_4351_43right_f32_f32(tmp174, tmp176);
        class_6 tmp179 = class_tmp165;
        float tmp180 = 9.69999980926513671875;
        float drones = the_spark_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp179, tmp180);
        float tmp183 = 7.0;
        float tmp184 = left1_4321_43right_f32_f32(screen._m0, tmp183);
        float tmp185 = 8.3999996185302734375;
        float tmp186 = left1_4321_43right_f32_f32(time, tmp185);
        class_6 class_tmp181 = class_6(0.0, 0.0);
        class_tmp181._m0 = left1_4331_43right_f32_f32(tmp184, tmp186);
        float tmp189 = 46.0;
        class_tmp181._m1 = left1_4321_43right_f32_f32(screen._m1, tmp189);
        class_6 tmp192 = class_tmp181;
        float tmp193 = 14.19999980926513671875;
        float particles = the_spark_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp192, tmp193);
        float tmp194 = 0.0;
        float tmp195 = 0.660000026226043701171875;
        float tmp197 = the_absolute_value_of_magnitude_f32(screen._m1);
        float core = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp194, tmp195, tmp197);
        float tmp198 = left1_4321_43right_f32_f32(particles, core);
        float trails = left1_4321_43right_f32_f32(tmp198, motorcycle);
        float tmp200 = 0.0030000000260770320892333984375;
        float tmp201 = 0.01200000010430812835693359375;
        float tmp202 = left1_4321_43right_f32_f32(chamber, tmp201);
        float tmp203 = left1_4331_43right_f32_f32(tmp200, tmp202);
        float tmp204 = 0.017999999225139617919921875;
        float tmp205 = left1_4321_43right_f32_f32(ridges, tmp204);
        float tmp206 = left1_4331_43right_f32_f32(tmp203, tmp205);
        float tmp207 = 0.180000007152557373046875;
        float tmp208 = left1_4321_43right_f32_f32(drones, tmp207);
        float tmp209 = left1_4331_43right_f32_f32(tmp206, tmp208);
        float tmp210 = 0.319999992847442626953125;
        float tmp211 = left1_4321_43right_f32_f32(trails, tmp210);
        class_8 class_tmp199 = class_8(0.0, 0.0, 0.0);
        class_tmp199._m0 = left1_4331_43right_f32_f32(tmp209, tmp211);
        float tmp213 = 0.006000000052154064178466796875;
        float tmp214 = 0.0240000002086162567138671875;
        float tmp215 = left1_4321_43right_f32_f32(chamber, tmp214);
        float tmp216 = left1_4331_43right_f32_f32(tmp213, tmp215);
        float tmp217 = 0.014999999664723873138427734375;
        float tmp218 = left1_4321_43right_f32_f32(ridges, tmp217);
        float tmp219 = left1_4331_43right_f32_f32(tmp216, tmp218);
        float tmp220 = 0.4600000083446502685546875;
        float tmp221 = left1_4321_43right_f32_f32(drones, tmp220);
        float tmp222 = left1_4331_43right_f32_f32(tmp219, tmp221);
        float tmp223 = 0.7799999713897705078125;
        float tmp224 = left1_4321_43right_f32_f32(trails, tmp223);
        class_tmp199._m1 = left1_4331_43right_f32_f32(tmp222, tmp224);
        float tmp226 = 0.0240000002086162567138671875;
        float tmp227 = 0.08200000226497650146484375;
        float tmp228 = left1_4321_43right_f32_f32(chamber, tmp227);
        float tmp229 = left1_4331_43right_f32_f32(tmp226, tmp228);
        float tmp230 = 0.071000002324581146240234375;
        float tmp231 = left1_4321_43right_f32_f32(ridges, tmp230);
        float tmp232 = left1_4331_43right_f32_f32(tmp229, tmp231);
        float tmp233 = 1.019999980926513671875;
        float tmp234 = left1_4321_43right_f32_f32(drones, tmp233);
        float tmp235 = left1_4331_43right_f32_f32(tmp232, tmp234);
        float tmp236 = 1.46000003814697265625;
        float tmp237 = left1_4321_43right_f32_f32(trails, tmp236);
        class_tmp199._m2 = left1_4331_43right_f32_f32(tmp235, tmp237);
        class_8 color83 = class_tmp199;
        class_8 class_tmp241 = class_8(0.0, 0.0, 0.0);
        class_tmp241._m0 = 0.0;
        class_tmp241._m1 = 0.0;
        class_tmp241._m2 = 0.819999992847442626953125;
        class_3 class_tmp240 = class_3(class_8(0.0, 0.0, 0.0), 0.0);
        class_tmp240._m0 = class_tmp241;
        class_tmp240._m1 = 1.2400000095367431640625;
        class_3 bound = class_tmp240;
        float tmp251 = 3.5;
        bool tmp252 = left_1is_greater_than_or_equal_to4213_right_f32_f32(moment, tmp251);
        float tmp253 = 7.400000095367431640625;
        bool tmp254 = left_0_right_f32_f32(moment, tmp253);
        if (_boolean8left5_and_3boolean8right5_bool_bool(tmp252, tmp254))
        {
            float tmp256 = 0.0;
            float tmp257 = 1.2599999904632568359375;
            float tmp258 = left1_4351_43right_f32_f32(tmp256, tmp257);
            float tmp259 = 1.17999994754791259765625;
            float tmp260 = left1_4321_43right_f32_f32(progress, tmp259);
            class_8 class_tmp255 = class_8(0.0, 0.0, 0.0);
            class_tmp255._m0 = left1_4331_43right_f32_f32(tmp258, tmp260);
            float tmp262 = 0.0;
            float tmp263 = 0.189999997615814208984375;
            float tmp264 = left1_4351_43right_f32_f32(tmp262, tmp263);
            float tmp265 = 0.119999997317790985107421875;
            float tmp266 = left1_4321_43right_f32_f32(progress, tmp265);
            class_tmp255._m1 = left1_4331_43right_f32_f32(tmp264, tmp266);
            float tmp268 = 2.7999999523162841796875;
            float tmp269 = 4.900000095367431640625;
            float tmp270 = left1_4321_43right_f32_f32(progress, tmp269);
            class_tmp255._m2 = left1_4351_43right_f32_f32(tmp268, tmp270);
            bound._m0 = class_tmp255;
            bound._m1 = 1.36000001430511474609375;
        }
        class_8 class_tmp275 = class_8(0.0, 0.0, 0.0);
        class_tmp275._m0 = left1_4351_43right_f32_f32(bound._m0._m0, camera._m0);
        class_tmp275._m1 = left1_4351_43right_f32_f32(bound._m0._m1, camera._m1);
        class_tmp275._m2 = left1_4351_43right_f32_f32(bound._m0._m2, camera._m2);
        class_8 center = class_tmp275;
        float tmp291 = left1_4321_43right_f32_f32(center._m0, ray._m0);
        float tmp294 = left1_4321_43right_f32_f32(center._m1, ray._m1);
        float tmp295 = left1_4331_43right_f32_f32(tmp291, tmp294);
        float tmp298 = left1_4321_43right_f32_f32(center._m2, ray._m2);
        float depth = left1_4331_43right_f32_f32(tmp295, tmp298);
        float tmp302 = left1_4321_43right_f32_f32(ray._m0, depth);
        class_8 class_tmp299 = class_8(0.0, 0.0, 0.0);
        class_tmp299._m0 = left1_4331_43right_f32_f32(camera._m0, tmp302);
        float tmp306 = left1_4321_43right_f32_f32(ray._m1, depth);
        class_tmp299._m1 = left1_4331_43right_f32_f32(camera._m1, tmp306);
        float tmp310 = left1_4321_43right_f32_f32(ray._m2, depth);
        class_tmp299._m2 = left1_4331_43right_f32_f32(camera._m2, tmp310);
        class_8 nearest = class_tmp299;
        class_8 class_tmp313 = class_8(0.0, 0.0, 0.0);
        class_tmp313._m0 = left1_4351_43right_f32_f32(nearest._m0, bound._m0._m0);
        class_tmp313._m1 = left1_4351_43right_f32_f32(nearest._m1, bound._m0._m1);
        class_tmp313._m2 = left1_4351_43right_f32_f32(nearest._m2, bound._m0._m2);
        class_8 miss = class_tmp313;
        float tmp329 = left1_4321_43right_f32_f32(miss._m0, miss._m0);
        float tmp332 = left1_4321_43right_f32_f32(miss._m1, miss._m1);
        float tmp333 = left1_4331_43right_f32_f32(tmp329, tmp332);
        float tmp336 = left1_4321_43right_f32_f32(miss._m2, miss._m2);
        float separation = left1_4331_43right_f32_f32(tmp333, tmp336);
        float radius = left1_4321_43right_f32_f32(bound._m1, bound._m1);
        class_4 class_tmp339 = class_4(0.0, 0.0, 0u, false);
        class_tmp339._m0 = 0.07999999821186065673828125;
        class_tmp339._m1 = 0.07999999821186065673828125;
        class_tmp339._m2 = 0u;
        uint tmp343 = 1u;
        class_tmp339._m3 = value_as_3type8destination5_i32_type_ct_destination_type(tmp343);
        class_4 traversal = class_tmp339;
        float tmp348 = 7.400000095367431640625;
        bool tmp349 = left_1is_greater_than_or_equal_to4213_right_f32_f32(moment, tmp348);
        float tmp350 = 0.0;
        bool tmp351 = left_1is_less_than_or_equal_to4013_right_f32_f32(depth, tmp350);
        bool tmp352 = left_or_right_bool_bool(tmp349, tmp351);
        bool tmp353 = left_1is_greater_than_or_equal_to4213_right_f32_f32(separation, radius);
        if (left_or_right_bool_bool(tmp352, tmp353))
        {
            uint tmp354 = 0u;
            traversal._m3 = value_as_3type8destination5_i32_type_ct_destination_type(tmp354);
        }
        else
        {
            float tmp357 = left1_4351_43right_f32_f32(radius, separation);
            float span = the_square_root_of_value_f32(tmp357);
            float tmp358 = left1_4351_43right_f32_f32(depth, span);
            float tmp359 = 0.07999999821186065673828125;
            traversal._m0 = the_maximum_of_left_and_right_f32_f32(tmp358, tmp359);
            traversal._m1 = left1_4331_43right_f32_f32(depth, span);
        }
        uint tmp363 = 0u;
        class_5 class_tmp362 = class_5(false, class_8(0.0, 0.0, 0.0), 0.0);
        class_tmp362._m0 = value_as_3type8destination5_i32_type_ct_destination_type(tmp363);
        class_tmp362._m1 = camera;
        class_tmp362._m2 = 0.0;
        class_5 surface = class_tmp362;
        uint tmp445 = 0u;
        class_8 class_tmp397 = class_8(0.0, 0.0, 0.0);
        class_8 class_tmp372 = class_8(0.0, 0.0, 0.0);
        bool tmp371 = false;
        bool tmp370 = false;
        uint tmp369 = 0u;
        for (;;)
        {
            tmp369 = 44u;
            tmp370 = left_0_right_i32_i32(traversal._m2, tmp369);
            tmp371 = _ray_traversal8traversal5_continues_ray_traversal(traversal);
            if (_boolean8left5_and_3boolean8right5_bool_bool(tmp370, tmp371))
            {
                float tmp376 = left1_4321_43right_f32_f32(ray._m0, traversal._m0);
                class_tmp372._m0 = left1_4331_43right_f32_f32(camera._m0, tmp376);
                float tmp381 = left1_4321_43right_f32_f32(ray._m1, traversal._m0);
                class_tmp372._m1 = left1_4331_43right_f32_f32(camera._m1, tmp381);
                float tmp386 = left1_4321_43right_f32_f32(ray._m2, traversal._m0);
                class_tmp372._m2 = left1_4331_43right_f32_f32(camera._m2, tmp386);
                class_8 _sample = class_tmp372;
                float _distance = 12.0;
                float tmp391 = 3.5;
                if (left_0_right_f32_f32(moment, tmp391))
                {
                    float tmp392 = 0.7200000286102294921875;
                    float yaw = left1_4321_43right_f32_f32(time, tmp392);
                    float tmp393 = 0.4699999988079071044921875;
                    float tmp394 = left1_4321_43right_f32_f32(time, tmp393);
                    float tmp395 = the_sine_of_value_f32(tmp394);
                    float tmp396 = 0.519999980926513671875;
                    float pitch = left1_4321_43right_f32_f32(tmp395, tmp396);
                    class_tmp397._m0 = _sample._m0;
                    class_tmp397._m1 = _sample._m1;
                    float tmp405 = 0.819999992847442626953125;
                    class_tmp397._m2 = left1_4351_43right_f32_f32(_sample._m2, tmp405);
                    class_8 location = class_tmp397;
                    _distance = the_crystal_distance_at_3spatial_coordinate8position5_rotated_by_3float8yaw5_and_3float8pitch5_spatial_coordinate_f32_f32(location, yaw, pitch);
                }
                else
                {
                    float _1124 = the_motorcycle_distance_at_3spatial_coordinate8position5_with_3motorcycle_motion8motion5_f32_spatial_coordinate_motorcycle_motion(_distance, _sample, motion);
                    _distance = _1124;
                }
                float magnitude = the_absolute_value_of_magnitude_f32(_distance);
                float tmp409 = 0.01200000010430812835693359375;
                float tmp410 = 0.115000002086162567138671875;
                float proximity = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp409, tmp410, magnitude);
                float tmp411 = 1.0;
                float tmp413 = 0.3499999940395355224609375;
                float tmp414 = left1_4351_43right_f32_f32(traversal._m1, tmp413);
                float tmp417 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp414, traversal._m1, traversal._m0);
                float fade = left1_4351_43right_f32_f32(tmp411, tmp417);
                float tmp419 = left1_4321_43right_f32_f32(proximity, fade);
                float tmp420 = 0.0035000001080334186553955078125;
                float tmp421 = left1_4321_43right_f32_f32(tmp419, tmp420);
                surface._m2 = left1_4331_43right_f32_f32(surface._m2, tmp421);
                float tmp425 = 0.0089999996125698089599609375;
                if (left_0_right_f32_f32(magnitude, tmp425))
                {
                    uint tmp426 = 1u;
                    surface._m0 = value_as_3type8destination5_i32_type_ct_destination_type(tmp426);
                    surface._m1 = _sample;
                    uint tmp429 = 0u;
                    traversal._m3 = value_as_3type8destination5_i32_type_ct_destination_type(tmp429);
                }
                else
                {
                    float tmp433 = 0.63999998569488525390625;
                    float tmp434 = left1_4321_43right_f32_f32(magnitude, tmp433);
                    float tmp435 = 0.01400000043213367462158203125;
                    float tmp436 = the_maximum_of_left_and_right_f32_f32(tmp434, tmp435);
                    traversal._m0 = left1_4331_43right_f32_f32(traversal._m0, tmp436);
                    if (left_2_right_f32_f32(traversal._m0, traversal._m1))
                    {
                        uint tmp442 = 0u;
                        traversal._m3 = value_as_3type8destination5_i32_type_ct_destination_type(tmp442);
                    }
                }
                tmp445 = 1u;
                traversal._m2 = left1_4331_43right_i32_i32(traversal._m2, tmp445);
                continue;
            }
            else
            {
                break;
            }
        }
        float light = 0.0;
        if (_surface_result8surface5_was_hit_surface_result(surface))
        {
            class_8 class_tmp450 = class_8(0.0, 0.0, 0.0);
            class_tmp450._m0 = surface._m1._m0;
            class_tmp450._m1 = surface._m1._m1;
            class_tmp450._m2 = surface._m1._m2;
            class_8 tmp463 = class_tmp450;
            float tmp464 = 1.7000000476837158203125;
            float tmp465 = left1_4321_43right_f32_f32(time, tmp464);
            drones = the_nano_drone_field_at_3spatial_shader_point8point5_with_phase_3any8phase5_spatial_shader_point_f32(tmp463, tmp465);
            float tmp468 = 13.0;
            float tmp469 = left1_4321_43right_f32_f32(surface._m1._m2, tmp468);
            float tmp470 = 4.19999980926513671875;
            float tmp471 = left1_4321_43right_f32_f32(time, tmp470);
            float tmp472 = left1_4351_43right_f32_f32(tmp469, tmp471);
            float tmp473 = the_sine_of_value_f32(tmp472);
            float tmp474 = 0.5;
            float tmp475 = left1_4321_43right_f32_f32(tmp473, tmp474);
            float tmp476 = 0.5;
            float pulse = left1_4331_43right_f32_f32(tmp475, tmp476);
            float tmp477 = 0.0;
            float tmp478 = 0.04500000178813934326171875;
            float tmp481 = 2.7000000476837158203125;
            float tmp482 = left1_4321_43right_f32_f32(surface._m1._m1, tmp481);
            float tmp483 = 0.310000002384185791015625;
            float tmp484 = left1_4321_43right_f32_f32(time, tmp483);
            float tmp485 = left1_4351_43right_f32_f32(tmp482, tmp484);
            float tmp486 = the_fractional_part_of_number_f32(tmp485);
            float tmp487 = 0.5;
            float tmp488 = left1_4351_43right_f32_f32(tmp486, tmp487);
            float tmp489 = the_absolute_value_of_magnitude_f32(tmp488);
            float scan449 = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp477, tmp478, tmp489);
            float tmp490 = 1.82000005245208740234375;
            float tmp491 = 1.059999942779541015625;
            float tmp492 = left1_4321_43right_f32_f32(pulse, tmp491);
            float tmp493 = left1_4331_43right_f32_f32(tmp490, tmp492);
            float tmp494 = left1_4321_43right_f32_f32(drones, tmp493);
            float tmp495 = 0.0199999995529651641845703125;
            float tmp496 = left1_4321_43right_f32_f32(scan449, tmp495);
            light = left1_4331_43right_f32_f32(tmp494, tmp496);
        }
        float tmp498 = 1.7999999523162841796875;
        float tmp499 = left1_4321_43right_f32_f32(surface._m2, tmp498);
        float tmp500 = left1_4331_43right_f32_f32(light, tmp499);
        crystal = left1_4321_43right_f32_f32(tmp500, crystal);
        float tmp502 = 2.099999904632568359375;
        float tmp503 = left1_4321_43right_f32_f32(surface._m2, tmp502);
        float tmp504 = left1_4331_43right_f32_f32(light, tmp503);
        motorcycle = left1_4321_43right_f32_f32(tmp504, motorcycle);
        float tmp507 = 5.30000019073486328125;
        float tmp508 = left1_4321_43right_f32_f32(surface._m1._m0, tmp507);
        float tmp511 = 3.7000000476837158203125;
        float tmp512 = left1_4321_43right_f32_f32(surface._m1._m1, tmp511);
        float tmp513 = left1_4331_43right_f32_f32(tmp508, tmp512);
        float tmp516 = 4.099999904632568359375;
        float tmp517 = left1_4321_43right_f32_f32(surface._m1._m2, tmp516);
        float hue = left1_4331_43right_f32_f32(tmp513, tmp517);
        float tmp518 = 0.699999988079071044921875;
        float tmp519 = left1_4321_43right_f32_f32(time, tmp518);
        hue = left1_4351_43right_f32_f32(hue, tmp519);
        float tmp520 = the_sine_of_value_f32(hue);
        float tmp521 = 0.5;
        float tmp522 = left1_4321_43right_f32_f32(tmp520, tmp521);
        float tmp523 = 0.5;
        hue = left1_4331_43right_f32_f32(tmp522, tmp523);
        float tmp525 = 0.36000001430511474609375;
        float tmp526 = left1_4321_43right_f32_f32(crystal, tmp525);
        float tmp527 = left1_4331_43right_f32_f32(color83._m0, tmp526);
        float tmp528 = 0.7200000286102294921875;
        float tmp529 = 0.959999978542327880859375;
        float tmp530 = left1_4321_43right_f32_f32(hue, tmp529);
        float tmp531 = left1_4331_43right_f32_f32(tmp528, tmp530);
        float tmp532 = left1_4321_43right_f32_f32(motorcycle, tmp531);
        color83._m0 = left1_4331_43right_f32_f32(tmp527, tmp532);
        float tmp535 = 0.7799999713897705078125;
        float tmp536 = left1_4321_43right_f32_f32(crystal, tmp535);
        float tmp537 = left1_4331_43right_f32_f32(color83._m1, tmp536);
        float tmp538 = 0.920000016689300537109375;
        float tmp539 = 0.579999983310699462890625;
        float tmp540 = left1_4321_43right_f32_f32(hue, tmp539);
        float tmp541 = left1_4351_43right_f32_f32(tmp538, tmp540);
        float tmp542 = left1_4321_43right_f32_f32(motorcycle, tmp541);
        color83._m1 = left1_4331_43right_f32_f32(tmp537, tmp542);
        float tmp545 = 1.46000003814697265625;
        float tmp546 = left1_4321_43right_f32_f32(crystal, tmp545);
        float tmp547 = left1_4331_43right_f32_f32(color83._m2, tmp546);
        float tmp548 = 1.17999994754791259765625;
        float tmp549 = 0.2800000011920928955078125;
        float tmp550 = left1_4321_43right_f32_f32(hue, tmp549);
        float tmp551 = left1_4351_43right_f32_f32(tmp548, tmp550);
        float tmp552 = left1_4321_43right_f32_f32(motorcycle, tmp551);
        color83._m2 = left1_4331_43right_f32_f32(tmp547, tmp552);
        float tmp554 = 0.2800000011920928955078125;
        float tmp555 = 1.2599999904632568359375;
        float aura = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp554, tmp555, radial);
        float tmp557 = left1_4321_43right_f32_f32(aura, vitruvian);
        float tmp558 = 0.017999999225139617919921875;
        float tmp559 = left1_4321_43right_f32_f32(tmp557, tmp558);
        color83._m0 = left1_4331_43right_f32_f32(color83._m0, tmp559);
        float tmp562 = left1_4321_43right_f32_f32(aura, vitruvian);
        float tmp563 = 0.05200000107288360595703125;
        float tmp564 = left1_4321_43right_f32_f32(tmp562, tmp563);
        color83._m1 = left1_4331_43right_f32_f32(color83._m1, tmp564);
        float tmp567 = left1_4321_43right_f32_f32(aura, vitruvian);
        float tmp568 = 0.119999997317790985107421875;
        float tmp569 = left1_4321_43right_f32_f32(tmp567, tmp568);
        color83._m2 = left1_4331_43right_f32_f32(color83._m2, tmp569);
        float tmp571 = 0.180000007152557373046875;
        float tmp572 = 1.0;
        float tmp573 = 0.519999980926513671875;
        float tmp574 = 1.62000000476837158203125;
        float tmp575 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp573, tmp574, radial);
        float tmp576 = left1_4351_43right_f32_f32(tmp572, tmp575);
        float tmp577 = 0.819999992847442626953125;
        float tmp578 = left1_4321_43right_f32_f32(tmp576, tmp577);
        float vignette = left1_4331_43right_f32_f32(tmp571, tmp578);
        float tmp580 = left1_4321_43right_f32_f32(color83._m0, vignette);
        float tmp581 = 1.0;
        float tmp583 = left1_4331_43right_f32_f32(tmp581, color83._m0);
        float tmp584 = left1_4371_43right_f32_f32(tmp580, tmp583);
        color83._m0 = the_square_root_of_value_f32(tmp584);
        float tmp587 = left1_4321_43right_f32_f32(color83._m1, vignette);
        float tmp588 = 1.0;
        float tmp590 = left1_4331_43right_f32_f32(tmp588, color83._m1);
        float tmp591 = left1_4371_43right_f32_f32(tmp587, tmp590);
        color83._m1 = the_square_root_of_value_f32(tmp591);
        float tmp594 = left1_4321_43right_f32_f32(color83._m2, vignette);
        float tmp595 = 1.0;
        float tmp597 = left1_4331_43right_f32_f32(tmp595, color83._m2);
        float tmp598 = left1_4371_43right_f32_f32(tmp594, tmp597);
        color83._m2 = the_square_root_of_value_f32(tmp598);
        vec4 _1101 = vec4(0.0, 0.0, 0.0, 1.0);
        _1101.z = color83._m2;
        _1101.y = color83._m1;
        _1101.x = color83._m0;
        dynlexColor = _1101;
    }
}
