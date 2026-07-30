#version 300 es
precision highp float;
precision highp int;

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

float the_minimum_of_left_and_right_f32_f32(float left, float right)
{
    return isnan(right) ? left : (isnan(left) ? right : min(left, right));
}

bool left_2_right_f32_f32(float left, float right)
{
    return left > right;
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

float the_scene_window_from_opening_to_closing_at_moment_f32_f32_f32(float opening, float closing, float moment)
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

float _the_negative_of_4the_opposite_of_453value_f32(float value)
{
    return -value;
}

float the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_point_f32(_class point, float phase)
{
    float tmp = point._m0.x;
    float tmp1 = 0.730000019073486328125;
    float tmp2 = left1_4321_43right_f32_f32(tmp, tmp1);
    float tmp6 = point._m0.y;
    float tmp7 = 0.4099999964237213134765625;
    float tmp8 = left1_4321_43right_f32_f32(tmp6, tmp7);
    float tmp9 = left1_4351_43right_f32_f32(tmp2, tmp8);
    float tmp10 = left1_4331_43right_f32_f32(tmp9, phase);
    float sway = the_sine_of_value_f32(tmp10);
    float tmp14 = point._m0.x;
    float tmp15 = 0.37000000476837158203125;
    float tmp16 = left1_4321_43right_f32_f32(tmp14, tmp15);
    float tmp20 = point._m0.y;
    float tmp21 = 0.88999998569488525390625;
    float tmp22 = left1_4321_43right_f32_f32(tmp20, tmp21);
    float tmp23 = left1_4331_43right_f32_f32(tmp16, tmp22);
    float tmp24 = 0.709999978542327880859375;
    float tmp25 = left1_4321_43right_f32_f32(phase, tmp24);
    float tmp26 = left1_4351_43right_f32_f32(tmp23, tmp25);
    float drift = the_cosine_of_value_f32(tmp26);
    float tmp30 = point._m0.x;
    float tmp31 = 0.579999983310699462890625;
    float tmp32 = left1_4321_43right_f32_f32(sway, tmp31);
    float longitude = left1_4331_43right_f32_f32(tmp30, tmp32);
    float tmp36 = point._m0.y;
    float tmp37 = 0.579999983310699462890625;
    float tmp38 = left1_4321_43right_f32_f32(drift, tmp37);
    float latitude = left1_4331_43right_f32_f32(tmp36, tmp38);
    float tmp39 = 1.309999942779541015625;
    float tmp40 = left1_4321_43right_f32_f32(longitude, tmp39);
    float tmp41 = 0.87000000476837158203125;
    float tmp42 = left1_4321_43right_f32_f32(latitude, tmp41);
    float tmp43 = left1_4331_43right_f32_f32(tmp40, tmp42);
    float tmp44 = 0.430000007152557373046875;
    float tmp45 = left1_4321_43right_f32_f32(phase, tmp44);
    float tmp46 = left1_4331_43right_f32_f32(tmp43, tmp45);
    float broad = the_sine_of_value_f32(tmp46);
    float tmp47 = 0.790000021457672119140625;
    float tmp48 = _the_negative_of_4the_opposite_of_453value_f32(tmp47);
    float tmp49 = left1_4321_43right_f32_f32(longitude, tmp48);
    float tmp50 = 1.730000019073486328125;
    float tmp51 = left1_4321_43right_f32_f32(latitude, tmp50);
    float tmp52 = left1_4331_43right_f32_f32(tmp49, tmp51);
    float tmp53 = 0.310000002384185791015625;
    float tmp54 = left1_4321_43right_f32_f32(phase, tmp53);
    float tmp55 = left1_4351_43right_f32_f32(tmp52, tmp54);
    float crossing = the_cosine_of_value_f32(tmp55);
    float tmp56 = 2.4700000286102294921875;
    float tmp57 = left1_4321_43right_f32_f32(longitude, tmp56);
    float tmp58 = 2.1099998950958251953125;
    float tmp59 = left1_4321_43right_f32_f32(latitude, tmp58);
    float tmp60 = left1_4351_43right_f32_f32(tmp57, tmp59);
    float tmp61 = 1.7999999523162841796875;
    float tmp62 = left1_4321_43right_f32_f32(broad, tmp61);
    float tmp63 = left1_4331_43right_f32_f32(tmp60, tmp62);
    float curl = the_sine_of_value_f32(tmp63);
    float tmp64 = 4.030000209808349609375;
    float tmp65 = left1_4321_43right_f32_f32(longitude, tmp64);
    float tmp66 = 3.1700000762939453125;
    float tmp67 = left1_4321_43right_f32_f32(latitude, tmp66);
    float tmp68 = left1_4331_43right_f32_f32(tmp65, tmp67);
    float tmp69 = 1.39999997615814208984375;
    float tmp70 = left1_4321_43right_f32_f32(crossing, tmp69);
    float tmp71 = left1_4331_43right_f32_f32(tmp68, tmp70);
    float detail = the_cosine_of_value_f32(tmp71);
    float tmp72 = 0.4600000083446502685546875;
    float tmp73 = left1_4321_43right_f32_f32(broad, tmp72);
    float tmp74 = 0.2899999916553497314453125;
    float tmp75 = left1_4321_43right_f32_f32(crossing, tmp74);
    float tmp76 = left1_4331_43right_f32_f32(tmp73, tmp75);
    float tmp77 = 0.17000000178813934326171875;
    float tmp78 = left1_4321_43right_f32_f32(curl, tmp77);
    float tmp79 = 0.07999999821186065673828125;
    float tmp80 = left1_4321_43right_f32_f32(detail, tmp79);
    float tmp81 = left1_4331_43right_f32_f32(tmp78, tmp80);
    return left1_4331_43right_f32_f32(tmp76, tmp81);
}

float the_flowing_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(_class point, float phase)
{
    float tmp = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_point_f32(point, phase);
    float tmp1 = 0.5;
    float tmp2 = left1_4321_43right_f32_f32(tmp, tmp1);
    float tmp3 = 0.5;
    return left1_4331_43right_f32_f32(tmp2, tmp3);
}

float the_absolute_value_of_magnitude_f32(float magnitude)
{
    return abs(magnitude);
}

float the_ridged_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(_class point, float phase)
{
    float tmp = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_point_f32(point, phase);
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

float the_spark_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(_class point, float phase)
{
    float tmp = point._m0.x;
    float tmp1 = 0.189999997615814208984375;
    vec2 _1471 = vec2(0.0);
    _1471.x = left1_4321_43right_f32_f32(tmp, tmp1);
    float tmp5 = point._m0.y;
    float tmp6 = 0.189999997615814208984375;
    _1471.y = left1_4321_43right_f32_f32(tmp5, tmp6);
    _class class_tmp = _class(vec2(0.0));
    class_tmp._m0 = _1471;
    _class _sample = class_tmp;
    float warp = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_point_f32(_sample, phase);
    float tmp13 = point._m0.x;
    float tmp14 = 0.23000000417232513427734375;
    float tmp15 = left1_4321_43right_f32_f32(tmp13, tmp14);
    float tmp16 = 7.0;
    vec2 _1485 = vec2(0.0);
    _1485.x = left1_4331_43right_f32_f32(tmp15, tmp16);
    float tmp21 = point._m0.y;
    float tmp22 = 0.23000000417232513427734375;
    float tmp23 = left1_4321_43right_f32_f32(tmp21, tmp22);
    float tmp24 = 5.0;
    _1485.y = left1_4351_43right_f32_f32(tmp23, tmp24);
    _class class_tmp9 = _class(vec2(0.0));
    class_tmp9._m0 = _1485;
    _class shifted = class_tmp9;
    float tmp31 = point._m0.x;
    float tmp32 = 1.7000000476837158203125;
    float tmp33 = left1_4321_43right_f32_f32(warp, tmp32);
    float longitude = left1_4331_43right_f32_f32(tmp31, tmp33);
    float tmp37 = point._m0.y;
    float tmp38 = 1.7000000476837158203125;
    float tmp39 = left1_4331_43right_f32_f32(phase, tmp38);
    float tmp40 = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_point_f32(shifted, tmp39);
    float tmp41 = 1.7000000476837158203125;
    float tmp42 = left1_4321_43right_f32_f32(tmp40, tmp41);
    float latitude = left1_4331_43right_f32_f32(tmp37, tmp42);
    float tmp43 = 1.730000019073486328125;
    float tmp44 = left1_4321_43right_f32_f32(longitude, tmp43);
    float tmp45 = 0.310000002384185791015625;
    float tmp46 = left1_4321_43right_f32_f32(latitude, tmp45);
    float tmp47 = left1_4331_43right_f32_f32(tmp46, phase);
    float tmp48 = left1_4331_43right_f32_f32(tmp44, tmp47);
    float tmp49 = the_sine_of_value_f32(tmp48);
    float primary = the_absolute_value_of_magnitude_f32(tmp49);
    float tmp50 = 0.2700000107288360595703125;
    float tmp51 = _the_negative_of_4the_opposite_of_453value_f32(tmp50);
    float tmp52 = left1_4321_43right_f32_f32(longitude, tmp51);
    float tmp53 = 1.90999996662139892578125;
    float tmp54 = left1_4321_43right_f32_f32(latitude, tmp53);
    float tmp55 = 0.829999983310699462890625;
    float tmp56 = left1_4321_43right_f32_f32(phase, tmp55);
    float tmp57 = left1_4351_43right_f32_f32(tmp54, tmp56);
    float tmp58 = left1_4331_43right_f32_f32(tmp52, tmp57);
    float tmp59 = the_sine_of_value_f32(tmp58);
    float secondary = the_absolute_value_of_magnitude_f32(tmp59);
    float tmp60 = left1_4321_43right_f32_f32(primary, primary);
    float tmp61 = left1_4321_43right_f32_f32(secondary, secondary);
    float tmp62 = left1_4331_43right_f32_f32(tmp60, tmp61);
    float crossing = the_square_root_of_value_f32(tmp62);
    float tmp63 = 0.0;
    float tmp64 = 0.115000002086162567138671875;
    float brightness = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp63, tmp64, crossing);
    float tmp66 = 0.10999999940395355224609375;
    vec2 _1526 = vec2(0.0);
    _1526.x = left1_4321_43right_f32_f32(longitude, tmp66);
    float tmp68 = 0.10999999940395355224609375;
    _1526.y = left1_4321_43right_f32_f32(latitude, tmp68);
    _class class_tmp65 = _class(vec2(0.0));
    class_tmp65._m0 = _1526;
    _class location = class_tmp65;
    float tmp72 = 4.0;
    float tmp73 = left1_4331_43right_f32_f32(phase, tmp72);
    float rarity = the_flowing_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(location, tmp73);
    float tmp74 = 0.4799999892711639404296875;
    float tmp75 = 0.819999992847442626953125;
    float tmp76 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp74, tmp75, rarity);
    return left1_4321_43right_f32_f32(brightness, tmp76);
}

