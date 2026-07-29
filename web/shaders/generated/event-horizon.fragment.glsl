#version 300 es
precision highp float;
precision highp int;

struct class_1
{
    float _m0;
    float _m1;
};

struct class_0
{
    float _m0;
    float _m1;
    float _m2;
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

float the_sine_of_value_f32(float value)
{
    return sin(value);
}

float the_cosine_of_value_f32(float value)
{
    return cos(value);
}

float the_square_root_of_value_f32(float value)
{
    return sqrt(value);
}

float the_minimum_of_left_and_right_f32_f32(float left, float right)
{
    return isnan(right) ? left : (isnan(left) ? right : min(left, right));
}

float _the_negative_of_4the_opposite_of_453value_f32(float value)
{
    return -value;
}

float the_signed_flow_at_3shader_point8point5_during_3any8phase5_shader_point_f32(class_1 point, float phase)
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

float the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(class_1 point, float phase)
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

float the_ridged_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(class_1 point, float phase)
{
    float tmp = the_signed_flow_at_3shader_point8point5_during_3any8phase5_shader_point_f32(point, phase);
    float wave = the_absolute_value_of_magnitude_f32(tmp);
    float tmp1 = 1.0;
    float tmp2 = 1.0;
    float tmp3 = the_minimum_of_left_and_right_f32_f32(wave, tmp2);
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

float the_floor_of_value_f32(float value)
{
    return floor(value);
}

float the_fractional_part_of_number_f32(float number)
{
    float tmp = the_floor_of_value_f32(number);
    return left1_4351_43right_f32_f32(number, tmp);
}

float the_glow_from_inner_to_outer_at_sample_f32_f32_f32(float inner, float outer, float _sample)
{
    float tmp = 1.0;
    float tmp1 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(inner, outer, _sample);
    return left1_4351_43right_f32_f32(tmp, tmp1);
}

float the_spark_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(class_1 point, float phase)
{
    float tmp = 0.189999997615814208984375;
    class_1 class_tmp = class_1(0.0, 0.0);
    class_tmp._m0 = left1_4321_43right_f32_f32(point._m0, tmp);
    float tmp3 = 0.189999997615814208984375;
    class_tmp._m1 = left1_4321_43right_f32_f32(point._m1, tmp3);
    class_1 _sample = class_tmp;
    float warp = the_signed_flow_at_3shader_point8point5_during_3any8phase5_shader_point_f32(_sample, phase);
    float tmp7 = 0.23000000417232513427734375;
    float tmp8 = left1_4321_43right_f32_f32(point._m0, tmp7);
    float tmp9 = 7.0;
    class_1 class_tmp5 = class_1(0.0, 0.0);
    class_tmp5._m0 = left1_4331_43right_f32_f32(tmp8, tmp9);
    float tmp12 = 0.23000000417232513427734375;
    float tmp13 = left1_4321_43right_f32_f32(point._m1, tmp12);
    float tmp14 = 5.0;
    class_tmp5._m1 = left1_4351_43right_f32_f32(tmp13, tmp14);
    class_1 shifted = class_tmp5;
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
    class_1 class_tmp48 = class_1(0.0, 0.0);
    class_tmp48._m0 = left1_4321_43right_f32_f32(longitude, tmp49);
    float tmp51 = 0.10999999940395355224609375;
    class_tmp48._m1 = left1_4321_43right_f32_f32(latitude, tmp51);
    class_1 location = class_tmp48;
    float tmp54 = 4.0;
    float tmp55 = left1_4331_43right_f32_f32(phase, tmp54);
    float rarity = the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(location, tmp55);
    float tmp56 = 0.4799999892711639404296875;
    float tmp57 = 0.819999992847442626953125;
    float tmp58 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp56, tmp57, rarity);
    return left1_4321_43right_f32_f32(brightness, tmp58);
}

float a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3float8scale5_during_3float8phase5_at_3float8time5_planar_coordinate_f32_f32_f32(class_1 position, float scale, float phase, float time)
{
    float tmp = 0.12999999523162841796875;
    float tmp1 = left1_4321_43right_f32_f32(time, tmp);
    float tmp2 = left1_4331_43right_f32_f32(tmp1, phase);
    float depth = the_fractional_part_of_number_f32(tmp2);
    float tmp3 = 0.4199999868869781494140625;
    float tmp4 = left1_4321_43right_f32_f32(depth, depth);
    float tmp5 = 3.599999904632568359375;
    float tmp6 = left1_4321_43right_f32_f32(tmp4, tmp5);
    float expansion = left1_4331_43right_f32_f32(tmp3, tmp6);
    float tmp7 = left1_4371_43right_f32_f32(position._m0, expansion);
    class_1 class_tmp = class_1(0.0, 0.0);
    class_tmp._m0 = left1_4321_43right_f32_f32(tmp7, scale);
    float tmp10 = left1_4371_43right_f32_f32(position._m1, expansion);
    class_tmp._m1 = left1_4321_43right_f32_f32(tmp10, scale);
    class_1 _sample = class_tmp;
    class_1 class_tmp12 = class_1(0.0, 0.0);
    class_tmp12._m0 = _sample._m0;
    class_tmp12._m1 = _sample._m1;
    class_1 tmp18 = class_tmp12;
    float tmp19 = 13.69999980926513671875;
    float tmp20 = left1_4321_43right_f32_f32(phase, tmp19);
    float points = the_spark_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp18, tmp20);
    float tmp23 = 0.3300000131130218505859375;
    class_1 class_tmp21 = class_1(0.0, 0.0);
    class_tmp21._m0 = left1_4321_43right_f32_f32(_sample._m0, tmp23);
    class_tmp21._m1 = _sample._m1;
    class_1 tmp29 = class_tmp21;
    float tmp30 = 13.69999980926513671875;
    float tmp31 = left1_4321_43right_f32_f32(phase, tmp30);
    float tmp32 = 0.0900000035762786865234375;
    float tmp33 = left1_4331_43right_f32_f32(tmp31, tmp32);
    float horizontal = the_spark_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp29, tmp33);
    class_1 class_tmp34 = class_1(0.0, 0.0);
    class_tmp34._m0 = _sample._m0;
    float tmp39 = 0.3300000131130218505859375;
    class_tmp34._m1 = left1_4321_43right_f32_f32(_sample._m1, tmp39);
    class_1 tmp42 = class_tmp34;
    float tmp43 = 13.69999980926513671875;
    float tmp44 = left1_4321_43right_f32_f32(phase, tmp43);
    float tmp45 = 0.10999999940395355224609375;
    float tmp46 = left1_4351_43right_f32_f32(tmp44, tmp45);
    float vertical = the_spark_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp42, tmp46);
    float tmp49 = left1_4321_43right_f32_f32(position._m0, position._m0);
    float tmp52 = left1_4321_43right_f32_f32(position._m1, position._m1);
    float tmp53 = left1_4331_43right_f32_f32(tmp49, tmp52);
    float radius = the_square_root_of_value_f32(tmp53);
    float tmp54 = left1_4331_43right_f32_f32(horizontal, vertical);
    float tmp55 = 0.0;
    float tmp56 = 1.35000002384185791015625;
    float tmp57 = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp55, tmp56, radius);
    float streaks = left1_4321_43right_f32_f32(tmp54, tmp57);
    float tmp58 = 2.7000000476837158203125;
    float tmp59 = left1_4321_43right_f32_f32(time, tmp58);
    float tmp60 = 29.0;
    float tmp61 = left1_4321_43right_f32_f32(phase, tmp60);
    float tmp62 = left1_4331_43right_f32_f32(tmp59, tmp61);
    float tmp63 = the_sine_of_value_f32(tmp62);
    float tmp64 = 0.12999999523162841796875;
    float tmp65 = left1_4321_43right_f32_f32(tmp63, tmp64);
    float tmp66 = 0.87000000476837158203125;
    float twinkle = left1_4331_43right_f32_f32(tmp65, tmp66);
    float tmp67 = 2.7999999523162841796875;
    float tmp68 = left1_4321_43right_f32_f32(points, tmp67);
    float tmp69 = 0.17000000178813934326171875;
    float tmp70 = left1_4321_43right_f32_f32(streaks, tmp69);
    float tmp71 = left1_4331_43right_f32_f32(tmp68, tmp70);
    float tmp72 = 0.2800000011920928955078125;
    float tmp73 = 1.25;
    float tmp74 = left1_4321_43right_f32_f32(depth, tmp73);
    float tmp75 = left1_4331_43right_f32_f32(tmp72, tmp74);
    float tmp76 = left1_4321_43right_f32_f32(tmp71, tmp75);
    return left1_4321_43right_f32_f32(tmp76, twinkle);
}

