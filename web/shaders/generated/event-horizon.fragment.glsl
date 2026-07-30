#version 300 es
precision highp float;
precision highp int;

struct _class
{
    vec2 _m0;
};

struct class_0
{
    vec3 _m0;
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

float the_spark_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(_class point, float phase)
{
    float tmp = point._m0.x;
    float tmp1 = 0.189999997615814208984375;
    vec2 _1952 = vec2(0.0);
    _1952.x = left1_4321_43right_f32_f32(tmp, tmp1);
    float tmp5 = point._m0.y;
    float tmp6 = 0.189999997615814208984375;
    _1952.y = left1_4321_43right_f32_f32(tmp5, tmp6);
    _class class_tmp = _class(vec2(0.0));
    class_tmp._m0 = _1952;
    _class _sample = class_tmp;
    float warp = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_point_f32(_sample, phase);
    float tmp13 = point._m0.x;
    float tmp14 = 0.23000000417232513427734375;
    float tmp15 = left1_4321_43right_f32_f32(tmp13, tmp14);
    float tmp16 = 7.0;
    vec2 _1966 = vec2(0.0);
    _1966.x = left1_4331_43right_f32_f32(tmp15, tmp16);
    float tmp21 = point._m0.y;
    float tmp22 = 0.23000000417232513427734375;
    float tmp23 = left1_4321_43right_f32_f32(tmp21, tmp22);
    float tmp24 = 5.0;
    _1966.y = left1_4351_43right_f32_f32(tmp23, tmp24);
    _class class_tmp9 = _class(vec2(0.0));
    class_tmp9._m0 = _1966;
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
    vec2 _2007 = vec2(0.0);
    _2007.x = left1_4321_43right_f32_f32(longitude, tmp66);
    float tmp68 = 0.10999999940395355224609375;
    _2007.y = left1_4321_43right_f32_f32(latitude, tmp68);
    _class class_tmp65 = _class(vec2(0.0));
    class_tmp65._m0 = _2007;
    _class location = class_tmp65;
    float tmp72 = 4.0;
    float tmp73 = left1_4331_43right_f32_f32(phase, tmp72);
    float rarity = the_flowing_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(location, tmp73);
    float tmp74 = 0.4799999892711639404296875;
    float tmp75 = 0.819999992847442626953125;
    float tmp76 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp74, tmp75, rarity);
    return left1_4321_43right_f32_f32(brightness, tmp76);
}

float a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3floating5point_number8scale5_during_3floating5point_number8phase5_at_3floating5point_number8time5_a_point_f32_f32_f32(_class position, float scale, float phase, float time)
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
    float tmp7 = position._m0.x;
    float tmp8 = left1_4371_43right_f32_f32(tmp7, expansion);
    vec2 _1791 = vec2(0.0);
    _1791.x = left1_4321_43right_f32_f32(tmp8, scale);
    float tmp12 = position._m0.y;
    float tmp13 = left1_4371_43right_f32_f32(tmp12, expansion);
    _1791.y = left1_4321_43right_f32_f32(tmp13, scale);
    _class class_tmp = _class(vec2(0.0));
    class_tmp._m0 = _1791;
    _class _sample = class_tmp;
    vec2 _1803 = vec2(0.0);
    _1803.x = _sample._m0.x;
    _1803.y = _sample._m0.y;
    _class class_tmp16 = _class(vec2(0.0));
    class_tmp16._m0 = _1803;
    _class tmp27 = class_tmp16;
    float tmp28 = 13.69999980926513671875;
    float tmp29 = left1_4321_43right_f32_f32(phase, tmp28);
    float points = the_spark_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp27, tmp29);
    float tmp34 = _sample._m0.x;
    float tmp35 = 0.3300000131130218505859375;
    vec2 _1816 = vec2(0.0);
    _1816.x = left1_4321_43right_f32_f32(tmp34, tmp35);
    _1816.y = _sample._m0.y;
    _class class_tmp30 = _class(vec2(0.0));
    class_tmp30._m0 = _1816;
    _class tmp43 = class_tmp30;
    float tmp44 = 13.69999980926513671875;
    float tmp45 = left1_4321_43right_f32_f32(phase, tmp44);
    float tmp46 = 0.0900000035762786865234375;
    float tmp47 = left1_4331_43right_f32_f32(tmp45, tmp46);
    float horizontal = the_spark_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp43, tmp47);
    vec2 _1829 = vec2(0.0);
    _1829.x = _sample._m0.x;
    float tmp56 = _sample._m0.y;
    float tmp57 = 0.3300000131130218505859375;
    _1829.y = left1_4321_43right_f32_f32(tmp56, tmp57);
    _class class_tmp48 = _class(vec2(0.0));
    class_tmp48._m0 = _1829;
    _class tmp61 = class_tmp48;
    float tmp62 = 13.69999980926513671875;
    float tmp63 = left1_4321_43right_f32_f32(phase, tmp62);
    float tmp64 = 0.10999999940395355224609375;
    float tmp65 = left1_4351_43right_f32_f32(tmp63, tmp64);
    float vertical = the_spark_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp61, tmp65);
    float tmp69 = position._m0.x;
    float tmp73 = position._m0.x;
    float tmp74 = left1_4321_43right_f32_f32(tmp69, tmp73);
    float tmp78 = position._m0.y;
    float tmp82 = position._m0.y;
    float tmp83 = left1_4321_43right_f32_f32(tmp78, tmp82);
    float tmp84 = left1_4331_43right_f32_f32(tmp74, tmp83);
    float radius = the_square_root_of_value_f32(tmp84);
    float tmp85 = left1_4331_43right_f32_f32(horizontal, vertical);
    float tmp86 = 0.0;
    float tmp87 = 1.35000002384185791015625;
    float tmp88 = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp86, tmp87, radius);
    float streaks = left1_4321_43right_f32_f32(tmp85, tmp88);
    float tmp89 = 2.7000000476837158203125;
    float tmp90 = left1_4321_43right_f32_f32(time, tmp89);
    float tmp91 = 29.0;
    float tmp92 = left1_4321_43right_f32_f32(phase, tmp91);
    float tmp93 = left1_4331_43right_f32_f32(tmp90, tmp92);
    float tmp94 = the_sine_of_value_f32(tmp93);
    float tmp95 = 0.12999999523162841796875;
    float tmp96 = left1_4321_43right_f32_f32(tmp94, tmp95);
    float tmp97 = 0.87000000476837158203125;
    float twinkle = left1_4331_43right_f32_f32(tmp96, tmp97);
    float tmp98 = 2.7999999523162841796875;
    float tmp99 = left1_4321_43right_f32_f32(points, tmp98);
    float tmp100 = 0.17000000178813934326171875;
    float tmp101 = left1_4321_43right_f32_f32(streaks, tmp100);
    float tmp102 = left1_4331_43right_f32_f32(tmp99, tmp101);
    float tmp103 = 0.2800000011920928955078125;
    float tmp104 = 1.25;
    float tmp105 = left1_4321_43right_f32_f32(depth, tmp104);
    float tmp106 = left1_4331_43right_f32_f32(tmp103, tmp105);
    float tmp107 = left1_4321_43right_f32_f32(tmp102, tmp106);
    return left1_4321_43right_f32_f32(tmp107, twinkle);
}