float the_distance_from_3a_point8point5_to_segment_from_3a_point8start5_to_3a_point8end5_a_point_a_point_a_point(_class point, _class start, _class end)
{
    float tmp = end._m0.x;
    float tmp4 = start._m0.x;
    float span = left1_4351_43right_f32_f32(tmp, tmp4);
    float tmp8 = end._m0.y;
    float tmp12 = start._m0.y;
    float rise = left1_4351_43right_f32_f32(tmp8, tmp12);
    float tmp16 = point._m0.x;
    float tmp20 = start._m0.x;
    float horizontal = left1_4351_43right_f32_f32(tmp16, tmp20);
    float tmp24 = point._m0.y;
    float tmp28 = start._m0.y;
    float vertical = left1_4351_43right_f32_f32(tmp24, tmp28);
    float tmp29 = left1_4321_43right_f32_f32(span, span);
    float tmp30 = left1_4321_43right_f32_f32(rise, rise);
    float measure = left1_4331_43right_f32_f32(tmp29, tmp30);
    float tmp31 = left1_4321_43right_f32_f32(horizontal, span);
    float tmp32 = left1_4321_43right_f32_f32(vertical, rise);
    float tmp33 = left1_4331_43right_f32_f32(tmp31, tmp32);
    float tmp34 = 9.9999999747524270787835121154785e-07;
    float tmp35 = the_maximum_of_left_and_right_f32_f32(measure, tmp34);
    float projection = left1_4371_43right_f32_f32(tmp33, tmp35);
    projection = number_saturated_f32(projection);
    float tmp39 = start._m0.x;
    float tmp40 = left1_4321_43right_f32_f32(span, projection);
    float longitude = left1_4331_43right_f32_f32(tmp39, tmp40);
    float tmp44 = start._m0.y;
    float tmp45 = left1_4321_43right_f32_f32(rise, projection);
    float latitude = left1_4331_43right_f32_f32(tmp44, tmp45);
    float tmp49 = point._m0.x;
    float difference = left1_4351_43right_f32_f32(tmp49, longitude);
    float tmp53 = point._m0.y;
    float deviation = left1_4351_43right_f32_f32(tmp53, latitude);
    float tmp54 = left1_4321_43right_f32_f32(difference, difference);
    float tmp55 = left1_4321_43right_f32_f32(deviation, deviation);
    float tmp56 = left1_4331_43right_f32_f32(tmp54, tmp55);
    return the_square_root_of_value_f32(tmp56);
}