void main()
{
    class_1 class_tmp = class_1(0.0, 0.0);
    class_tmp._m0 = gl_FragCoord.x;
    class_tmp._m1 = gl_FragCoord.y;
    class_1 pixel = class_tmp;
    float time = dynlexUniform0.value;
    float tmp = dynlexUniform1.value;
    float tmp5 = 1.0;
    class_1 class_tmp4 = class_1(0.0, 0.0);
    class_tmp4._m0 = the_maximum_of_left_and_right_f32_f32(tmp, tmp5);
    float tmp7 = dynlexUniform2.value;
    float tmp8 = 1.0;
    class_tmp4._m1 = the_maximum_of_left_and_right_f32_f32(tmp7, tmp8);
    class_1 frame = class_tmp4;
    float aspect = left1_4371_43right_f32_f32(frame._m0, frame._m1);
    float tmp16 = left1_4371_43right_f32_f32(pixel._m0, frame._m0);
    float tmp17 = 2.0;
    float tmp18 = left1_4321_43right_f32_f32(tmp16, tmp17);
    float tmp19 = 1.0;
    float tmp20 = left1_4351_43right_f32_f32(tmp18, tmp19);
    class_1 class_tmp13 = class_1(0.0, 0.0);
    class_tmp13._m0 = left1_4321_43right_f32_f32(tmp20, aspect);
    float tmp24 = left1_4371_43right_f32_f32(pixel._m1, frame._m1);
    float tmp25 = 2.0;
    float tmp26 = left1_4321_43right_f32_f32(tmp24, tmp25);
    float tmp27 = 1.0;
    class_tmp13._m1 = left1_4351_43right_f32_f32(tmp26, tmp27);
    class_1 screen = class_tmp13;
    float tmp30 = 0.62000000476837158203125;
    float tmp31 = 0.054999999701976776123046875;
    float tmp32 = left1_4321_43right_f32_f32(time, tmp31);
    float tmp33 = left1_4331_43right_f32_f32(tmp30, tmp32);
    float tmp34 = left1_4321_43right_f32_f32(time, time);
    float tmp35 = 0.00179999996908009052276611328125;
    float tmp36 = left1_4321_43right_f32_f32(tmp34, tmp35);
    float flight = left1_4331_43right_f32_f32(tmp33, tmp36);
    float tmp38 = 0.2700000107288360595703125;
    float tmp39 = left1_4321_43right_f32_f32(time, tmp38);
    float tmp40 = the_sine_of_value_f32(tmp39);
    float tmp41 = 0.048000000417232513427734375;
    class_1 class_tmp37 = class_1(0.0, 0.0);
    class_tmp37._m0 = left1_4321_43right_f32_f32(tmp40, tmp41);
    float tmp43 = 0.20999999344348907470703125;
    float tmp44 = left1_4321_43right_f32_f32(time, tmp43);
    float tmp45 = the_cosine_of_value_f32(tmp44);
    float tmp46 = 0.026000000536441802978515625;
    class_tmp37._m1 = left1_4321_43right_f32_f32(tmp45, tmp46);
    class_1 drift = class_tmp37;
    float tmp52 = left1_4331_43right_f32_f32(screen._m0, drift._m0);
    class_1 class_tmp49 = class_1(0.0, 0.0);
    class_tmp49._m0 = left1_4371_43right_f32_f32(tmp52, flight);
    float tmp56 = left1_4331_43right_f32_f32(screen._m1, drift._m1);
    class_tmp49._m1 = left1_4371_43right_f32_f32(tmp56, flight);
    class_1 position = class_tmp49;
    float tmp61 = left1_4321_43right_f32_f32(position._m0, position._m0);
    float tmp64 = left1_4321_43right_f32_f32(position._m1, position._m1);
    float tmp65 = left1_4331_43right_f32_f32(tmp61, tmp64);
    float radius = the_square_root_of_value_f32(tmp65);
    float tmp66 = 0.001000000047497451305389404296875;
    float denominator = the_maximum_of_left_and_right_f32_f32(radius, tmp66);
    float tmp67 = 0.189999997615814208984375;
    float tmp68 = left1_4321_43right_f32_f32(radius, radius);
    float tmp69 = 0.02099999971687793731689453125;
    float tmp70 = left1_4331_43right_f32_f32(tmp68, tmp69);
    float tmp71 = left1_4371_43right_f32_f32(tmp67, tmp70);
    float tmp72 = 0.0350000001490116119384765625;
    float tmp73 = left1_4321_43right_f32_f32(time, tmp72);
    float angle = left1_4331_43right_f32_f32(tmp71, tmp73);
    float tmp74 = 4.599999904632568359375;
    angle = the_minimum_of_left_and_right_f32_f32(angle, tmp74);
    float sine = the_sine_of_value_f32(angle);
    float cosine = the_cosine_of_value_f32(angle);
    float tmp75 = 1.0;
    float tmp76 = 0.119999997317790985107421875;
    float tmp77 = left1_4321_43right_f32_f32(radius, radius);
    float tmp78 = 0.01200000010430812835693359375;
    float tmp79 = left1_4331_43right_f32_f32(tmp77, tmp78);
    float tmp80 = left1_4371_43right_f32_f32(tmp76, tmp79);
    float scale = left1_4331_43right_f32_f32(tmp75, tmp80);
    float tmp83 = left1_4321_43right_f32_f32(position._m0, cosine);
    float tmp85 = left1_4321_43right_f32_f32(position._m1, sine);
    float tmp86 = left1_4351_43right_f32_f32(tmp83, tmp85);
    class_1 class_tmp81 = class_1(0.0, 0.0);
    class_tmp81._m0 = left1_4321_43right_f32_f32(tmp86, scale);
    float tmp89 = left1_4321_43right_f32_f32(position._m0, sine);
    float tmp91 = left1_4321_43right_f32_f32(position._m1, cosine);
    float tmp92 = left1_4331_43right_f32_f32(tmp89, tmp91);
    class_tmp81._m1 = left1_4321_43right_f32_f32(tmp92, scale);
    class_1 universe = class_tmp81;
    float tmp97 = 1.7999999523162841796875;
    float tmp98 = left1_4321_43right_f32_f32(universe._m0, tmp97);
    float tmp99 = 0.02500000037252902984619140625;
    float tmp100 = left1_4321_43right_f32_f32(time, tmp99);
    class_1 class_tmp95 = class_1(0.0, 0.0);
    class_tmp95._m0 = left1_4331_43right_f32_f32(tmp98, tmp100);
    float tmp103 = 1.7999999523162841796875;
    float tmp104 = left1_4321_43right_f32_f32(universe._m1, tmp103);
    float tmp105 = 4.0;
    class_tmp95._m1 = left1_4351_43right_f32_f32(tmp104, tmp105);
    class_1 tmp108 = class_tmp95;
    float tmp109 = 1.7000000476837158203125;
    float primary = the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp108, tmp109);
    float tmp112 = 4.599999904632568359375;
    float tmp113 = left1_4321_43right_f32_f32(universe._m0, tmp112);
    float tmp114 = 11.0;
    class_1 class_tmp110 = class_1(0.0, 0.0);
    class_tmp110._m0 = left1_4351_43right_f32_f32(tmp113, tmp114);
    float tmp117 = 4.599999904632568359375;
    float tmp118 = left1_4321_43right_f32_f32(universe._m1, tmp117);
    float tmp119 = 0.017999999225139617919921875;
    float tmp120 = left1_4321_43right_f32_f32(time, tmp119);
    class_tmp110._m1 = left1_4331_43right_f32_f32(tmp118, tmp120);
    class_1 tmp123 = class_tmp110;
    float tmp124 = 5.30000019073486328125;
    float secondary = the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp123, tmp124);
    float tmp127 = 2.400000095367431640625;
    float tmp128 = left1_4321_43right_f32_f32(universe._m0, tmp127);
    float tmp129 = 7.0;
    class_1 class_tmp125 = class_1(0.0, 0.0);
    class_tmp125._m0 = left1_4331_43right_f32_f32(tmp128, tmp129);
    float tmp132 = 2.400000095367431640625;
    float tmp133 = left1_4321_43right_f32_f32(universe._m1, tmp132);
    float tmp134 = 9.0;
    class_tmp125._m1 = left1_4351_43right_f32_f32(tmp133, tmp134);
    class_1 tmp137 = class_tmp125;
    float tmp138 = 0.04500000178813934326171875;
    float tmp139 = left1_4321_43right_f32_f32(time, tmp138);
    float ridge = the_ridged_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp137, tmp139);
    float tmp141 = 2.7000000476837158203125;
    float tmp142 = left1_4321_43right_f32_f32(universe._m0, tmp141);
    float tmp144 = 3.599999904632568359375;
    float tmp145 = left1_4321_43right_f32_f32(universe._m1, tmp144);
    float tmp146 = left1_4351_43right_f32_f32(tmp142, tmp145);
    float tmp147 = 8.0;
    float tmp148 = left1_4321_43right_f32_f32(primary, tmp147);
    float phase = left1_4331_43right_f32_f32(tmp146, tmp148);
    float tmp149 = the_sine_of_value_f32(phase);
    float tmp150 = 0.5;
    float tmp151 = left1_4321_43right_f32_f32(tmp149, tmp150);
    float tmp152 = 0.5;
    float aurora = left1_4331_43right_f32_f32(tmp151, tmp152);
    float tmp153 = 0.449999988079071044921875;
    float tmp154 = 0.86000001430511474609375;
    float cloud = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp153, tmp154, primary);
    float tmp155 = 0.519999980926513671875;
    float tmp156 = 0.89999997615814208984375;
    float dust = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp155, tmp156, secondary);
    float tmp158 = 0.00200000009499490261077880859375;
    float tmp159 = 0.02199999988079071044921875;
    float tmp160 = left1_4321_43right_f32_f32(cloud, tmp159);
    float tmp161 = left1_4331_43right_f32_f32(tmp158, tmp160);
    float tmp162 = left1_4321_43right_f32_f32(ridge, aurora);
    float tmp163 = 0.07500000298023223876953125;
    float tmp164 = left1_4321_43right_f32_f32(tmp162, tmp163);
    class_0 class_tmp157 = class_0(0.0, 0.0, 0.0);
    class_tmp157._m0 = left1_4331_43right_f32_f32(tmp161, tmp164);
    float tmp166 = 0.0040000001899898052215576171875;
    float tmp167 = 0.0280000008642673492431640625;
    float tmp168 = left1_4321_43right_f32_f32(cloud, tmp167);
    float tmp169 = left1_4331_43right_f32_f32(tmp166, tmp168);
    float tmp170 = left1_4321_43right_f32_f32(dust, aurora);
    float tmp171 = 0.05200000107288360595703125;
    float tmp172 = left1_4321_43right_f32_f32(tmp170, tmp171);
    class_tmp157._m1 = left1_4331_43right_f32_f32(tmp169, tmp172);
    float tmp174 = 0.01400000043213367462158203125;
    float tmp175 = 0.12999999523162841796875;
    float tmp176 = left1_4321_43right_f32_f32(cloud, tmp175);
    float tmp177 = left1_4331_43right_f32_f32(tmp174, tmp176);
    float tmp178 = 1.0;
    float tmp179 = left1_4351_43right_f32_f32(tmp178, aurora);
    float tmp180 = left1_4321_43right_f32_f32(dust, tmp179);
    float tmp181 = 0.23999999463558197021484375;
    float tmp182 = left1_4321_43right_f32_f32(tmp180, tmp181);
    class_tmp157._m2 = left1_4331_43right_f32_f32(tmp177, tmp182);
    class_0 color = class_tmp157;
    float tmp185 = 13.0;
    float tmp186 = 0.0900000035762786865234375;
    float faraway = a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3float8scale5_during_3float8phase5_at_3float8time5_planar_coordinate_f32_f32_f32(universe, tmp185, tmp186, time);
    float tmp187 = 21.0;
    float tmp188 = 0.4099999964237213134765625;
    float middle = a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3float8scale5_during_3float8phase5_at_3float8time5_planar_coordinate_f32_f32_f32(universe, tmp187, tmp188, time);
    float tmp189 = 31.0;
    float tmp190 = 0.769999980926513671875;
    float nearby = a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3float8scale5_during_3float8phase5_at_3float8time5_planar_coordinate_f32_f32_f32(universe, tmp189, tmp190, time);
    float tmp191 = 0.4600000083446502685546875;
    float tmp192 = left1_4321_43right_f32_f32(faraway, tmp191);
    float tmp193 = 0.7599999904632568359375;
    float tmp194 = left1_4321_43right_f32_f32(middle, tmp193);
    float tmp195 = left1_4331_43right_f32_f32(tmp192, tmp194);
    float tmp196 = 1.13999998569488525390625;
    float tmp197 = left1_4321_43right_f32_f32(nearby, tmp196);
    float stars = left1_4331_43right_f32_f32(tmp195, tmp197);
    float tmp200 = 5.099999904632568359375;
    class_1 class_tmp198 = class_1(0.0, 0.0);
    class_tmp198._m0 = left1_4321_43right_f32_f32(universe._m0, tmp200);
    float tmp203 = 5.099999904632568359375;
    class_tmp198._m1 = left1_4321_43right_f32_f32(universe._m1, tmp203);
    class_1 tmp206 = class_tmp198;
    float tmp207 = 8.19999980926513671875;
    float temperature = the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp206, tmp207);
    float tmp209 = 0.579999983310699462890625;
    float tmp210 = 0.62000000476837158203125;
    float tmp211 = left1_4321_43right_f32_f32(temperature, tmp210);
    float tmp212 = left1_4331_43right_f32_f32(tmp209, tmp211);
    float tmp213 = left1_4321_43right_f32_f32(stars, tmp212);
    color._m0 = left1_4331_43right_f32_f32(color._m0, tmp213);
    float tmp216 = 0.699999988079071044921875;
    float tmp217 = 0.310000002384185791015625;
    float tmp218 = left1_4321_43right_f32_f32(temperature, tmp217);
    float tmp219 = left1_4331_43right_f32_f32(tmp216, tmp218);
    float tmp220 = left1_4321_43right_f32_f32(stars, tmp219);
    color._m1 = left1_4331_43right_f32_f32(color._m1, tmp220);
    float tmp223 = 1.25;
    float tmp224 = 0.180000007152557373046875;
    float tmp225 = left1_4321_43right_f32_f32(temperature, tmp224);
    float tmp226 = left1_4351_43right_f32_f32(tmp223, tmp225);
    float tmp227 = left1_4321_43right_f32_f32(stars, tmp226);
    color._m2 = left1_4331_43right_f32_f32(color._m2, tmp227);
    float tmp229 = 0.119999997317790985107421875;
    float tmp230 = 0.17000000178813934326171875;
    float tmp231 = left1_4321_43right_f32_f32(time, tmp230);
    float tmp232 = the_sine_of_value_f32(tmp231);
    float tmp233 = 0.017999999225139617919921875;
    float tmp234 = left1_4321_43right_f32_f32(tmp232, tmp233);
    float tilt = left1_4331_43right_f32_f32(tmp229, tmp234);
    sine = the_sine_of_value_f32(tilt);
    cosine = the_cosine_of_value_f32(tilt);
    float tmp237 = left1_4321_43right_f32_f32(position._m0, cosine);
    float tmp239 = left1_4321_43right_f32_f32(position._m1, sine);
    class_1 class_tmp235 = class_1(0.0, 0.0);
    class_tmp235._m0 = left1_4331_43right_f32_f32(tmp237, tmp239);
    float tmp242 = left1_4321_43right_f32_f32(position._m1, cosine);
    float tmp244 = left1_4321_43right_f32_f32(position._m0, sine);
    class_tmp235._m1 = left1_4351_43right_f32_f32(tmp242, tmp244);
    class_1 disk = class_tmp235;
    float tmp248 = 7.19999980926513671875;
    float stretched = left1_4321_43right_f32_f32(disk._m1, tmp248);
    float tmp251 = left1_4321_43right_f32_f32(disk._m0, disk._m0);
    float tmp252 = left1_4321_43right_f32_f32(stretched, stretched);
    float tmp253 = left1_4331_43right_f32_f32(tmp251, tmp252);
    float orbit = the_square_root_of_value_f32(tmp253);
    float tmp254 = 0.23000000417232513427734375;
    float tmp255 = 0.2849999964237213134765625;
    float inner = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp254, tmp255, orbit);
    float tmp256 = 1.0;
    float tmp257 = 0.7200000286102294921875;
    float tmp258 = 1.12000000476837158203125;
    float tmp259 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp257, tmp258, orbit);
    float outer = left1_4351_43right_f32_f32(tmp256, tmp259);
    float mask = left1_4321_43right_f32_f32(inner, outer);
    float tmp262 = 8.30000019073486328125;
    float tmp263 = left1_4321_43right_f32_f32(disk._m0, tmp262);
    float tmp264 = 0.519999980926513671875;
    float tmp265 = left1_4321_43right_f32_f32(time, tmp264);
    class_1 class_tmp260 = class_1(0.0, 0.0);
    class_tmp260._m0 = left1_4331_43right_f32_f32(tmp263, tmp265);
    float tmp267 = 1.7999999523162841796875;
    class_tmp260._m1 = left1_4321_43right_f32_f32(stretched, tmp267);
    class_1 tmp270 = class_tmp260;
    float tmp271 = 3.099999904632568359375;
    float turbulence = the_signed_flow_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp270, tmp271);
    float tmp274 = 18.0;
    float tmp275 = left1_4321_43right_f32_f32(disk._m0, tmp274);
    float tmp276 = 0.910000026226043701171875;
    float tmp277 = left1_4321_43right_f32_f32(time, tmp276);
    class_1 class_tmp272 = class_1(0.0, 0.0);
    class_tmp272._m0 = left1_4351_43right_f32_f32(tmp275, tmp277);
    float tmp279 = 3.7000000476837158203125;
    class_tmp272._m1 = left1_4321_43right_f32_f32(stretched, tmp279);
    class_1 tmp282 = class_tmp272;
    float tmp283 = 7.900000095367431640625;
    float detail = the_signed_flow_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp282, tmp283);
    float tmp284 = 39.0;
    float tmp285 = left1_4321_43right_f32_f32(orbit, tmp284);
    float tmp286 = 6.80000019073486328125;
    float tmp287 = left1_4321_43right_f32_f32(time, tmp286);
    float tmp288 = left1_4351_43right_f32_f32(tmp285, tmp287);
    float tmp289 = 7.400000095367431640625;
    float tmp290 = left1_4321_43right_f32_f32(turbulence, tmp289);
    float tmp291 = left1_4331_43right_f32_f32(tmp288, tmp290);
    float tmp292 = 2.2000000476837158203125;
    float tmp293 = left1_4321_43right_f32_f32(detail, tmp292);
    phase = left1_4331_43right_f32_f32(tmp291, tmp293);
    float tmp294 = the_sine_of_value_f32(phase);
    float tmp295 = 0.5;
    float tmp296 = left1_4321_43right_f32_f32(tmp294, tmp295);
    float tmp297 = 0.5;
    float spiral = left1_4331_43right_f32_f32(tmp296, tmp297);
    float tmp298 = 71.0;
    float tmp299 = left1_4321_43right_f32_f32(orbit, tmp298);
    float tmp300 = 10.3999996185302734375;
    float tmp301 = left1_4321_43right_f32_f32(time, tmp300);
    float tmp302 = left1_4351_43right_f32_f32(tmp299, tmp301);
    float tmp303 = 4.0;
    float tmp304 = left1_4321_43right_f32_f32(turbulence, tmp303);
    phase = left1_4351_43right_f32_f32(tmp302, tmp304);
    float tmp305 = the_cosine_of_value_f32(phase);
    float tmp306 = 0.5;
    float tmp307 = left1_4321_43right_f32_f32(tmp305, tmp306);
    float tmp308 = 0.5;
    float braid = left1_4331_43right_f32_f32(tmp307, tmp308);
    float tmp309 = 1.0;
    float tmp310 = 0.2899999916553497314453125;
    float tmp311 = 0.829999983310699462890625;
    float tmp312 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp310, tmp311, orbit);
    float heat = left1_4351_43right_f32_f32(tmp309, tmp312);
    float tmp313 = 0.189999997615814208984375;
    float tmp314 = 0.579999983310699462890625;
    float tmp315 = left1_4321_43right_f32_f32(spiral, tmp314);
    float tmp316 = left1_4331_43right_f32_f32(tmp313, tmp315);
    float tmp317 = 0.23000000417232513427734375;
    float tmp318 = left1_4321_43right_f32_f32(braid, tmp317);
    float tmp319 = left1_4331_43right_f32_f32(tmp316, tmp318);
    float tmp320 = left1_4321_43right_f32_f32(mask, tmp319);
    float tmp321 = 0.550000011920928955078125;
    float tmp322 = 4.400000095367431640625;
    float tmp323 = left1_4321_43right_f32_f32(heat, tmp322);
    float tmp324 = left1_4331_43right_f32_f32(tmp321, tmp323);
    float energy = left1_4321_43right_f32_f32(tmp320, tmp324);
    float tmp326 = 0.001000000047497451305389404296875;
    float tmp327 = the_maximum_of_left_and_right_f32_f32(orbit, tmp326);
    float tmp328 = left1_4371_43right_f32_f32(disk._m0, tmp327);
    float tmp329 = 0.5;
    float tmp330 = left1_4321_43right_f32_f32(tmp328, tmp329);
    float tmp331 = 0.5;
    float tmp332 = left1_4331_43right_f32_f32(tmp330, tmp331);
    float approaching = number_saturated_f32(tmp332);
    float tmp334 = 1.7200000286102294921875;
    float tmp335 = 0.62000000476837158203125;
    float tmp336 = left1_4321_43right_f32_f32(approaching, tmp335);
    float tmp337 = left1_4351_43right_f32_f32(tmp334, tmp336);
    float tmp338 = left1_4321_43right_f32_f32(energy, tmp337);
    color._m0 = left1_4331_43right_f32_f32(color._m0, tmp338);
    float tmp341 = 0.300000011920928955078125;
    float tmp342 = 1.03999996185302734375;
    float tmp343 = left1_4321_43right_f32_f32(approaching, tmp342);
    float tmp344 = left1_4331_43right_f32_f32(tmp341, tmp343);
    float tmp345 = left1_4321_43right_f32_f32(energy, tmp344);
    color._m1 = left1_4331_43right_f32_f32(color._m1, tmp345);
    float tmp348 = 0.054999999701976776123046875;
    float tmp349 = 2.1800000667572021484375;
    float tmp350 = left1_4321_43right_f32_f32(approaching, tmp349);
    float tmp351 = left1_4331_43right_f32_f32(tmp348, tmp350);
    float tmp352 = left1_4321_43right_f32_f32(energy, tmp351);
    color._m2 = left1_4331_43right_f32_f32(color._m2, tmp352);
    float axis = the_absolute_value_of_magnitude_f32(position._m0);
    float tmp355 = 0.0;
    float tmp356 = 0.0320000015199184417724609375;
    float core = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp355, tmp356, axis);
    float tmp357 = 0.017999999225139617919921875;
    float tmp358 = 0.1599999964237213134765625;
    float halo = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp357, tmp358, axis);
    float tmp359 = 0.189999997615814208984375;
    float tmp360 = 0.7200000286102294921875;
    float tmp362 = the_absolute_value_of_magnitude_f32(position._m1);
    float reach = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp359, tmp360, tmp362);
    float tmp365 = 15.0;
    class_1 class_tmp363 = class_1(0.0, 0.0);
    class_tmp363._m0 = left1_4321_43right_f32_f32(position._m0, tmp365);
    float tmp368 = 3.0;
    float tmp369 = left1_4321_43right_f32_f32(position._m1, tmp368);
    float tmp370 = 0.800000011920928955078125;
    float tmp371 = left1_4321_43right_f32_f32(time, tmp370);
    class_tmp363._m1 = left1_4351_43right_f32_f32(tmp369, tmp371);
    class_1 tmp374 = class_tmp363;
    float tmp375 = 10.0;
    float flicker = the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp374, tmp375);
    float tmp376 = 0.519999980926513671875;
    float tmp377 = left1_4321_43right_f32_f32(core, tmp376);
    float tmp378 = 0.12999999523162841796875;
    float tmp379 = left1_4321_43right_f32_f32(halo, tmp378);
    float tmp380 = left1_4331_43right_f32_f32(tmp377, tmp379);
    float tmp381 = left1_4321_43right_f32_f32(tmp380, reach);
    float tmp382 = 0.3499999940395355224609375;
    float tmp383 = 0.64999997615814208984375;
    float tmp384 = left1_4321_43right_f32_f32(flicker, tmp383);
    float tmp385 = left1_4331_43right_f32_f32(tmp382, tmp384);
    float jet = left1_4321_43right_f32_f32(tmp381, tmp385);
    float tmp387 = 0.20999999344348907470703125;
    float tmp388 = left1_4321_43right_f32_f32(jet, tmp387);
    color._m0 = left1_4331_43right_f32_f32(color._m0, tmp388);
    float tmp391 = 0.4799999892711639404296875;
    float tmp392 = left1_4321_43right_f32_f32(jet, tmp391);
    color._m1 = left1_4331_43right_f32_f32(color._m1, tmp392);
    float tmp395 = 1.36000001430511474609375;
    float tmp396 = left1_4321_43right_f32_f32(jet, tmp395);
    color._m2 = left1_4331_43right_f32_f32(color._m2, tmp396);
    float tmp398 = 1.0;
    float tmp399 = 0.17599999904632568359375;
    float tmp400 = 0.20200000703334808349609375;
    float tmp401 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp399, tmp400, radius);
    float horizon = left1_4351_43right_f32_f32(tmp398, tmp401);
    float tmp403 = 1.0;
    float tmp404 = left1_4351_43right_f32_f32(tmp403, horizon);
    color._m0 = left1_4321_43right_f32_f32(color._m0, tmp404);
    float tmp407 = 1.0;
    float tmp408 = left1_4351_43right_f32_f32(tmp407, horizon);
    color._m1 = left1_4321_43right_f32_f32(color._m1, tmp408);
    float tmp411 = 1.0;
    float tmp412 = left1_4351_43right_f32_f32(tmp411, horizon);
    color._m2 = left1_4321_43right_f32_f32(color._m2, tmp412);
    float tmp414 = 1.0;
    float tmp415 = 0.01899999938905239105224609375;
    float tmp416 = _the_negative_of_4the_opposite_of_453value_f32(tmp415);
    float tmp417 = 0.014999999664723873138427734375;
    float tmp419 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp416, tmp417, disk._m1);
    float front = left1_4351_43right_f32_f32(tmp414, tmp419);
    float foreground = left1_4321_43right_f32_f32(energy, front);
    float tmp421 = 1.61000001430511474609375;
    float tmp422 = 0.4799999892711639404296875;
    float tmp423 = left1_4321_43right_f32_f32(approaching, tmp422);
    float tmp424 = left1_4351_43right_f32_f32(tmp421, tmp423);
    float tmp425 = left1_4321_43right_f32_f32(foreground, tmp424);
    color._m0 = left1_4331_43right_f32_f32(color._m0, tmp425);
    float tmp428 = 0.3400000035762786865234375;
    float tmp429 = 0.930000007152557373046875;
    float tmp430 = left1_4321_43right_f32_f32(approaching, tmp429);
    float tmp431 = left1_4331_43right_f32_f32(tmp428, tmp430);
    float tmp432 = left1_4321_43right_f32_f32(foreground, tmp431);
    color._m1 = left1_4331_43right_f32_f32(color._m1, tmp432);
    float tmp435 = 0.070000000298023223876953125;
    float tmp436 = 1.940000057220458984375;
    float tmp437 = left1_4321_43right_f32_f32(approaching, tmp436);
    float tmp438 = left1_4331_43right_f32_f32(tmp435, tmp437);
    float tmp439 = left1_4321_43right_f32_f32(foreground, tmp438);
    color._m2 = left1_4331_43right_f32_f32(color._m2, tmp439);
    float tmp441 = 0.21600000560283660888671875;
    float tmp442 = left1_4351_43right_f32_f32(radius, tmp441);
    float _distance = the_absolute_value_of_magnitude_f32(tmp442);
    float tmp443 = 0.001000000047497451305389404296875;
    float tmp444 = 0.0170000009238719940185546875;
    float ring = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp443, tmp444, _distance);
    float tmp446 = left1_4371_43right_f32_f32(position._m1, denominator);
    float poles = the_absolute_value_of_magnitude_f32(tmp446);
    float tmp447 = 0.1599999964237213134765625;
    float tmp448 = 0.86000001430511474609375;
    float tmp449 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp447, tmp448, poles);
    float arcs = left1_4321_43right_f32_f32(ring, tmp449);
    float tmp450 = 2.099999904632568359375;
    float tmp451 = left1_4321_43right_f32_f32(time, tmp450);
    float tmp452 = 5.0;
    float tmp453 = left1_4321_43right_f32_f32(turbulence, tmp452);
    float tmp454 = left1_4331_43right_f32_f32(tmp451, tmp453);
    float tmp455 = the_sine_of_value_f32(tmp454);
    float tmp456 = 0.100000001490116119384765625;
    float tmp457 = left1_4321_43right_f32_f32(tmp455, tmp456);
    float tmp458 = 0.89999997615814208984375;
    flicker = left1_4331_43right_f32_f32(tmp457, tmp458);
    float tmp460 = 1.34000003337860107421875;
    float tmp461 = left1_4321_43right_f32_f32(ring, tmp460);
    float tmp462 = left1_4321_43right_f32_f32(tmp461, flicker);
    float tmp463 = left1_4331_43right_f32_f32(color._m0, tmp462);
    float tmp464 = 2.2000000476837158203125;
    float tmp465 = left1_4321_43right_f32_f32(arcs, tmp464);
    color._m0 = left1_4331_43right_f32_f32(tmp463, tmp465);
    float tmp468 = 0.7200000286102294921875;
    float tmp469 = left1_4321_43right_f32_f32(ring, tmp468);
    float tmp470 = left1_4321_43right_f32_f32(tmp469, flicker);
    float tmp471 = left1_4331_43right_f32_f32(color._m1, tmp470);
    float tmp472 = 0.930000007152557373046875;
    float tmp473 = left1_4321_43right_f32_f32(arcs, tmp472);
    color._m1 = left1_4331_43right_f32_f32(tmp471, tmp473);
    float tmp476 = 0.439999997615814208984375;
    float tmp477 = left1_4321_43right_f32_f32(ring, tmp476);
    float tmp478 = left1_4321_43right_f32_f32(tmp477, flicker);
    float tmp479 = left1_4331_43right_f32_f32(color._m2, tmp478);
    float tmp480 = 1.480000019073486328125;
    float tmp481 = left1_4321_43right_f32_f32(arcs, tmp480);
    color._m2 = left1_4331_43right_f32_f32(tmp479, tmp481);
    float tmp483 = 0.20200000703334808349609375;
    float tmp484 = left1_4351_43right_f32_f32(radius, tmp483);
    float gap = the_absolute_value_of_magnitude_f32(tmp484);
    float tmp485 = 0.039999999105930328369140625;
    float tmp486 = 0.039999999105930328369140625;
    float tmp487 = left1_4331_43right_f32_f32(gap, tmp486);
    float tmp488 = left1_4371_43right_f32_f32(tmp485, tmp487);
    float tmp489 = 1.0;
    float tmp490 = left1_4351_43right_f32_f32(tmp489, horizon);
    halo = left1_4321_43right_f32_f32(tmp488, tmp490);
    float tmp492 = 0.189999997615814208984375;
    float tmp493 = left1_4321_43right_f32_f32(halo, tmp492);
    color._m0 = left1_4331_43right_f32_f32(color._m0, tmp493);
    float tmp496 = 0.070000000298023223876953125;
    float tmp497 = left1_4321_43right_f32_f32(halo, tmp496);
    color._m1 = left1_4331_43right_f32_f32(color._m1, tmp497);
    float tmp500 = 0.310000002384185791015625;
    float tmp501 = left1_4321_43right_f32_f32(halo, tmp500);
    color._m2 = left1_4331_43right_f32_f32(color._m2, tmp501);
    float tmp504 = left1_4371_43right_f32_f32(screen._m0, aspect);
    float tmp506 = left1_4371_43right_f32_f32(screen._m0, aspect);
    float tmp507 = left1_4321_43right_f32_f32(tmp504, tmp506);
    float tmp510 = left1_4321_43right_f32_f32(screen._m1, screen._m1);
    float tmp511 = left1_4331_43right_f32_f32(tmp507, tmp510);
    float edge = the_square_root_of_value_f32(tmp511);
    float tmp512 = 0.180000007152557373046875;
    float tmp513 = 1.0;
    float tmp514 = 0.4799999892711639404296875;
    float tmp515 = 1.34000003337860107421875;
    float tmp516 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp514, tmp515, edge);
    float tmp517 = left1_4351_43right_f32_f32(tmp513, tmp516);
    float tmp518 = 0.819999992847442626953125;
    float tmp519 = left1_4321_43right_f32_f32(tmp517, tmp518);
    float vignette = left1_4331_43right_f32_f32(tmp512, tmp519);
    float tmp521 = left1_4321_43right_f32_f32(color._m0, vignette);
    float tmp522 = 1.0;
    float tmp524 = left1_4331_43right_f32_f32(tmp522, color._m0);
    float tmp525 = left1_4371_43right_f32_f32(tmp521, tmp524);
    color._m0 = the_square_root_of_value_f32(tmp525);
    float tmp528 = left1_4321_43right_f32_f32(color._m1, vignette);
    float tmp529 = 1.0;
    float tmp531 = left1_4331_43right_f32_f32(tmp529, color._m1);
    float tmp532 = left1_4371_43right_f32_f32(tmp528, tmp531);
    color._m1 = the_square_root_of_value_f32(tmp532);
    float tmp535 = left1_4321_43right_f32_f32(color._m2, vignette);
    float tmp536 = 1.0;
    float tmp538 = left1_4331_43right_f32_f32(tmp536, color._m2);
    float tmp539 = left1_4371_43right_f32_f32(tmp535, tmp538);
    color._m2 = the_square_root_of_value_f32(tmp539);
    vec4 _1117 = vec4(0.0, 0.0, 0.0, 1.0);
    _1117.z = color._m2;
    _1117.y = color._m1;
    _1117.x = color._m0;
    dynlexColor = _1117;
}