void main()
{
    vec2 _737 = vec2(0.0);
    _737.x = gl_FragCoord.x;
    _737.y = gl_FragCoord.y;
    _class class_tmp = _class(vec2(0.0));
    class_tmp._m0 = _737;
    _class pixel = class_tmp;
    float time = dynlexUniform0.value;
    float tmp = dynlexUniform1.value;
    float tmp5 = 1.0;
    vec2 _746 = vec2(0.0);
    _746.x = the_maximum_of_left_and_right_f32_f32(tmp, tmp5);
    float tmp7 = dynlexUniform2.value;
    float tmp8 = 1.0;
    _746.y = the_maximum_of_left_and_right_f32_f32(tmp7, tmp8);
    _class class_tmp4 = _class(vec2(0.0));
    class_tmp4._m0 = _746;
    _class frame = class_tmp4;
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
    vec2 _769 = vec2(0.0);
    _769.x = left1_4321_43right_f32_f32(tmp32, aspect);
    float tmp37 = pixel._m0.y;
    float tmp41 = frame._m0.y;
    float tmp42 = left1_4371_43right_f32_f32(tmp37, tmp41);
    float tmp43 = 2.0;
    float tmp44 = left1_4321_43right_f32_f32(tmp42, tmp43);
    float tmp45 = 1.0;
    _769.y = left1_4351_43right_f32_f32(tmp44, tmp45);
    _class class_tmp19 = _class(vec2(0.0));
    class_tmp19._m0 = _769;
    _class screen = class_tmp19;
    float tmp49 = 0.62000000476837158203125;
    float tmp50 = 0.054999999701976776123046875;
    float tmp51 = left1_4321_43right_f32_f32(time, tmp50);
    float tmp52 = left1_4331_43right_f32_f32(tmp49, tmp51);
    float tmp53 = left1_4321_43right_f32_f32(time, time);
    float tmp54 = 0.00179999996908009052276611328125;
    float tmp55 = left1_4321_43right_f32_f32(tmp53, tmp54);
    float flight = left1_4331_43right_f32_f32(tmp52, tmp55);
    float tmp57 = 0.2700000107288360595703125;
    float tmp58 = left1_4321_43right_f32_f32(time, tmp57);
    float tmp59 = the_sine_of_value_f32(tmp58);
    float tmp60 = 0.048000000417232513427734375;
    vec2 _790 = vec2(0.0);
    _790.x = left1_4321_43right_f32_f32(tmp59, tmp60);
    float tmp62 = 0.20999999344348907470703125;
    float tmp63 = left1_4321_43right_f32_f32(time, tmp62);
    float tmp64 = the_cosine_of_value_f32(tmp63);
    float tmp65 = 0.026000000536441802978515625;
    _790.y = left1_4321_43right_f32_f32(tmp64, tmp65);
    _class class_tmp56 = _class(vec2(0.0));
    class_tmp56._m0 = _790;
    _class drift = class_tmp56;
    float tmp73 = screen._m0.x;
    float tmp77 = drift._m0.x;
    float tmp78 = left1_4331_43right_f32_f32(tmp73, tmp77);
    vec2 _805 = vec2(0.0);
    _805.x = left1_4371_43right_f32_f32(tmp78, flight);
    float tmp83 = screen._m0.y;
    float tmp87 = drift._m0.y;
    float tmp88 = left1_4331_43right_f32_f32(tmp83, tmp87);
    _805.y = left1_4371_43right_f32_f32(tmp88, flight);
    _class class_tmp69 = _class(vec2(0.0));
    class_tmp69._m0 = _805;
    _class position = class_tmp69;
    float tmp95 = position._m0.x;
    float tmp99 = position._m0.x;
    float tmp100 = left1_4321_43right_f32_f32(tmp95, tmp99);
    float tmp104 = position._m0.y;
    float tmp108 = position._m0.y;
    float tmp109 = left1_4321_43right_f32_f32(tmp104, tmp108);
    float tmp110 = left1_4331_43right_f32_f32(tmp100, tmp109);
    float radius = the_square_root_of_value_f32(tmp110);
    float tmp111 = 0.001000000047497451305389404296875;
    float denominator = the_maximum_of_left_and_right_f32_f32(radius, tmp111);
    float tmp112 = 0.189999997615814208984375;
    float tmp113 = left1_4321_43right_f32_f32(radius, radius);
    float tmp114 = 0.02099999971687793731689453125;
    float tmp115 = left1_4331_43right_f32_f32(tmp113, tmp114);
    float tmp116 = left1_4371_43right_f32_f32(tmp112, tmp115);
    float tmp117 = 0.0350000001490116119384765625;
    float tmp118 = left1_4321_43right_f32_f32(time, tmp117);
    float angle = left1_4331_43right_f32_f32(tmp116, tmp118);
    float tmp119 = 4.599999904632568359375;
    angle = the_minimum_of_left_and_right_f32_f32(angle, tmp119);
    float sine = the_sine_of_value_f32(angle);
    float cosine = the_cosine_of_value_f32(angle);
    float tmp120 = 1.0;
    float tmp121 = 0.119999997317790985107421875;
    float tmp122 = left1_4321_43right_f32_f32(radius, radius);
    float tmp123 = 0.01200000010430812835693359375;
    float tmp124 = left1_4331_43right_f32_f32(tmp122, tmp123);
    float tmp125 = left1_4371_43right_f32_f32(tmp121, tmp124);
    float scale = left1_4331_43right_f32_f32(tmp120, tmp125);
    float tmp130 = position._m0.x;
    float tmp131 = left1_4321_43right_f32_f32(tmp130, cosine);
    float tmp135 = position._m0.y;
    float tmp136 = left1_4321_43right_f32_f32(tmp135, sine);
    float tmp137 = left1_4351_43right_f32_f32(tmp131, tmp136);
    vec2 _856 = vec2(0.0);
    _856.x = left1_4321_43right_f32_f32(tmp137, scale);
    float tmp142 = position._m0.x;
    float tmp143 = left1_4321_43right_f32_f32(tmp142, sine);
    float tmp147 = position._m0.y;
    float tmp148 = left1_4321_43right_f32_f32(tmp147, cosine);
    float tmp149 = left1_4331_43right_f32_f32(tmp143, tmp148);
    _856.y = left1_4321_43right_f32_f32(tmp149, scale);
    _class class_tmp126 = _class(vec2(0.0));
    class_tmp126._m0 = _856;
    _class universe = class_tmp126;
    float tmp157 = universe._m0.x;
    float tmp158 = 1.7999999523162841796875;
    float tmp159 = left1_4321_43right_f32_f32(tmp157, tmp158);
    float tmp160 = 0.02500000037252902984619140625;
    float tmp161 = left1_4321_43right_f32_f32(time, tmp160);
    vec2 _876 = vec2(0.0);
    _876.x = left1_4331_43right_f32_f32(tmp159, tmp161);
    float tmp166 = universe._m0.y;
    float tmp167 = 1.7999999523162841796875;
    float tmp168 = left1_4321_43right_f32_f32(tmp166, tmp167);
    float tmp169 = 4.0;
    _876.y = left1_4351_43right_f32_f32(tmp168, tmp169);
    _class class_tmp153 = _class(vec2(0.0));
    class_tmp153._m0 = _876;
    _class tmp173 = class_tmp153;
    float tmp174 = 1.7000000476837158203125;
    float primary = the_flowing_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp173, tmp174);
    float tmp179 = universe._m0.x;
    float tmp180 = 4.599999904632568359375;
    float tmp181 = left1_4321_43right_f32_f32(tmp179, tmp180);
    float tmp182 = 11.0;
    vec2 _891 = vec2(0.0);
    _891.x = left1_4351_43right_f32_f32(tmp181, tmp182);
    float tmp187 = universe._m0.y;
    float tmp188 = 4.599999904632568359375;
    float tmp189 = left1_4321_43right_f32_f32(tmp187, tmp188);
    float tmp190 = 0.017999999225139617919921875;
    float tmp191 = left1_4321_43right_f32_f32(time, tmp190);
    _891.y = left1_4331_43right_f32_f32(tmp189, tmp191);
    _class class_tmp175 = _class(vec2(0.0));
    class_tmp175._m0 = _891;
    _class tmp195 = class_tmp175;
    float tmp196 = 5.30000019073486328125;
    float secondary = the_flowing_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp195, tmp196);
    float tmp201 = universe._m0.x;
    float tmp202 = 2.400000095367431640625;
    float tmp203 = left1_4321_43right_f32_f32(tmp201, tmp202);
    float tmp204 = 7.0;
    vec2 _907 = vec2(0.0);
    _907.x = left1_4331_43right_f32_f32(tmp203, tmp204);
    float tmp209 = universe._m0.y;
    float tmp210 = 2.400000095367431640625;
    float tmp211 = left1_4321_43right_f32_f32(tmp209, tmp210);
    float tmp212 = 9.0;
    _907.y = left1_4351_43right_f32_f32(tmp211, tmp212);
    _class class_tmp197 = _class(vec2(0.0));
    class_tmp197._m0 = _907;
    _class tmp216 = class_tmp197;
    float tmp217 = 0.04500000178813934326171875;
    float tmp218 = left1_4321_43right_f32_f32(time, tmp217);
    float ridge = the_ridged_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp216, tmp218);
    float tmp222 = universe._m0.x;
    float tmp223 = 2.7000000476837158203125;
    float tmp224 = left1_4321_43right_f32_f32(tmp222, tmp223);
    float tmp228 = universe._m0.y;
    float tmp229 = 3.599999904632568359375;
    float tmp230 = left1_4321_43right_f32_f32(tmp228, tmp229);
    float tmp231 = left1_4351_43right_f32_f32(tmp224, tmp230);
    float tmp232 = 8.0;
    float tmp233 = left1_4321_43right_f32_f32(primary, tmp232);
    float phase = left1_4331_43right_f32_f32(tmp231, tmp233);
    float tmp234 = the_sine_of_value_f32(phase);
    float tmp235 = 0.5;
    float tmp236 = left1_4321_43right_f32_f32(tmp234, tmp235);
    float tmp237 = 0.5;
    float aurora = left1_4331_43right_f32_f32(tmp236, tmp237);
    float tmp238 = 0.449999988079071044921875;
    float tmp239 = 0.86000001430511474609375;
    float cloud = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp238, tmp239, primary);
    float tmp240 = 0.519999980926513671875;
    float tmp241 = 0.89999997615814208984375;
    float dust = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp240, tmp241, secondary);
    float tmp243 = 0.00200000009499490261077880859375;
    float tmp244 = 0.02199999988079071044921875;
    float tmp245 = left1_4321_43right_f32_f32(cloud, tmp244);
    float tmp246 = left1_4331_43right_f32_f32(tmp243, tmp245);
    float tmp247 = left1_4321_43right_f32_f32(ridge, aurora);
    float tmp248 = 0.07500000298023223876953125;
    float tmp249 = left1_4321_43right_f32_f32(tmp247, tmp248);
    vec3 _939 = vec3(0.0);
    _939.x = left1_4331_43right_f32_f32(tmp246, tmp249);
    float tmp251 = 0.0040000001899898052215576171875;
    float tmp252 = 0.0280000008642673492431640625;
    float tmp253 = left1_4321_43right_f32_f32(cloud, tmp252);
    float tmp254 = left1_4331_43right_f32_f32(tmp251, tmp253);
    float tmp255 = left1_4321_43right_f32_f32(dust, aurora);
    float tmp256 = 0.05200000107288360595703125;
    float tmp257 = left1_4321_43right_f32_f32(tmp255, tmp256);
    _939.y = left1_4331_43right_f32_f32(tmp254, tmp257);
    float tmp259 = 0.01400000043213367462158203125;
    float tmp260 = 0.12999999523162841796875;
    float tmp261 = left1_4321_43right_f32_f32(cloud, tmp260);
    float tmp262 = left1_4331_43right_f32_f32(tmp259, tmp261);
    float tmp263 = 1.0;
    float tmp264 = left1_4351_43right_f32_f32(tmp263, aurora);
    float tmp265 = left1_4321_43right_f32_f32(dust, tmp264);
    float tmp266 = 0.23999999463558197021484375;
    float tmp267 = left1_4321_43right_f32_f32(tmp265, tmp266);
    _939.z = left1_4331_43right_f32_f32(tmp262, tmp267);
    class_0 class_tmp242 = class_0(vec3(0.0));
    class_tmp242._m0 = _939;
    class_0 color = class_tmp242;
    float tmp271 = 13.0;
    float tmp272 = 0.0900000035762786865234375;
    float faraway = a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3floating5point_number8scale5_during_3floating5point_number8phase5_at_3floating5point_number8time5_a_point_f32_f32_f32(universe, tmp271, tmp272, time);
    float tmp273 = 21.0;
    float tmp274 = 0.4099999964237213134765625;
    float middle = a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3floating5point_number8scale5_during_3floating5point_number8phase5_at_3floating5point_number8time5_a_point_f32_f32_f32(universe, tmp273, tmp274, time);
    float tmp275 = 31.0;
    float tmp276 = 0.769999980926513671875;
    float nearby = a_moving_star_field_at_3planar_coordinate8position5_scaled_by_3floating5point_number8scale5_during_3floating5point_number8phase5_at_3floating5point_number8time5_a_point_f32_f32_f32(universe, tmp275, tmp276, time);
    float tmp277 = 0.4600000083446502685546875;
    float tmp278 = left1_4321_43right_f32_f32(faraway, tmp277);
    float tmp279 = 0.7599999904632568359375;
    float tmp280 = left1_4321_43right_f32_f32(middle, tmp279);
    float tmp281 = left1_4331_43right_f32_f32(tmp278, tmp280);
    float tmp282 = 1.13999998569488525390625;
    float tmp283 = left1_4321_43right_f32_f32(nearby, tmp282);
    float stars = left1_4331_43right_f32_f32(tmp281, tmp283);
    float tmp288 = universe._m0.x;
    float tmp289 = 5.099999904632568359375;
    vec2 _967 = vec2(0.0);
    _967.x = left1_4321_43right_f32_f32(tmp288, tmp289);
    float tmp294 = universe._m0.y;
    float tmp295 = 5.099999904632568359375;
    _967.y = left1_4321_43right_f32_f32(tmp294, tmp295);
    _class class_tmp284 = _class(vec2(0.0));
    class_tmp284._m0 = _967;
    _class tmp299 = class_tmp284;
    float tmp300 = 8.19999980926513671875;
    float temperature = the_flowing_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp299, tmp300);
    vec3 _977 = color._m0;
    float tmp305 = color._m0.x;
    float tmp306 = 0.579999983310699462890625;
    float tmp307 = 0.62000000476837158203125;
    float tmp308 = left1_4321_43right_f32_f32(temperature, tmp307);
    float tmp309 = left1_4331_43right_f32_f32(tmp306, tmp308);
    float tmp310 = left1_4321_43right_f32_f32(stars, tmp309);
    _977.x = left1_4331_43right_f32_f32(tmp305, tmp310);
    color._m0 = _977;
    vec3 _988 = color._m0;
    float tmp317 = color._m0.y;
    float tmp318 = 0.699999988079071044921875;
    float tmp319 = 0.310000002384185791015625;
    float tmp320 = left1_4321_43right_f32_f32(temperature, tmp319);
    float tmp321 = left1_4331_43right_f32_f32(tmp318, tmp320);
    float tmp322 = left1_4321_43right_f32_f32(stars, tmp321);
    _988.y = left1_4331_43right_f32_f32(tmp317, tmp322);
    color._m0 = _988;
    vec3 _999 = color._m0;
    float tmp330 = color._m0.z;
    float tmp331 = 1.25;
    float tmp332 = 0.180000007152557373046875;
    float tmp333 = left1_4321_43right_f32_f32(temperature, tmp332);
    float tmp334 = left1_4351_43right_f32_f32(tmp331, tmp333);
    float tmp335 = left1_4321_43right_f32_f32(stars, tmp334);
    _999.z = left1_4331_43right_f32_f32(tmp330, tmp335);
    color._m0 = _999;
    float tmp338 = 0.119999997317790985107421875;
    float tmp339 = 0.17000000178813934326171875;
    float tmp340 = left1_4321_43right_f32_f32(time, tmp339);
    float tmp341 = the_sine_of_value_f32(tmp340);
    float tmp342 = 0.017999999225139617919921875;
    float tmp343 = left1_4321_43right_f32_f32(tmp341, tmp342);
    float tilt = left1_4331_43right_f32_f32(tmp338, tmp343);
    sine = the_sine_of_value_f32(tilt);
    cosine = the_cosine_of_value_f32(tilt);
    float tmp348 = position._m0.x;
    float tmp349 = left1_4321_43right_f32_f32(tmp348, cosine);
    float tmp353 = position._m0.y;
    float tmp354 = left1_4321_43right_f32_f32(tmp353, sine);
    vec2 _1024 = vec2(0.0);
    _1024.x = left1_4331_43right_f32_f32(tmp349, tmp354);
    float tmp359 = position._m0.y;
    float tmp360 = left1_4321_43right_f32_f32(tmp359, cosine);
    float tmp364 = position._m0.x;
    float tmp365 = left1_4321_43right_f32_f32(tmp364, sine);
    _1024.y = left1_4351_43right_f32_f32(tmp360, tmp365);
    _class class_tmp344 = _class(vec2(0.0));
    class_tmp344._m0 = _1024;
    _class disk = class_tmp344;
    float tmp372 = disk._m0.y;
    float tmp373 = 7.19999980926513671875;
    float stretched = left1_4321_43right_f32_f32(tmp372, tmp373);
    float tmp377 = disk._m0.x;
    float tmp381 = disk._m0.x;
    float tmp382 = left1_4321_43right_f32_f32(tmp377, tmp381);
    float tmp383 = left1_4321_43right_f32_f32(stretched, stretched);
    float tmp384 = left1_4331_43right_f32_f32(tmp382, tmp383);
    float orbit = the_square_root_of_value_f32(tmp384);
    float tmp385 = 0.23000000417232513427734375;
    float tmp386 = 0.2849999964237213134765625;
    float inner = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp385, tmp386, orbit);
    float tmp387 = 1.0;
    float tmp388 = 0.7200000286102294921875;
    float tmp389 = 1.12000000476837158203125;
    float tmp390 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp388, tmp389, orbit);
    float outer = left1_4351_43right_f32_f32(tmp387, tmp390);
    float mask = left1_4321_43right_f32_f32(inner, outer);
    float tmp395 = disk._m0.x;
    float tmp396 = 8.30000019073486328125;
    float tmp397 = left1_4321_43right_f32_f32(tmp395, tmp396);
    float tmp398 = 0.519999980926513671875;
    float tmp399 = left1_4321_43right_f32_f32(time, tmp398);
    vec2 _1061 = vec2(0.0);
    _1061.x = left1_4331_43right_f32_f32(tmp397, tmp399);
    float tmp401 = 1.7999999523162841796875;
    _1061.y = left1_4321_43right_f32_f32(stretched, tmp401);
    _class class_tmp391 = _class(vec2(0.0));
    class_tmp391._m0 = _1061;
    _class tmp405 = class_tmp391;
    float tmp406 = 3.099999904632568359375;
    float turbulence = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp405, tmp406);
    float tmp411 = disk._m0.x;
    float tmp412 = 18.0;
    float tmp413 = left1_4321_43right_f32_f32(tmp411, tmp412);
    float tmp414 = 0.910000026226043701171875;
    float tmp415 = left1_4321_43right_f32_f32(time, tmp414);
    vec2 _1073 = vec2(0.0);
    _1073.x = left1_4351_43right_f32_f32(tmp413, tmp415);
    float tmp417 = 3.7000000476837158203125;
    _1073.y = left1_4321_43right_f32_f32(stretched, tmp417);
    _class class_tmp407 = _class(vec2(0.0));
    class_tmp407._m0 = _1073;
    _class tmp421 = class_tmp407;
    float tmp422 = 7.900000095367431640625;
    float detail = the_signed_flow_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp421, tmp422);
    float tmp423 = 39.0;
    float tmp424 = left1_4321_43right_f32_f32(orbit, tmp423);
    float tmp425 = 6.80000019073486328125;
    float tmp426 = left1_4321_43right_f32_f32(time, tmp425);
    float tmp427 = left1_4351_43right_f32_f32(tmp424, tmp426);
    float tmp428 = 7.400000095367431640625;
    float tmp429 = left1_4321_43right_f32_f32(turbulence, tmp428);
    float tmp430 = left1_4331_43right_f32_f32(tmp427, tmp429);
    float tmp431 = 2.2000000476837158203125;
    float tmp432 = left1_4321_43right_f32_f32(detail, tmp431);
    phase = left1_4331_43right_f32_f32(tmp430, tmp432);
    float tmp433 = the_sine_of_value_f32(phase);
    float tmp434 = 0.5;
    float tmp435 = left1_4321_43right_f32_f32(tmp433, tmp434);
    float tmp436 = 0.5;
    float spiral = left1_4331_43right_f32_f32(tmp435, tmp436);
    float tmp437 = 71.0;
    float tmp438 = left1_4321_43right_f32_f32(orbit, tmp437);
    float tmp439 = 10.3999996185302734375;
    float tmp440 = left1_4321_43right_f32_f32(time, tmp439);
    float tmp441 = left1_4351_43right_f32_f32(tmp438, tmp440);
    float tmp442 = 4.0;
    float tmp443 = left1_4321_43right_f32_f32(turbulence, tmp442);
    phase = left1_4351_43right_f32_f32(tmp441, tmp443);
    float tmp444 = the_cosine_of_value_f32(phase);
    float tmp445 = 0.5;
    float tmp446 = left1_4321_43right_f32_f32(tmp444, tmp445);
    float tmp447 = 0.5;
    float braid = left1_4331_43right_f32_f32(tmp446, tmp447);
    float tmp448 = 1.0;
    float tmp449 = 0.2899999916553497314453125;
    float tmp450 = 0.829999983310699462890625;
    float tmp451 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp449, tmp450, orbit);
    float heat = left1_4351_43right_f32_f32(tmp448, tmp451);
    float tmp452 = 0.189999997615814208984375;
    float tmp453 = 0.579999983310699462890625;
    float tmp454 = left1_4321_43right_f32_f32(spiral, tmp453);
    float tmp455 = left1_4331_43right_f32_f32(tmp452, tmp454);
    float tmp456 = 0.23000000417232513427734375;
    float tmp457 = left1_4321_43right_f32_f32(braid, tmp456);
    float tmp458 = left1_4331_43right_f32_f32(tmp455, tmp457);
    float tmp459 = left1_4321_43right_f32_f32(mask, tmp458);
    float tmp460 = 0.550000011920928955078125;
    float tmp461 = 4.400000095367431640625;
    float tmp462 = left1_4321_43right_f32_f32(heat, tmp461);
    float tmp463 = left1_4331_43right_f32_f32(tmp460, tmp462);
    float energy = left1_4321_43right_f32_f32(tmp459, tmp463);
    float tmp467 = disk._m0.x;
    float tmp468 = 0.001000000047497451305389404296875;
    float tmp469 = the_maximum_of_left_and_right_f32_f32(orbit, tmp468);
    float tmp470 = left1_4371_43right_f32_f32(tmp467, tmp469);
    float tmp471 = 0.5;
    float tmp472 = left1_4321_43right_f32_f32(tmp470, tmp471);
    float tmp473 = 0.5;
    float tmp474 = left1_4331_43right_f32_f32(tmp472, tmp473);
    float approaching = number_saturated_f32(tmp474);
    vec3 _1116 = color._m0;
    float tmp480 = color._m0.x;
    float tmp481 = 1.7200000286102294921875;
    float tmp482 = 0.62000000476837158203125;
    float tmp483 = left1_4321_43right_f32_f32(approaching, tmp482);
    float tmp484 = left1_4351_43right_f32_f32(tmp481, tmp483);
    float tmp485 = left1_4321_43right_f32_f32(energy, tmp484);
    _1116.x = left1_4331_43right_f32_f32(tmp480, tmp485);
    color._m0 = _1116;
    vec3 _1127 = color._m0;
    float tmp493 = color._m0.y;
    float tmp494 = 0.300000011920928955078125;
    float tmp495 = 1.03999996185302734375;
    float tmp496 = left1_4321_43right_f32_f32(approaching, tmp495);
    float tmp497 = left1_4331_43right_f32_f32(tmp494, tmp496);
    float tmp498 = left1_4321_43right_f32_f32(energy, tmp497);
    _1127.y = left1_4331_43right_f32_f32(tmp493, tmp498);
    color._m0 = _1127;
    vec3 _1138 = color._m0;
    float tmp506 = color._m0.z;
    float tmp507 = 0.054999999701976776123046875;
    float tmp508 = 2.1800000667572021484375;
    float tmp509 = left1_4321_43right_f32_f32(approaching, tmp508);
    float tmp510 = left1_4331_43right_f32_f32(tmp507, tmp509);
    float tmp511 = left1_4321_43right_f32_f32(energy, tmp510);
    _1138.z = left1_4331_43right_f32_f32(tmp506, tmp511);
    color._m0 = _1138;
    float tmp517 = position._m0.x;
    float axis = the_absolute_value_of_magnitude_f32(tmp517);
    float tmp518 = 0.0;
    float tmp519 = 0.0320000015199184417724609375;
    float core = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp518, tmp519, axis);
    float tmp520 = 0.017999999225139617919921875;
    float tmp521 = 0.1599999964237213134765625;
    float halo = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp520, tmp521, axis);
    float tmp522 = 0.189999997615814208984375;
    float tmp523 = 0.7200000286102294921875;
    float tmp527 = position._m0.y;
    float tmp528 = the_absolute_value_of_magnitude_f32(tmp527);
    float reach = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp522, tmp523, tmp528);
    float tmp533 = position._m0.x;
    float tmp534 = 15.0;
    vec2 _1163 = vec2(0.0);
    _1163.x = left1_4321_43right_f32_f32(tmp533, tmp534);
    float tmp539 = position._m0.y;
    float tmp540 = 3.0;
    float tmp541 = left1_4321_43right_f32_f32(tmp539, tmp540);
    float tmp542 = 0.800000011920928955078125;
    float tmp543 = left1_4321_43right_f32_f32(time, tmp542);
    _1163.y = left1_4351_43right_f32_f32(tmp541, tmp543);
    _class class_tmp529 = _class(vec2(0.0));
    class_tmp529._m0 = _1163;
    _class tmp547 = class_tmp529;
    float tmp548 = 10.0;
    float flicker = the_flowing_field_at_3a_point8point5_during_3a_value8phase5_a_point_f32(tmp547, tmp548);
    float tmp549 = 0.519999980926513671875;
    float tmp550 = left1_4321_43right_f32_f32(core, tmp549);
    float tmp551 = 0.12999999523162841796875;
    float tmp552 = left1_4321_43right_f32_f32(halo, tmp551);
    float tmp553 = left1_4331_43right_f32_f32(tmp550, tmp552);
    float tmp554 = left1_4321_43right_f32_f32(tmp553, reach);
    float tmp555 = 0.3499999940395355224609375;
    float tmp556 = 0.64999997615814208984375;
    float tmp557 = left1_4321_43right_f32_f32(flicker, tmp556);
    float tmp558 = left1_4331_43right_f32_f32(tmp555, tmp557);
    float jet = left1_4321_43right_f32_f32(tmp554, tmp558);
    vec3 _1182 = color._m0;
    float tmp564 = color._m0.x;
    float tmp565 = 0.20999999344348907470703125;
    float tmp566 = left1_4321_43right_f32_f32(jet, tmp565);
    _1182.x = left1_4331_43right_f32_f32(tmp564, tmp566);
    color._m0 = _1182;
    vec3 _1191 = color._m0;
    float tmp574 = color._m0.y;
    float tmp575 = 0.4799999892711639404296875;
    float tmp576 = left1_4321_43right_f32_f32(jet, tmp575);
    _1191.y = left1_4331_43right_f32_f32(tmp574, tmp576);
    color._m0 = _1191;
    vec3 _1200 = color._m0;
    float tmp584 = color._m0.z;
    float tmp585 = 1.36000001430511474609375;
    float tmp586 = left1_4321_43right_f32_f32(jet, tmp585);
    _1200.z = left1_4331_43right_f32_f32(tmp584, tmp586);
    color._m0 = _1200;
    float tmp589 = 1.0;
    float tmp590 = 0.17599999904632568359375;
    float tmp591 = 0.20200000703334808349609375;
    float tmp592 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp590, tmp591, radius);
    float horizon = left1_4351_43right_f32_f32(tmp589, tmp592);
    vec3 _1211 = color._m0;
    float tmp598 = color._m0.x;
    float tmp599 = 1.0;
    float tmp600 = left1_4351_43right_f32_f32(tmp599, horizon);
    _1211.x = left1_4321_43right_f32_f32(tmp598, tmp600);
    color._m0 = _1211;
    vec3 _1220 = color._m0;
    float tmp608 = color._m0.y;
    float tmp609 = 1.0;
    float tmp610 = left1_4351_43right_f32_f32(tmp609, horizon);
    _1220.y = left1_4321_43right_f32_f32(tmp608, tmp610);
    color._m0 = _1220;
    vec3 _1229 = color._m0;
    float tmp618 = color._m0.z;
    float tmp619 = 1.0;
    float tmp620 = left1_4351_43right_f32_f32(tmp619, horizon);
    _1229.z = left1_4321_43right_f32_f32(tmp618, tmp620);
    color._m0 = _1229;
    float tmp623 = 1.0;
    float tmp624 = 0.01899999938905239105224609375;
    float tmp625 = _the_negative_of_4the_opposite_of_453value_f32(tmp624);
    float tmp626 = 0.014999999664723873138427734375;
    float tmp630 = disk._m0.y;
    float tmp631 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp625, tmp626, tmp630);
    float front = left1_4351_43right_f32_f32(tmp623, tmp631);
    float foreground = left1_4321_43right_f32_f32(energy, front);
    vec3 _1245 = color._m0;
    float tmp637 = color._m0.x;
    float tmp638 = 1.61000001430511474609375;
    float tmp639 = 0.4799999892711639404296875;
    float tmp640 = left1_4321_43right_f32_f32(approaching, tmp639);
    float tmp641 = left1_4351_43right_f32_f32(tmp638, tmp640);
    float tmp642 = left1_4321_43right_f32_f32(foreground, tmp641);
    _1245.x = left1_4331_43right_f32_f32(tmp637, tmp642);
    color._m0 = _1245;
    vec3 _1256 = color._m0;
    float tmp650 = color._m0.y;
    float tmp651 = 0.3400000035762786865234375;
    float tmp652 = 0.930000007152557373046875;
    float tmp653 = left1_4321_43right_f32_f32(approaching, tmp652);
    float tmp654 = left1_4331_43right_f32_f32(tmp651, tmp653);
    float tmp655 = left1_4321_43right_f32_f32(foreground, tmp654);
    _1256.y = left1_4331_43right_f32_f32(tmp650, tmp655);
    color._m0 = _1256;
    vec3 _1267 = color._m0;
    float tmp663 = color._m0.z;
    float tmp664 = 0.070000000298023223876953125;
    float tmp665 = 1.940000057220458984375;
    float tmp666 = left1_4321_43right_f32_f32(approaching, tmp665);
    float tmp667 = left1_4331_43right_f32_f32(tmp664, tmp666);
    float tmp668 = left1_4321_43right_f32_f32(foreground, tmp667);
    _1267.z = left1_4331_43right_f32_f32(tmp663, tmp668);
    color._m0 = _1267;
    float tmp671 = 0.21600000560283660888671875;
    float tmp672 = left1_4351_43right_f32_f32(radius, tmp671);
    float _distance = the_absolute_value_of_magnitude_f32(tmp672);
    float tmp673 = 0.001000000047497451305389404296875;
    float tmp674 = 0.0170000009238719940185546875;
    float ring = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp673, tmp674, _distance);
    float tmp678 = position._m0.y;
    float tmp679 = left1_4371_43right_f32_f32(tmp678, denominator);
    float poles = the_absolute_value_of_magnitude_f32(tmp679);
    float tmp680 = 0.1599999964237213134765625;
    float tmp681 = 0.86000001430511474609375;
    float tmp682 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp680, tmp681, poles);
    float arcs = left1_4321_43right_f32_f32(ring, tmp682);
    float tmp683 = 2.099999904632568359375;
    float tmp684 = left1_4321_43right_f32_f32(time, tmp683);
    float tmp685 = 5.0;
    float tmp686 = left1_4321_43right_f32_f32(turbulence, tmp685);
    float tmp687 = left1_4331_43right_f32_f32(tmp684, tmp686);
    float tmp688 = the_sine_of_value_f32(tmp687);
    float tmp689 = 0.100000001490116119384765625;
    float tmp690 = left1_4321_43right_f32_f32(tmp688, tmp689);
    float tmp691 = 0.89999997615814208984375;
    flicker = left1_4331_43right_f32_f32(tmp690, tmp691);
    vec3 _1294 = color._m0;
    float tmp697 = color._m0.x;
    float tmp698 = 1.34000003337860107421875;
    float tmp699 = left1_4321_43right_f32_f32(ring, tmp698);
    float tmp700 = left1_4321_43right_f32_f32(tmp699, flicker);
    float tmp701 = left1_4331_43right_f32_f32(tmp697, tmp700);
    float tmp702 = 2.2000000476837158203125;
    float tmp703 = left1_4321_43right_f32_f32(arcs, tmp702);
    _1294.x = left1_4331_43right_f32_f32(tmp701, tmp703);
    color._m0 = _1294;
    vec3 _1306 = color._m0;
    float tmp711 = color._m0.y;
    float tmp712 = 0.7200000286102294921875;
    float tmp713 = left1_4321_43right_f32_f32(ring, tmp712);
    float tmp714 = left1_4321_43right_f32_f32(tmp713, flicker);
    float tmp715 = left1_4331_43right_f32_f32(tmp711, tmp714);
    float tmp716 = 0.930000007152557373046875;
    float tmp717 = left1_4321_43right_f32_f32(arcs, tmp716);
    _1306.y = left1_4331_43right_f32_f32(tmp715, tmp717);
    color._m0 = _1306;
    vec3 _1318 = color._m0;
    float tmp725 = color._m0.z;
    float tmp726 = 0.439999997615814208984375;
    float tmp727 = left1_4321_43right_f32_f32(ring, tmp726);
    float tmp728 = left1_4321_43right_f32_f32(tmp727, flicker);
    float tmp729 = left1_4331_43right_f32_f32(tmp725, tmp728);
    float tmp730 = 1.480000019073486328125;
    float tmp731 = left1_4321_43right_f32_f32(arcs, tmp730);
    _1318.z = left1_4331_43right_f32_f32(tmp729, tmp731);
    color._m0 = _1318;
    float tmp734 = 0.20200000703334808349609375;
    float tmp735 = left1_4351_43right_f32_f32(radius, tmp734);
    float gap = the_absolute_value_of_magnitude_f32(tmp735);
    float tmp736 = 0.039999999105930328369140625;
    float tmp737 = 0.039999999105930328369140625;
    float tmp738 = left1_4331_43right_f32_f32(gap, tmp737);
    float tmp739 = left1_4371_43right_f32_f32(tmp736, tmp738);
    float tmp740 = 1.0;
    float tmp741 = left1_4351_43right_f32_f32(tmp740, horizon);
    halo = left1_4321_43right_f32_f32(tmp739, tmp741);
    vec3 _1336 = color._m0;
    float tmp747 = color._m0.x;
    float tmp748 = 0.189999997615814208984375;
    float tmp749 = left1_4321_43right_f32_f32(halo, tmp748);
    _1336.x = left1_4331_43right_f32_f32(tmp747, tmp749);
    color._m0 = _1336;
    vec3 _1345 = color._m0;
    float tmp757 = color._m0.y;
    float tmp758 = 0.070000000298023223876953125;
    float tmp759 = left1_4321_43right_f32_f32(halo, tmp758);
    _1345.y = left1_4331_43right_f32_f32(tmp757, tmp759);
    color._m0 = _1345;
    vec3 _1354 = color._m0;
    float tmp767 = color._m0.z;
    float tmp768 = 0.310000002384185791015625;
    float tmp769 = left1_4321_43right_f32_f32(halo, tmp768);
    _1354.z = left1_4331_43right_f32_f32(tmp767, tmp769);
    color._m0 = _1354;
    float tmp775 = screen._m0.x;
    float tmp776 = left1_4371_43right_f32_f32(tmp775, aspect);
    float tmp780 = screen._m0.x;
    float tmp781 = left1_4371_43right_f32_f32(tmp780, aspect);
    float tmp782 = left1_4321_43right_f32_f32(tmp776, tmp781);
    float tmp786 = screen._m0.y;
    float tmp790 = screen._m0.y;
    float tmp791 = left1_4321_43right_f32_f32(tmp786, tmp790);
    float tmp792 = left1_4331_43right_f32_f32(tmp782, tmp791);
    float edge = the_square_root_of_value_f32(tmp792);
    float tmp793 = 0.180000007152557373046875;
    float tmp794 = 1.0;
    float tmp795 = 0.4799999892711639404296875;
    float tmp796 = 1.34000003337860107421875;
    float tmp797 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp795, tmp796, edge);
    float tmp798 = left1_4351_43right_f32_f32(tmp794, tmp797);
    float tmp799 = 0.819999992847442626953125;
    float tmp800 = left1_4321_43right_f32_f32(tmp798, tmp799);
    float vignette = left1_4331_43right_f32_f32(tmp793, tmp800);
    vec3 _1385 = color._m0;
    float tmp806 = color._m0.x;
    float tmp807 = left1_4321_43right_f32_f32(tmp806, vignette);
    float tmp808 = 1.0;
    float tmp812 = color._m0.x;
    float tmp813 = left1_4331_43right_f32_f32(tmp808, tmp812);
    float tmp814 = left1_4371_43right_f32_f32(tmp807, tmp813);
    _1385.x = the_square_root_of_value_f32(tmp814);
    color._m0 = _1385;
    vec3 _1399 = color._m0;
    float tmp822 = color._m0.y;
    float tmp823 = left1_4321_43right_f32_f32(tmp822, vignette);
    float tmp824 = 1.0;
    float tmp828 = color._m0.y;
    float tmp829 = left1_4331_43right_f32_f32(tmp824, tmp828);
    float tmp830 = left1_4371_43right_f32_f32(tmp823, tmp829);
    _1399.y = the_square_root_of_value_f32(tmp830);
    color._m0 = _1399;
    vec3 _1413 = color._m0;
    float tmp838 = color._m0.z;
    float tmp839 = left1_4321_43right_f32_f32(tmp838, vignette);
    float tmp840 = 1.0;
    float tmp844 = color._m0.z;
    float tmp845 = left1_4331_43right_f32_f32(tmp840, tmp844);
    float tmp846 = left1_4371_43right_f32_f32(tmp839, tmp845);
    _1413.z = the_square_root_of_value_f32(tmp846);
    color._m0 = _1413;
    vec4 _1429 = vec4(0.0);
    _1429.x = color._m0.x;
    _1429.y = color._m0.y;
    _1429.z = color._m0.z;
    _1429.w = 1.0;
    dynlexColor = _1429;
}