void main()
{
    vec2 _557 = vec2(0.0);
    _557.x = gl_FragCoord.x;
    _557.y = gl_FragCoord.y;
    _class class_tmp = _class(vec2(0.0));
    class_tmp._m0 = _557;
    _class pixel = class_tmp;
    float time = dynlexUniform0.value;
    float tmp = dynlexUniform2.value;
    float tmp5 = 1.0;
    vec2 _566 = vec2(0.0);
    _566.x = the_maximum_of_left_and_right_f32_f32(tmp, tmp5);
    float tmp7 = dynlexUniform3.value;
    float tmp8 = 1.0;
    _566.y = the_maximum_of_left_and_right_f32_f32(tmp7, tmp8);
    _class class_tmp4 = _class(vec2(0.0));
    class_tmp4._m0 = _566;
    _class frame = class_tmp4;
    float pass = dynlexUniform1.value;
    float tmp14 = frame._m0.x;
    float tmp18 = frame._m0.y;
    float aspect = left1_4371_43right_f32_f32(tmp14, tmp18);
    float tmp23 = pixel._m0.x;
    float tmp27 = frame._m0.x;
    float tmp28 = left1_4371_43right_f32_f32(tmp23, tmp27);
    float tmp29 = 2.0;
    float tmp30 = left1_4321_43right_f32_f32(tmp28, tmp29);
    float tmp31 = 1.0;
    float tmp32 = left1_4351_43right_f32_f32(tmp30, tmp31);
    vec2 _590 = vec2(0.0);
    _590.x = left1_4321_43right_f32_f32(tmp32, aspect);
    float tmp37 = pixel._m0.y;
    float tmp41 = frame._m0.y;
    float tmp42 = left1_4371_43right_f32_f32(tmp37, tmp41);
    float tmp43 = 2.0;
    float tmp44 = left1_4321_43right_f32_f32(tmp42, tmp43);
    float tmp45 = 1.0;
    _590.y = left1_4351_43right_f32_f32(tmp44, tmp45);
    _class class_tmp19 = _class(vec2(0.0));
    class_tmp19._m0 = _590;
    _class screen = class_tmp19;
    float tmp52 = screen._m0.x;
    float tmp56 = screen._m0.x;
    float tmp57 = left1_4321_43right_f32_f32(tmp52, tmp56);
    float tmp61 = screen._m0.y;
    float tmp65 = screen._m0.y;
    float tmp66 = left1_4321_43right_f32_f32(tmp61, tmp65);
    float tmp67 = left1_4331_43right_f32_f32(tmp57, tmp66);
    float radial = the_square_root_of_value_f32(tmp67);
    float tmp68 = 10.3999996185302734375;
    float moment = the_minimum_of_left_and_right_f32_f32(time, tmp68);
    float tmp69 = 0.5;
    if (left_2_right_f32_f32(pass, tmp69))
    {
        float tmp73 = screen._m0.x;
        float tmp74 = 19.0;
        float tmp75 = left1_4321_43right_f32_f32(tmp73, tmp74);
        float tmp79 = screen._m0.y;
        float tmp80 = 13.0;
        float tmp81 = left1_4321_43right_f32_f32(tmp79, tmp80);
        float tmp82 = left1_4351_43right_f32_f32(tmp75, tmp81);
        float tmp83 = 4.19999980926513671875;
        float tmp84 = left1_4321_43right_f32_f32(time, tmp83);
        float tmp85 = left1_4331_43right_f32_f32(tmp82, tmp84);
        float tmp86 = the_sine_of_value_f32(tmp85);
        float tmp87 = 0.5;
        float tmp88 = left1_4321_43right_f32_f32(tmp86, tmp87);
        float tmp89 = 0.5;
        float wave = left1_4331_43right_f32_f32(tmp88, tmp89);
        float tmp93 = screen._m0.y;
        float tmp94 = 47.0;
        float tmp95 = left1_4321_43right_f32_f32(tmp93, tmp94);
        float tmp96 = 6.80000019073486328125;
        float tmp97 = left1_4321_43right_f32_f32(time, tmp96);
        float tmp98 = left1_4351_43right_f32_f32(tmp95, tmp97);
        float tmp99 = the_sine_of_value_f32(tmp98);
        float tmp100 = 0.5;
        float tmp101 = left1_4321_43right_f32_f32(tmp99, tmp100);
        float tmp102 = 0.5;
        float scan = left1_4331_43right_f32_f32(tmp101, tmp102);
        float tmp103 = 1.0;
        float tmp104 = 4.44999980926513671875;
        float tmp105 = 7.400000095367431640625;
        float tmp106 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp104, tmp105, moment);
        float warmth = left1_4351_43right_f32_f32(tmp103, tmp106);
        float tmp108 = 0.07999999821186065673828125;
        float tmp109 = 0.2800000011920928955078125;
        float tmp110 = left1_4321_43right_f32_f32(wave, tmp109);
        float tmp111 = left1_4331_43right_f32_f32(tmp108, tmp110);
        float tmp112 = 0.37999999523162841796875;
        float tmp113 = left1_4321_43right_f32_f32(warmth, tmp112);
        vec3 _1091 = vec3(0.0);
        _1091.x = left1_4331_43right_f32_f32(tmp111, tmp113);
        float tmp115 = 0.300000011920928955078125;
        float tmp116 = 0.23999999463558197021484375;
        float tmp117 = left1_4321_43right_f32_f32(scan, tmp116);
        float tmp118 = left1_4331_43right_f32_f32(tmp115, tmp117);
        float tmp119 = 0.119999997317790985107421875;
        float tmp120 = left1_4321_43right_f32_f32(warmth, tmp119);
        _1091.y = left1_4331_43right_f32_f32(tmp118, tmp120);
        float tmp122 = 0.660000026226043701171875;
        float tmp123 = 0.300000011920928955078125;
        float tmp124 = left1_4321_43right_f32_f32(wave, tmp123);
        float tmp125 = left1_4331_43right_f32_f32(tmp122, tmp124);
        float tmp126 = 0.180000007152557373046875;
        float tmp127 = left1_4321_43right_f32_f32(warmth, tmp126);
        _1091.z = left1_4351_43right_f32_f32(tmp125, tmp127);
        class_0 class_tmp107 = class_0(vec3(0.0));
        class_tmp107._m0 = _1091;
        class_0 color = class_tmp107;
        vec4 _1107 = vec4(0.0);
        _1107.x = color._m0.x;
        _1107.y = color._m0.y;
        _1107.z = color._m0.z;
        _1107.w = 1.0;
        dynlexColor = _1107;
    }
    else
    {
        float tmp144 = 0.0;
        float tmp145 = 7.400000095367431640625;
        float speed = the_scene_window_from_opening_to_closing_at_moment_f32_f32_f32(tmp144, tmp145, moment);
        float tmp146 = 6.849999904632568359375;
        float tmp147 = 11.0;
        float figure = the_scene_window_from_opening_to_closing_at_moment_f32_f32_f32(tmp146, tmp147, moment);
        float tmp152 = screen._m0.x;
        float tmp153 = 2.2000000476837158203125;
        float tmp154 = left1_4321_43right_f32_f32(tmp152, tmp153);
        float tmp155 = 0.02099999971687793731689453125;
        float tmp156 = left1_4321_43right_f32_f32(time, tmp155);
        vec2 _629 = vec2(0.0);
        _629.x = left1_4331_43right_f32_f32(tmp154, tmp156);
        float tmp161 = screen._m0.y;
        float tmp162 = 2.2000000476837158203125;
        float tmp163 = left1_4321_43right_f32_f32(tmp161, tmp162);
        float tmp164 = 0.01400000043213367462158203125;
        float tmp165 = left1_4321_43right_f32_f32(time, tmp164);
        _629.y = left1_4351_43right_f32_f32(tmp163, tmp165);
        _class class_tmp148 = _class(vec2(0.0));
        class_tmp148._m0 = _629;
        _class tmp169 = class_tmp148;
        float tmp170 = 1.89999997615814208984375;
        float chamber = the_flowing_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp169, tmp170);
        float tmp175 = screen._m0.x;
        float tmp176 = 4.099999904632568359375;
        float tmp177 = left1_4321_43right_f32_f32(tmp175, tmp176);
        float tmp178 = 0.0170000009238719940185546875;
        float tmp179 = left1_4321_43right_f32_f32(time, tmp178);
        vec2 _646 = vec2(0.0);
        _646.x = left1_4351_43right_f32_f32(tmp177, tmp179);
        float tmp184 = screen._m0.y;
        float tmp185 = 4.099999904632568359375;
        float tmp186 = left1_4321_43right_f32_f32(tmp184, tmp185);
        float tmp187 = 0.010999999940395355224609375;
        float tmp188 = left1_4321_43right_f32_f32(time, tmp187);
        _646.y = left1_4331_43right_f32_f32(tmp186, tmp188);
        _class class_tmp171 = _class(vec2(0.0));
        class_tmp171._m0 = _646;
        _class tmp192 = class_tmp171;
        float tmp193 = 5.400000095367431640625;
        float ridges = the_ridged_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp192, tmp193);
        float tmp198 = screen._m0.x;
        float tmp199 = 27.0;
        float tmp200 = left1_4321_43right_f32_f32(tmp198, tmp199);
        float tmp201 = 0.23999999463558197021484375;
        float tmp202 = left1_4321_43right_f32_f32(time, tmp201);
        vec2 _663 = vec2(0.0);
        _663.x = left1_4331_43right_f32_f32(tmp200, tmp202);
        float tmp207 = screen._m0.y;
        float tmp208 = 27.0;
        float tmp209 = left1_4321_43right_f32_f32(tmp207, tmp208);
        float tmp210 = 0.12999999523162841796875;
        float tmp211 = left1_4321_43right_f32_f32(time, tmp210);
        _663.y = left1_4351_43right_f32_f32(tmp209, tmp211);
        _class class_tmp194 = _class(vec2(0.0));
        class_tmp194._m0 = _663;
        _class tmp215 = class_tmp194;
        float tmp216 = 9.69999980926513671875;
        float drones = the_spark_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp215, tmp216);
        float tmp221 = screen._m0.x;
        float tmp222 = 7.0;
        float tmp223 = left1_4321_43right_f32_f32(tmp221, tmp222);
        float tmp224 = 8.3999996185302734375;
        float tmp225 = left1_4321_43right_f32_f32(time, tmp224);
        vec2 _680 = vec2(0.0);
        _680.x = left1_4331_43right_f32_f32(tmp223, tmp225);
        float tmp230 = screen._m0.y;
        float tmp231 = 46.0;
        _680.y = left1_4321_43right_f32_f32(tmp230, tmp231);
        _class class_tmp217 = _class(vec2(0.0));
        class_tmp217._m0 = _680;
        _class tmp235 = class_tmp217;
        float tmp236 = 14.19999980926513671875;
        float particles = the_spark_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp235, tmp236);
        float tmp237 = 0.0;
        float tmp238 = 0.660000026226043701171875;
        float tmp242 = screen._m0.y;
        float tmp243 = the_absolute_value_of_magnitude_f32(tmp242);
        float core = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp237, tmp238, tmp243);
        float tmp244 = left1_4321_43right_f32_f32(particles, core);
        float trails = left1_4321_43right_f32_f32(tmp244, speed);
        float tmp246 = 0.0030000000260770320892333984375;
        float tmp247 = 0.01200000010430812835693359375;
        float tmp248 = left1_4321_43right_f32_f32(chamber, tmp247);
        float tmp249 = left1_4331_43right_f32_f32(tmp246, tmp248);
        float tmp250 = 0.017999999225139617919921875;
        float tmp251 = left1_4321_43right_f32_f32(ridges, tmp250);
        float tmp252 = left1_4331_43right_f32_f32(tmp249, tmp251);
        float tmp253 = 0.180000007152557373046875;
        float tmp254 = left1_4321_43right_f32_f32(drones, tmp253);
        float tmp255 = left1_4331_43right_f32_f32(tmp252, tmp254);
        float tmp256 = 0.319999992847442626953125;
        float tmp257 = left1_4321_43right_f32_f32(trails, tmp256);
        vec3 _704 = vec3(0.0);
        _704.x = left1_4331_43right_f32_f32(tmp255, tmp257);
        float tmp259 = 0.006000000052154064178466796875;
        float tmp260 = 0.0240000002086162567138671875;
        float tmp261 = left1_4321_43right_f32_f32(chamber, tmp260);
        float tmp262 = left1_4331_43right_f32_f32(tmp259, tmp261);
        float tmp263 = 0.014999999664723873138427734375;
        float tmp264 = left1_4321_43right_f32_f32(ridges, tmp263);
        float tmp265 = left1_4331_43right_f32_f32(tmp262, tmp264);
        float tmp266 = 0.4600000083446502685546875;
        float tmp267 = left1_4321_43right_f32_f32(drones, tmp266);
        float tmp268 = left1_4331_43right_f32_f32(tmp265, tmp267);
        float tmp269 = 0.7799999713897705078125;
        float tmp270 = left1_4321_43right_f32_f32(trails, tmp269);
        _704.y = left1_4331_43right_f32_f32(tmp268, tmp270);
        float tmp272 = 0.0240000002086162567138671875;
        float tmp273 = 0.08200000226497650146484375;
        float tmp274 = left1_4321_43right_f32_f32(chamber, tmp273);
        float tmp275 = left1_4331_43right_f32_f32(tmp272, tmp274);
        float tmp276 = 0.071000002324581146240234375;
        float tmp277 = left1_4321_43right_f32_f32(ridges, tmp276);
        float tmp278 = left1_4331_43right_f32_f32(tmp275, tmp277);
        float tmp279 = 1.019999980926513671875;
        float tmp280 = left1_4321_43right_f32_f32(drones, tmp279);
        float tmp281 = left1_4331_43right_f32_f32(tmp278, tmp280);
        float tmp282 = 1.46000003814697265625;
        float tmp283 = left1_4321_43right_f32_f32(trails, tmp282);
        _704.z = left1_4331_43right_f32_f32(tmp281, tmp283);
        class_0 class_tmp245 = class_0(vec3(0.0));
        class_tmp245._m0 = _704;
        class_0 color143 = class_tmp245;
        float tmp287 = 0.2800000011920928955078125;
        float tmp288 = 1.2599999904632568359375;
        float aura = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp287, tmp288, radial);
        vec3 _727 = color143._m0;
        float tmp294 = color143._m0.x;
        float tmp295 = left1_4321_43right_f32_f32(aura, figure);
        float tmp296 = 0.0320000015199184417724609375;
        float tmp297 = left1_4321_43right_f32_f32(tmp295, tmp296);
        _727.x = left1_4331_43right_f32_f32(tmp294, tmp297);
        color143._m0 = _727;
        vec3 _737 = color143._m0;
        float tmp304 = color143._m0.y;
        float tmp305 = left1_4321_43right_f32_f32(aura, figure);
        float tmp306 = 0.0949999988079071044921875;
        float tmp307 = left1_4321_43right_f32_f32(tmp305, tmp306);
        _737.y = left1_4331_43right_f32_f32(tmp304, tmp307);
        color143._m0 = _737;
        vec3 _747 = color143._m0;
        float tmp315 = color143._m0.z;
        float tmp316 = left1_4321_43right_f32_f32(aura, figure);
        float tmp317 = 0.20999999344348907470703125;
        float tmp318 = left1_4321_43right_f32_f32(tmp316, tmp317);
        _747.z = left1_4331_43right_f32_f32(tmp315, tmp318);
        color143._m0 = _747;
        float tmp321 = 7.55000019073486328125;
        float tmp322 = 8.1000003814697265625;
        float visibility = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp321, tmp322, moment);
        float tmp323 = 7.55000019073486328125;
        float tmp324 = left1_4351_43right_f32_f32(time, tmp323);
        float tmp325 = 1.32000005245208740234375;
        float angle = left1_4321_43right_f32_f32(tmp324, tmp325);
        float radius = 1.08000004291534423828125;
        float tmp327 = the_cosine_of_value_f32(angle);
        vec2 _761 = vec2(0.0);
        _761.x = left1_4321_43right_f32_f32(tmp327, radius);
        float tmp329 = the_sine_of_value_f32(angle);
        _761.y = left1_4321_43right_f32_f32(tmp329, radius);
        _class class_tmp326 = _class(vec2(0.0));
        class_tmp326._m0 = _761;
        _class source = class_tmp326;
        float tmp333 = 0.310000002384185791015625;
        float tmp334 = left1_4321_43right_f32_f32(time, tmp333);
        float tmp335 = the_sine_of_value_f32(tmp334);
        float tmp336 = 0.3400000035762786865234375;
        float yaw = left1_4321_43right_f32_f32(tmp335, tmp336);
        float tmp341 = source._m0.x;
        float tmp342 = the_cosine_of_value_f32(yaw);
        vec3 _775 = vec3(0.0);
        _775.x = left1_4321_43right_f32_f32(tmp341, tmp342);
        _775.y = source._m0.y;
        float tmp348 = 0.0;
        float tmp352 = source._m0.x;
        float tmp353 = the_sine_of_value_f32(yaw);
        float tmp354 = left1_4321_43right_f32_f32(tmp352, tmp353);
        _775.z = left1_4351_43right_f32_f32(tmp348, tmp354);
        class_0 class_tmp337 = class_0(vec3(0.0));
        class_tmp337._m0 = _775;
        class_0 turned = class_tmp337;
        float tmp358 = 3.25;
        float tmp362 = turned._m0.z;
        float tmp363 = 0.519999980926513671875;
        float tmp364 = left1_4321_43right_f32_f32(tmp362, tmp363);
        float depth = left1_4331_43right_f32_f32(tmp358, tmp364);
        float tmp366 = 0.0;
        float tmp367 = 0.039999999105930328369140625;
        float tmp368 = left1_4351_43right_f32_f32(tmp366, tmp367);
        float tmp369 = 3.25;
        vec2 _796 = vec2(0.0);
        _796.y = left1_4371_43right_f32_f32(tmp368, tmp369);
        _class class_tmp365 = _class(vec2(0.0));
        class_tmp365._m0 = _796;
        _class center = class_tmp365;
        float tmp377 = turned._m0.x;
        float tmp378 = 1.65999996662139892578125;
        float tmp379 = left1_4321_43right_f32_f32(tmp377, tmp378);
        vec2 _804 = vec2(0.0);
        _804.x = left1_4371_43right_f32_f32(tmp379, depth);
        float tmp384 = source._m0.y;
        float tmp385 = 1.86000001430511474609375;
        float tmp386 = left1_4321_43right_f32_f32(tmp384, tmp385);
        float tmp387 = 0.039999999105930328369140625;
        float tmp388 = left1_4351_43right_f32_f32(tmp386, tmp387);
        _804.y = left1_4371_43right_f32_f32(tmp388, depth);
        _class class_tmp373 = _class(vec2(0.0));
        class_tmp373._m0 = _804;
        _class ending = class_tmp373;
        float _distance = the_distance_from_3a_point8point5_to_segment_from_3a_point8start5_to_3a_point8end5_a_point_a_point_a_point(screen, center, ending);
        float tmp392 = 0.00200000009499490261077880859375;
        float tmp393 = 0.0089999996125698089599609375;
        float line = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp392, tmp393, _distance);
        vec2 _819 = vec2(0.0);
        _819.x = ending._m0.x;
        float tmp402 = ending._m0.y;
        float tmp406 = center._m0.y;
        _819.y = left1_4351_43right_f32_f32(tmp402, tmp406);
        _class class_tmp394 = _class(vec2(0.0));
        class_tmp394._m0 = _819;
        _class vector = class_tmp394;
        float tmp413 = vector._m0.x;
        float tmp417 = vector._m0.x;
        float tmp418 = left1_4321_43right_f32_f32(tmp413, tmp417);
        float tmp422 = vector._m0.y;
        float tmp426 = vector._m0.y;
        float tmp427 = left1_4321_43right_f32_f32(tmp422, tmp426);
        float tmp428 = left1_4331_43right_f32_f32(tmp418, tmp427);
        float tmp429 = the_square_root_of_value_f32(tmp428);
        float tmp430 = 9.9999997473787516355514526367188e-05;
        float _length = the_maximum_of_left_and_right_f32_f32(tmp429, tmp430);
        float tmp432 = 0.0;
        float tmp436 = vector._m0.y;
        float tmp437 = left1_4351_43right_f32_f32(tmp432, tmp436);
        vec2 _852 = vec2(0.0);
        _852.x = left1_4371_43right_f32_f32(tmp437, _length);
        float tmp442 = vector._m0.x;
        _852.y = left1_4371_43right_f32_f32(tmp442, _length);
        _class class_tmp431 = _class(vec2(0.0));
        class_tmp431._m0 = _852;
        _class tangent = class_tmp431;
        float tmp450 = ending._m0.x;
        float tmp454 = tangent._m0.x;
        float tmp455 = 0.026000000536441802978515625;
        float tmp456 = left1_4321_43right_f32_f32(tmp454, tmp455);
        vec2 _868 = vec2(0.0);
        _868.x = left1_4351_43right_f32_f32(tmp450, tmp456);
        float tmp461 = ending._m0.y;
        float tmp465 = tangent._m0.y;
        float tmp466 = 0.026000000536441802978515625;
        float tmp467 = left1_4321_43right_f32_f32(tmp465, tmp466);
        _868.y = left1_4351_43right_f32_f32(tmp461, tmp467);
        _class class_tmp446 = _class(vec2(0.0));
        class_tmp446._m0 = _868;
        _class start = class_tmp446;
        float tmp475 = ending._m0.x;
        float tmp479 = tangent._m0.x;
        float tmp480 = 0.026000000536441802978515625;
        float tmp481 = left1_4321_43right_f32_f32(tmp479, tmp480);
        vec2 _888 = vec2(0.0);
        _888.x = left1_4331_43right_f32_f32(tmp475, tmp481);
        float tmp486 = ending._m0.y;
        float tmp490 = tangent._m0.y;
        float tmp491 = 0.026000000536441802978515625;
        float tmp492 = left1_4321_43right_f32_f32(tmp490, tmp491);
        _888.y = left1_4331_43right_f32_f32(tmp486, tmp492);
        _class class_tmp471 = _class(vec2(0.0));
        class_tmp471._m0 = _888;
        _class finish = class_tmp471;
        _distance = the_distance_from_3a_point8point5_to_segment_from_3a_point8start5_to_3a_point8end5_a_point_a_point_a_point(screen, start, finish);
        float tmp496 = 0.00200000009499490261077880859375;
        float tmp497 = 0.010999999940395355224609375;
        float tick = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp496, tmp497, _distance);
        float tmp502 = screen._m0.x;
        float tmp506 = ending._m0.x;
        vec2 _909 = vec2(0.0);
        _909.x = left1_4351_43right_f32_f32(tmp502, tmp506);
        float tmp511 = screen._m0.y;
        float tmp515 = ending._m0.y;
        _909.y = left1_4351_43right_f32_f32(tmp511, tmp515);
        _class class_tmp498 = _class(vec2(0.0));
        class_tmp498._m0 = _909;
        _class highlight = class_tmp498;
        float tmp522 = highlight._m0.x;
        float tmp526 = highlight._m0.x;
        float tmp527 = left1_4321_43right_f32_f32(tmp522, tmp526);
        float tmp531 = highlight._m0.y;
        float tmp535 = highlight._m0.y;
        float tmp536 = left1_4321_43right_f32_f32(tmp531, tmp535);
        float tmp537 = left1_4331_43right_f32_f32(tmp527, tmp536);
        _distance = the_square_root_of_value_f32(tmp537);
        float tmp538 = 0.0;
        float tmp539 = 0.04500000178813934326171875;
        float circle = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp538, tmp539, _distance);
        vec2 _940 = vec2(0.0);
        _940.x = screen._m0.x;
        float tmp548 = screen._m0.y;
        float tmp552 = center._m0.y;
        _940.y = left1_4351_43right_f32_f32(tmp548, tmp552);
        _class class_tmp540 = _class(vec2(0.0));
        class_tmp540._m0 = _940;
        highlight = class_tmp540;
        float tmp559 = highlight._m0.x;
        float tmp563 = highlight._m0.x;
        float tmp564 = left1_4321_43right_f32_f32(tmp559, tmp563);
        float tmp568 = highlight._m0.y;
        float tmp572 = highlight._m0.y;
        float tmp573 = left1_4321_43right_f32_f32(tmp568, tmp572);
        float tmp574 = left1_4331_43right_f32_f32(tmp564, tmp573);
        _distance = the_square_root_of_value_f32(tmp574);
        float tmp575 = 0.0;
        float tmp576 = 0.02500000037252902984619140625;
        float radiance = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp575, tmp576, _distance);
        float tmp577 = 0.62000000476837158203125;
        float tmp578 = left1_4321_43right_f32_f32(line, tmp577);
        float tmp579 = 0.819999992847442626953125;
        float tmp580 = left1_4321_43right_f32_f32(tick, tmp579);
        float tmp581 = left1_4331_43right_f32_f32(tmp578, tmp580);
        float tmp582 = 0.4799999892711639404296875;
        float tmp583 = left1_4321_43right_f32_f32(circle, tmp582);
        float tmp584 = left1_4331_43right_f32_f32(tmp581, tmp583);
        float tmp585 = 0.36000001430511474609375;
        float tmp586 = left1_4321_43right_f32_f32(radiance, tmp585);
        float light = left1_4331_43right_f32_f32(tmp584, tmp586);
        light = left1_4321_43right_f32_f32(light, visibility);
        vec3 _977 = color143._m0;
        float tmp592 = color143._m0.x;
        float tmp593 = 0.4600000083446502685546875;
        float tmp594 = left1_4321_43right_f32_f32(light, tmp593);
        _977.x = left1_4331_43right_f32_f32(tmp592, tmp594);
        color143._m0 = _977;
        vec3 _986 = color143._m0;
        float tmp602 = color143._m0.y;
        float tmp603 = 0.87999999523162841796875;
        float tmp604 = left1_4321_43right_f32_f32(light, tmp603);
        _986.y = left1_4331_43right_f32_f32(tmp602, tmp604);
        color143._m0 = _986;
        vec3 _995 = color143._m0;
        float tmp612 = color143._m0.z;
        float tmp613 = 1.2400000095367431640625;
        float tmp614 = left1_4321_43right_f32_f32(light, tmp613);
        _995.z = left1_4331_43right_f32_f32(tmp612, tmp614);
        color143._m0 = _995;
        float tmp617 = 0.180000007152557373046875;
        float tmp618 = 1.0;
        float tmp619 = 0.519999980926513671875;
        float tmp620 = 1.62000000476837158203125;
        float tmp621 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp619, tmp620, radial);
        float tmp622 = left1_4351_43right_f32_f32(tmp618, tmp621);
        float tmp623 = 0.819999992847442626953125;
        float tmp624 = left1_4321_43right_f32_f32(tmp622, tmp623);
        float vignette = left1_4331_43right_f32_f32(tmp617, tmp624);
        vec3 _1008 = color143._m0;
        float tmp630 = color143._m0.x;
        float tmp631 = left1_4321_43right_f32_f32(tmp630, vignette);
        float tmp632 = 1.0;
        float tmp636 = color143._m0.x;
        float tmp637 = left1_4331_43right_f32_f32(tmp632, tmp636);
        float tmp638 = left1_4371_43right_f32_f32(tmp631, tmp637);
        _1008.x = the_square_root_of_value_f32(tmp638);
        color143._m0 = _1008;
        vec3 _1022 = color143._m0;
        float tmp646 = color143._m0.y;
        float tmp647 = left1_4321_43right_f32_f32(tmp646, vignette);
        float tmp648 = 1.0;
        float tmp652 = color143._m0.y;
        float tmp653 = left1_4331_43right_f32_f32(tmp648, tmp652);
        float tmp654 = left1_4371_43right_f32_f32(tmp647, tmp653);
        _1022.y = the_square_root_of_value_f32(tmp654);
        color143._m0 = _1022;
        vec3 _1036 = color143._m0;
        float tmp662 = color143._m0.z;
        float tmp663 = left1_4321_43right_f32_f32(tmp662, vignette);
        float tmp664 = 1.0;
        float tmp668 = color143._m0.z;
        float tmp669 = left1_4331_43right_f32_f32(tmp664, tmp668);
        float tmp670 = left1_4371_43right_f32_f32(tmp663, tmp669);
        _1036.z = the_square_root_of_value_f32(tmp670);
        color143._m0 = _1036;
        vec4 _1052 = vec4(0.0);
        _1052.x = color143._m0.x;
        _1052.y = color143._m0.y;
        _1052.z = color143._m0.z;
        _1052.w = 1.0;
        dynlexColor = _1052;
    }
}
