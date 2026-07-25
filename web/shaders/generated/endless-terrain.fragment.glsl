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

float _the43_sine_of_value_f32(float value)
{
    return sin(value);
}

float _the43_cosine_of_value_f32(float value)
{
    return cos(value);
}

float left1_4331_43right_f32_f32(float left, float right)
{
    return left + right;
}

float _the43_square_root_of_value_f32(float value)
{
    return sqrt(value);
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

float glow_from_inner_to_outer_at_sample_f32_f32_f32(float inner, float outer, float _sample)
{
    float tmp = 1.0;
    float tmp1 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(inner, outer, _sample);
    return left1_4351_43right_f32_f32(tmp, tmp1);
}

bool value_as_destinationtype_i32_type_ct_destinationtype_type(uint value)
{
    return value != 0u;
}

float terrain_height_at_x_z_f32_f32(float x, float z)
{
    float tmp = 0.082999996840953826904296875;
    float tmp1 = left1_4321_43right_f32_f32(x, tmp);
    float tmp2 = 0.046999998390674591064453125;
    float tmp3 = left1_4321_43right_f32_f32(z, tmp2);
    float tmp4 = left1_4331_43right_f32_f32(tmp1, tmp3);
    float tmp5 = _the43_sine_of_value_f32(tmp4);
    float tmp6 = 0.0309999994933605194091796875;
    float tmp7 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp6);
    float tmp8 = left1_4321_43right_f32_f32(x, tmp7);
    float tmp9 = 0.071000002324581146240234375;
    float tmp10 = left1_4321_43right_f32_f32(z, tmp9);
    float tmp11 = left1_4331_43right_f32_f32(tmp8, tmp10);
    float tmp12 = _the43_cosine_of_value_f32(tmp11);
    float tmp13 = 0.5299999713897705078125;
    float tmp14 = left1_4321_43right_f32_f32(tmp12, tmp13);
    float warp_x = left1_4331_43right_f32_f32(tmp5, tmp14);
    float tmp15 = 0.0570000000298023223876953125;
    float tmp16 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp15);
    float tmp17 = left1_4321_43right_f32_f32(x, tmp16);
    float tmp18 = 0.0610000006854534149169921875;
    float tmp19 = left1_4321_43right_f32_f32(z, tmp18);
    float tmp20 = left1_4331_43right_f32_f32(tmp17, tmp19);
    float tmp21 = _the43_cosine_of_value_f32(tmp20);
    float tmp22 = 0.068999998271465301513671875;
    float tmp23 = left1_4321_43right_f32_f32(x, tmp22);
    float tmp24 = 0.02899999916553497314453125;
    float tmp25 = left1_4321_43right_f32_f32(z, tmp24);
    float tmp26 = left1_4331_43right_f32_f32(tmp23, tmp25);
    float tmp27 = _the43_sine_of_value_f32(tmp26);
    float tmp28 = 0.4699999988079071044921875;
    float tmp29 = left1_4321_43right_f32_f32(tmp27, tmp28);
    float warp_z = left1_4331_43right_f32_f32(tmp21, tmp29);
    float tmp30 = 3.400000095367431640625;
    float tmp31 = left1_4321_43right_f32_f32(warp_x, tmp30);
    float bent_x = left1_4331_43right_f32_f32(x, tmp31);
    float tmp32 = 3.400000095367431640625;
    float tmp33 = left1_4321_43right_f32_f32(warp_z, tmp32);
    float bent_z = left1_4331_43right_f32_f32(z, tmp33);
    float tmp34 = 0.07299999892711639404296875;
    float tmp35 = left1_4321_43right_f32_f32(bent_x, tmp34);
    float tmp36 = 0.05099999904632568359375;
    float tmp37 = left1_4321_43right_f32_f32(bent_z, tmp36);
    float tmp38 = left1_4331_43right_f32_f32(tmp35, tmp37);
    float tmp39 = _the43_sine_of_value_f32(tmp38);
    float tmp40 = 0.62000000476837158203125;
    float continental = left1_4321_43right_f32_f32(tmp39, tmp40);
    float tmp41 = 0.0489999987185001373291015625;
    float tmp42 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp41);
    float tmp43 = left1_4321_43right_f32_f32(bent_x, tmp42);
    float tmp44 = 0.090999998152256011962890625;
    float tmp45 = left1_4321_43right_f32_f32(bent_z, tmp44);
    float tmp46 = left1_4331_43right_f32_f32(tmp43, tmp45);
    float tmp47 = _the43_cosine_of_value_f32(tmp46);
    float tmp48 = 0.3400000035762786865234375;
    float crossing = left1_4321_43right_f32_f32(tmp47, tmp48);
    float tmp49 = 0.17000000178813934326171875;
    float tmp50 = left1_4321_43right_f32_f32(bent_x, tmp49);
    float tmp51 = 0.10999999940395355224609375;
    float tmp52 = left1_4321_43right_f32_f32(bent_z, tmp51);
    float tmp53 = left1_4351_43right_f32_f32(tmp50, tmp52);
    float tmp54 = 0.310000002384185791015625;
    float tmp55 = left1_4321_43right_f32_f32(warp_z, tmp54);
    float tmp56 = left1_4331_43right_f32_f32(tmp53, tmp55);
    float tmp57 = _the43_cosine_of_value_f32(tmp56);
    float tmp58 = 0.5;
    float tmp59 = left1_4321_43right_f32_f32(tmp57, tmp58);
    float tmp60 = 0.5;
    float ridge_one = left1_4331_43right_f32_f32(tmp59, tmp60);
    float tmp61 = 0.12999999523162841796875;
    float tmp62 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp61);
    float tmp63 = left1_4321_43right_f32_f32(bent_x, tmp62);
    float tmp64 = 0.23000000417232513427734375;
    float tmp65 = left1_4321_43right_f32_f32(bent_z, tmp64);
    float tmp66 = left1_4351_43right_f32_f32(tmp63, tmp65);
    float tmp67 = 0.2700000107288360595703125;
    float tmp68 = left1_4321_43right_f32_f32(warp_x, tmp67);
    float tmp69 = left1_4351_43right_f32_f32(tmp66, tmp68);
    float tmp70 = _the43_cosine_of_value_f32(tmp69);
    float tmp71 = 0.5;
    float tmp72 = left1_4321_43right_f32_f32(tmp70, tmp71);
    float tmp73 = 0.5;
    float ridge_two = left1_4331_43right_f32_f32(tmp72, tmp73);
    float tmp74 = 0.2899999916553497314453125;
    float tmp75 = left1_4321_43right_f32_f32(bent_x, tmp74);
    float tmp76 = 0.070000000298023223876953125;
    float tmp77 = left1_4321_43right_f32_f32(bent_z, tmp76);
    float tmp78 = left1_4331_43right_f32_f32(tmp75, tmp77);
    float tmp79 = 0.189999997615814208984375;
    float tmp80 = left1_4321_43right_f32_f32(warp_z, tmp79);
    float tmp81 = left1_4331_43right_f32_f32(tmp78, tmp80);
    float tmp82 = _the43_cosine_of_value_f32(tmp81);
    float tmp83 = 0.5;
    float tmp84 = left1_4321_43right_f32_f32(tmp82, tmp83);
    float tmp85 = 0.5;
    float ridge_three = left1_4331_43right_f32_f32(tmp84, tmp85);
    float tmp86 = left1_4321_43right_f32_f32(ridge_one, ridge_one);
    float tmp87 = left1_4321_43right_f32_f32(ridge_one, ridge_one);
    ridge_one = left1_4321_43right_f32_f32(tmp86, tmp87);
    float tmp88 = left1_4321_43right_f32_f32(ridge_two, ridge_two);
    float tmp89 = left1_4321_43right_f32_f32(ridge_two, ridge_two);
    ridge_two = left1_4321_43right_f32_f32(tmp88, tmp89);
    float tmp90 = left1_4321_43right_f32_f32(ridge_three, ridge_three);
    float tmp91 = left1_4321_43right_f32_f32(ridge_three, ridge_three);
    ridge_three = left1_4321_43right_f32_f32(tmp90, tmp91);
    float tmp92 = 2.0799999237060546875;
    float tmp93 = left1_4321_43right_f32_f32(ridge_one, tmp92);
    float tmp94 = 1.46000003814697265625;
    float tmp95 = left1_4321_43right_f32_f32(ridge_two, tmp94);
    float tmp96 = left1_4331_43right_f32_f32(tmp93, tmp95);
    float tmp97 = 0.920000016689300537109375;
    float tmp98 = left1_4321_43right_f32_f32(ridge_three, tmp97);
    float alpine_peaks = left1_4331_43right_f32_f32(tmp96, tmp98);
    float tmp99 = 0.4600000083446502685546875;
    float tmp100 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp99);
    float tmp101 = 0.540000021457672119140625;
    float tmp102 = left1_4331_43right_f32_f32(continental, crossing);
    float mountain_mask = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp100, tmp101, tmp102);
    float tmp103 = 0.310000002384185791015625;
    float tmp104 = 0.7799999713897705078125;
    float tmp105 = left1_4321_43right_f32_f32(mountain_mask, tmp104);
    float tmp106 = left1_4331_43right_f32_f32(tmp103, tmp105);
    float peak_height = left1_4321_43right_f32_f32(alpine_peaks, tmp106);
    float tmp107 = 0.5099999904632568359375;
    float tmp108 = left1_4321_43right_f32_f32(bent_x, tmp107);
    float tmp109 = 0.37000000476837158203125;
    float tmp110 = left1_4321_43right_f32_f32(bent_z, tmp109);
    float tmp111 = left1_4331_43right_f32_f32(tmp108, tmp110);
    float tmp112 = _the43_sine_of_value_f32(tmp111);
    float tmp113 = 0.100000001490116119384765625;
    float detail_one = left1_4321_43right_f32_f32(tmp112, tmp113);
    float tmp114 = 0.829999983310699462890625;
    float tmp115 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp114);
    float tmp116 = left1_4321_43right_f32_f32(bent_x, tmp115);
    float tmp117 = 0.61000001430511474609375;
    float tmp118 = left1_4321_43right_f32_f32(bent_z, tmp117);
    float tmp119 = left1_4331_43right_f32_f32(tmp116, tmp118);
    float tmp120 = _the43_cosine_of_value_f32(tmp119);
    float tmp121 = 0.04500000178813934326171875;
    float detail_two = left1_4321_43right_f32_f32(tmp120, tmp121);
    float tmp122 = 0.310000002384185791015625;
    float tmp123 = left1_4331_43right_f32_f32(tmp122, continental);
    float tmp124 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp123);
    float tmp125 = 0.540000021457672119140625;
    float tmp126 = left1_4321_43right_f32_f32(crossing, tmp125);
    float tmp127 = left1_4331_43right_f32_f32(tmp124, tmp126);
    float tmp128 = left1_4331_43right_f32_f32(tmp127, peak_height);
    float tmp129 = left1_4331_43right_f32_f32(tmp128, detail_one);
    return left1_4331_43right_f32_f32(tmp129, detail_two);
}

bool left_0_right_i32_i32(uint left, uint right)
{
    return int(left) < int(right);
}

bool _boolean8left5_and_3boolean8right5_bool_bool(bool left, bool right)
{
    return left && right;
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
    float tmp15 = 0.12999999523162841796875;
    float tmp16 = left1_4321_43right_f32_f32(time, tmp15);
    float tmp17 = _the43_sine_of_value_f32(tmp16);
    float tmp18 = 3.099999904632568359375;
    float tmp19 = left1_4321_43right_f32_f32(tmp17, tmp18);
    float tmp20 = 0.071000002324581146240234375;
    float tmp21 = left1_4321_43right_f32_f32(time, tmp20);
    float tmp22 = _the43_cosine_of_value_f32(tmp21);
    float tmp23 = 1.39999997615814208984375;
    float tmp24 = left1_4321_43right_f32_f32(tmp22, tmp23);
    float camera_x = left1_4331_43right_f32_f32(tmp19, tmp24);
    float tmp25 = 3.2599999904632568359375;
    float tmp26 = 0.189999997615814208984375;
    float tmp27 = left1_4321_43right_f32_f32(time, tmp26);
    float tmp28 = _the43_sine_of_value_f32(tmp27);
    float tmp29 = 0.12999999523162841796875;
    float tmp30 = left1_4321_43right_f32_f32(tmp28, tmp29);
    float camera_y = left1_4331_43right_f32_f32(tmp25, tmp30);
    float tmp31 = 76.0;
    float tmp32 = 3.0499999523162841796875;
    float tmp33 = left1_4321_43right_f32_f32(time, tmp32);
    float camera_z = left1_4331_43right_f32_f32(tmp31, tmp33);
    float tmp34 = 0.09700000286102294921875;
    float tmp35 = left1_4321_43right_f32_f32(time, tmp34);
    float tmp36 = _the43_sine_of_value_f32(tmp35);
    float tmp37 = 0.1599999964237213134765625;
    float tmp38 = left1_4321_43right_f32_f32(tmp36, tmp37);
    float tmp39 = 0.0610000006854534149169921875;
    float tmp40 = left1_4321_43right_f32_f32(time, tmp39);
    float tmp41 = _the43_cosine_of_value_f32(tmp40);
    float tmp42 = 0.070000000298023223876953125;
    float tmp43 = left1_4321_43right_f32_f32(tmp41, tmp42);
    float yaw = left1_4331_43right_f32_f32(tmp38, tmp43);
    float yaw_sine = _the43_sine_of_value_f32(yaw);
    float yaw_cosine = _the43_cosine_of_value_f32(yaw);
    float tmp44 = 0.189999997615814208984375;
    float tmp45 = left1_4321_43right_f32_f32(time, tmp44);
    float tmp46 = _the43_sine_of_value_f32(tmp45);
    float tmp47 = 0.02500000037252902984619140625;
    float bank = left1_4321_43right_f32_f32(tmp46, tmp47);
    float bank_sine = _the43_sine_of_value_f32(bank);
    float bank_cosine = _the43_cosine_of_value_f32(bank);
    float tmp48 = left1_4321_43right_f32_f32(screen_x, bank_cosine);
    float tmp49 = left1_4321_43right_f32_f32(screen_y, bank_sine);
    float view_x = left1_4351_43right_f32_f32(tmp48, tmp49);
    float tmp50 = left1_4321_43right_f32_f32(screen_x, bank_sine);
    float tmp51 = left1_4321_43right_f32_f32(screen_y, bank_cosine);
    float view_y = left1_4331_43right_f32_f32(tmp50, tmp51);
    float tmp52 = 0.680000007152557373046875;
    float raw_direction_x = left1_4321_43right_f32_f32(view_x, tmp52);
    float tmp53 = 0.680000007152557373046875;
    float tmp54 = left1_4321_43right_f32_f32(view_y, tmp53);
    float tmp55 = 0.189999997615814208984375;
    float raw_direction_y = left1_4351_43right_f32_f32(tmp54, tmp55);
    float raw_direction_z = 1.2400000095367431640625;
    float tmp56 = left1_4321_43right_f32_f32(raw_direction_x, yaw_cosine);
    float tmp57 = left1_4321_43right_f32_f32(raw_direction_z, yaw_sine);
    float direction_x = left1_4331_43right_f32_f32(tmp56, tmp57);
    float tmp58 = left1_4321_43right_f32_f32(raw_direction_z, yaw_cosine);
    float tmp59 = left1_4321_43right_f32_f32(raw_direction_x, yaw_sine);
    float direction_z = left1_4351_43right_f32_f32(tmp58, tmp59);
    float tmp60 = left1_4321_43right_f32_f32(direction_x, direction_x);
    float tmp61 = left1_4321_43right_f32_f32(raw_direction_y, raw_direction_y);
    float tmp62 = left1_4331_43right_f32_f32(tmp60, tmp61);
    float tmp63 = left1_4321_43right_f32_f32(direction_z, direction_z);
    float tmp64 = left1_4331_43right_f32_f32(tmp62, tmp63);
    float direction_length = _the43_square_root_of_value_f32(tmp64);
    direction_x = left1_4371_43right_f32_f32(direction_x, direction_length);
    float direction_y = left1_4371_43right_f32_f32(raw_direction_y, direction_length);
    direction_z = left1_4371_43right_f32_f32(direction_z, direction_length);
    float tmp65 = 0.540000021457672119140625;
    float tmp66 = left1_4321_43right_f32_f32(screen_y, tmp65);
    float tmp67 = 0.449999988079071044921875;
    float tmp68 = left1_4331_43right_f32_f32(tmp66, tmp67);
    float sky_height = saturate_number_f32(tmp68);
    float tmp69 = 0.017999999225139617919921875;
    float tmp70 = 0.07500000298023223876953125;
    float tmp71 = left1_4321_43right_f32_f32(sky_height, tmp70);
    float red = left1_4331_43right_f32_f32(tmp69, tmp71);
    float tmp72 = 0.05200000107288360595703125;
    float tmp73 = 0.1599999964237213134765625;
    float tmp74 = left1_4321_43right_f32_f32(sky_height, tmp73);
    float green = left1_4331_43right_f32_f32(tmp72, tmp74);
    float tmp75 = 0.119999997317790985107421875;
    float tmp76 = 0.3400000035762786865234375;
    float tmp77 = left1_4321_43right_f32_f32(sky_height, tmp76);
    float blue = left1_4331_43right_f32_f32(tmp75, tmp77);
    float tmp78 = 1.25;
    float tmp79 = left1_4321_43right_f32_f32(screen_x, tmp78);
    float tmp80 = 0.017999999225139617919921875;
    float tmp81 = left1_4321_43right_f32_f32(time, tmp80);
    float tmp82 = left1_4331_43right_f32_f32(tmp79, tmp81);
    float tmp83 = 2.400000095367431640625;
    float tmp84 = left1_4321_43right_f32_f32(screen_y, tmp83);
    float tmp85 = 3.0;
    float tmp86 = left1_4331_43right_f32_f32(tmp84, tmp85);
    float tmp87 = 2.099999904632568359375;
    float cloud_broad = flowing_field_at_x_y_phase_f32_f32_f32(tmp82, tmp86, tmp87);
    float tmp88 = 2.099999904632568359375;
    float tmp89 = left1_4321_43right_f32_f32(screen_x, tmp88);
    float tmp90 = 8.0;
    float tmp91 = left1_4351_43right_f32_f32(tmp89, tmp90);
    float tmp92 = 3.7000000476837158203125;
    float tmp93 = left1_4321_43right_f32_f32(screen_y, tmp92);
    float tmp94 = 0.01200000010430812835693359375;
    float tmp95 = left1_4321_43right_f32_f32(time, tmp94);
    float tmp96 = left1_4331_43right_f32_f32(tmp93, tmp95);
    float tmp97 = 5.69999980926513671875;
    float cloud_ridges = ridged_field_at_x_y_phase_f32_f32_f32(tmp91, tmp96, tmp97);
    float tmp98 = 0.540000021457672119140625;
    float tmp99 = 0.819999992847442626953125;
    float tmp100 = 0.7599999904632568359375;
    float tmp101 = left1_4321_43right_f32_f32(cloud_broad, tmp100);
    float tmp102 = 0.23999999463558197021484375;
    float tmp103 = left1_4321_43right_f32_f32(cloud_ridges, tmp102);
    float tmp104 = left1_4331_43right_f32_f32(tmp101, tmp103);
    float cloud_shape = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp98, tmp99, tmp104);
    float tmp105 = 0.0500000007450580596923828125;
    float tmp106 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp105);
    float tmp107 = 0.310000002384185791015625;
    float cloud_altitude = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp106, tmp107, screen_y);
    float cloud_light = left1_4321_43right_f32_f32(cloud_shape, cloud_altitude);
    float tmp108 = 0.3400000035762786865234375;
    float tmp109 = left1_4321_43right_f32_f32(cloud_light, tmp108);
    red = left1_4331_43right_f32_f32(red, tmp109);
    float tmp110 = 0.310000002384185791015625;
    float tmp111 = left1_4321_43right_f32_f32(cloud_light, tmp110);
    green = left1_4331_43right_f32_f32(green, tmp111);
    float tmp112 = 0.2700000107288360595703125;
    float tmp113 = left1_4321_43right_f32_f32(cloud_light, tmp112);
    blue = left1_4331_43right_f32_f32(blue, tmp113);
    float tmp114 = 0.560000002384185791015625;
    float sun_x = left1_4351_43right_f32_f32(screen_x, tmp114);
    float tmp115 = 0.38999998569488525390625;
    float sun_y = left1_4351_43right_f32_f32(screen_y, tmp115);
    float tmp116 = left1_4321_43right_f32_f32(sun_x, sun_x);
    float tmp117 = left1_4321_43right_f32_f32(sun_y, sun_y);
    float tmp118 = left1_4331_43right_f32_f32(tmp116, tmp117);
    float sun_distance = _the43_square_root_of_value_f32(tmp118);
    float tmp119 = 0.017999999225139617919921875;
    float tmp120 = 0.07500000298023223876953125;
    float sun_core = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp119, tmp120, sun_distance);
    float tmp121 = 0.0599999986588954925537109375;
    float tmp122 = 0.4199999868869781494140625;
    float sun_halo = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp121, tmp122, sun_distance);
    float tmp123 = 0.2700000107288360595703125;
    float tmp124 = left1_4321_43right_f32_f32(sun_halo, tmp123);
    float tmp125 = left1_4331_43right_f32_f32(red, tmp124);
    float tmp126 = 1.2999999523162841796875;
    float tmp127 = left1_4321_43right_f32_f32(sun_core, tmp126);
    red = left1_4331_43right_f32_f32(tmp125, tmp127);
    float tmp128 = 0.1599999964237213134765625;
    float tmp129 = left1_4321_43right_f32_f32(sun_halo, tmp128);
    float tmp130 = left1_4331_43right_f32_f32(green, tmp129);
    float tmp131 = 0.920000016689300537109375;
    float tmp132 = left1_4321_43right_f32_f32(sun_core, tmp131);
    green = left1_4331_43right_f32_f32(tmp130, tmp132);
    float tmp133 = 0.07999999821186065673828125;
    float tmp134 = left1_4321_43right_f32_f32(sun_halo, tmp133);
    float tmp135 = left1_4331_43right_f32_f32(blue, tmp134);
    float tmp136 = 0.519999980926513671875;
    float tmp137 = left1_4321_43right_f32_f32(sun_core, tmp136);
    blue = left1_4331_43right_f32_f32(tmp135, tmp137);
    uint tmp138 = 0u;
    bool terrain_hit = value_as_destinationtype_i32_type_ct_destinationtype_type(tmp138);
    float travel = 0.119999997317790985107421875;
    float previous_travel = travel;
    float hit_distance = travel;
    float sample_x = camera_x;
    float sample_y = camera_y;
    float sample_z = camera_z;
    float surface_height = terrain_height_at_x_z_f32_f32(sample_x, sample_z);
    uint march_step = 0u;
    uint tmp153 = 0u;
    bool tmp142 = false;
    bool tmp141 = false;
    uint tmp140 = 0u;
    for (;;)
    {
        tmp140 = 96u;
        tmp141 = left_0_right_i32_i32(march_step, tmp140);
        tmp142 = terrain_hit != true;
        if (_boolean8left5_and_3boolean8right5_bool_bool(tmp141, tmp142))
        {
            float tmp143 = left1_4321_43right_f32_f32(direction_x, travel);
            sample_x = left1_4331_43right_f32_f32(camera_x, tmp143);
            float tmp144 = left1_4321_43right_f32_f32(direction_y, travel);
            sample_y = left1_4331_43right_f32_f32(camera_y, tmp144);
            float tmp145 = left1_4321_43right_f32_f32(direction_z, travel);
            sample_z = left1_4331_43right_f32_f32(camera_z, tmp145);
            surface_height = terrain_height_at_x_z_f32_f32(sample_x, sample_z);
            if (left_0_right_f32_f32(sample_y, surface_height))
            {
                uint tmp146 = 1u;
                terrain_hit = value_as_destinationtype_i32_type_ct_destinationtype_type(tmp146);
                hit_distance = travel;
            }
            else
            {
                previous_travel = travel;
                float tmp149 = 0.4600000083446502685546875;
                travel = left1_4331_43right_f32_f32(travel, tmp149);
                float tmp152 = 44.0;
                if (left_2_right_f32_f32(travel, tmp152))
                {
                    march_step = 96u;
                }
            }
            tmp153 = 1u;
            march_step = left1_4331_43right_i32_i32(march_step, tmp153);
            continue;
        }
        else
        {
            break;
        }
    }
    uint refinement = 0u;
    uint tmp168 = 0u;
    bool tmp158 = false;
    uint tmp157 = 0u;
    for (;;)
    {
        tmp157 = 6u;
        tmp158 = left_0_right_i32_i32(refinement, tmp157);
        if (_boolean8left5_and_3boolean8right5_bool_bool(tmp158, terrain_hit))
        {
            float tmp159 = left1_4331_43right_f32_f32(previous_travel, hit_distance);
            float tmp160 = 0.5;
            float middle_travel = left1_4321_43right_f32_f32(tmp159, tmp160);
            float tmp161 = left1_4321_43right_f32_f32(direction_x, middle_travel);
            float middle_x = left1_4331_43right_f32_f32(camera_x, tmp161);
            float tmp162 = left1_4321_43right_f32_f32(direction_y, middle_travel);
            float middle_y = left1_4331_43right_f32_f32(camera_y, tmp162);
            float tmp163 = left1_4321_43right_f32_f32(direction_z, middle_travel);
            float middle_z = left1_4331_43right_f32_f32(camera_z, tmp163);
            float middle_height = terrain_height_at_x_z_f32_f32(middle_x, middle_z);
            if (left_0_right_f32_f32(middle_y, middle_height))
            {
                hit_distance = middle_travel;
            }
            else
            {
                previous_travel = middle_travel;
            }
            tmp168 = 1u;
            refinement = left1_4331_43right_i32_i32(refinement, tmp168);
            continue;
        }
        else
        {
            break;
        }
    }
    if (terrain_hit)
    {
        float tmp172 = left1_4321_43right_f32_f32(direction_x, hit_distance);
        float world_x = left1_4331_43right_f32_f32(camera_x, tmp172);
        float tmp173 = left1_4321_43right_f32_f32(direction_y, hit_distance);
        float tmp174 = left1_4321_43right_f32_f32(direction_z, hit_distance);
        float world_z = left1_4331_43right_f32_f32(camera_z, tmp174);
        surface_height = terrain_height_at_x_z_f32_f32(world_x, world_z);
        float tmp175 = 0.054999999701976776123046875;
        float tmp176 = 0.00179999996908009052276611328125;
        float tmp177 = left1_4321_43right_f32_f32(hit_distance, tmp176);
        float normal_step = left1_4331_43right_f32_f32(tmp175, tmp177);
        float tmp178 = left1_4351_43right_f32_f32(world_x, normal_step);
        float height_left = terrain_height_at_x_z_f32_f32(tmp178, world_z);
        float tmp179 = left1_4331_43right_f32_f32(world_x, normal_step);
        float height_right = terrain_height_at_x_z_f32_f32(tmp179, world_z);
        float tmp180 = left1_4351_43right_f32_f32(world_z, normal_step);
        float height_back = terrain_height_at_x_z_f32_f32(world_x, tmp180);
        float tmp181 = left1_4331_43right_f32_f32(world_z, normal_step);
        float height_front = terrain_height_at_x_z_f32_f32(world_x, tmp181);
        float normal_x = left1_4351_43right_f32_f32(height_left, height_right);
        float tmp182 = 2.0;
        float normal_y = left1_4321_43right_f32_f32(normal_step, tmp182);
        float normal_z = left1_4351_43right_f32_f32(height_back, height_front);
        float tmp183 = left1_4321_43right_f32_f32(normal_x, normal_x);
        float tmp184 = left1_4321_43right_f32_f32(normal_y, normal_y);
        float tmp185 = left1_4331_43right_f32_f32(tmp183, tmp184);
        float tmp186 = left1_4321_43right_f32_f32(normal_z, normal_z);
        float tmp187 = left1_4331_43right_f32_f32(tmp185, tmp186);
        float normal_length = _the43_square_root_of_value_f32(tmp187);
        normal_x = left1_4371_43right_f32_f32(normal_x, normal_length);
        normal_y = left1_4371_43right_f32_f32(normal_y, normal_length);
        normal_z = left1_4371_43right_f32_f32(normal_z, normal_length);
        float tmp188 = 0.4799999892711639404296875;
        float tmp189 = left1_4321_43right_f32_f32(normal_x, tmp188);
        float tmp190 = 0.7599999904632568359375;
        float tmp191 = left1_4321_43right_f32_f32(normal_y, tmp190);
        float tmp192 = left1_4331_43right_f32_f32(tmp189, tmp191);
        float tmp193 = 0.439999997615814208984375;
        float tmp194 = left1_4321_43right_f32_f32(normal_z, tmp193);
        float tmp195 = left1_4351_43right_f32_f32(tmp192, tmp194);
        float sunlight = saturate_number_f32(tmp195);
        float tmp196 = 0.5;
        float tmp197 = left1_4321_43right_f32_f32(normal_y, tmp196);
        float tmp198 = 0.5;
        float tmp199 = left1_4331_43right_f32_f32(tmp197, tmp198);
        float sky_fill = saturate_number_f32(tmp199);
        float tmp200 = 0.7200000286102294921875;
        float tmp201 = left1_4321_43right_f32_f32(world_x, tmp200);
        float tmp202 = 0.7200000286102294921875;
        float tmp203 = left1_4321_43right_f32_f32(world_z, tmp202);
        float tmp204 = 9.3999996185302734375;
        float material = flowing_field_at_x_y_phase_f32_f32_f32(tmp201, tmp203, tmp204);
        float tmp205 = 1.90999996662139892578125;
        float tmp206 = left1_4321_43right_f32_f32(world_x, tmp205);
        float tmp207 = 4.0;
        float tmp208 = left1_4331_43right_f32_f32(tmp206, tmp207);
        float tmp209 = 1.90999996662139892578125;
        float tmp210 = left1_4321_43right_f32_f32(world_z, tmp209);
        float tmp211 = 7.0;
        float tmp212 = left1_4351_43right_f32_f32(tmp210, tmp211);
        float tmp213 = 3.7999999523162841796875;
        float ground_detail = ridged_field_at_x_y_phase_f32_f32_f32(tmp208, tmp212, tmp213);
        float tmp214 = 3.400000095367431640625;
        float tmp215 = left1_4321_43right_f32_f32(world_x, tmp214);
        float tmp216 = 9.0;
        float tmp217 = left1_4351_43right_f32_f32(tmp215, tmp216);
        float tmp218 = 3.400000095367431640625;
        float tmp219 = left1_4321_43right_f32_f32(world_z, tmp218);
        float tmp220 = 2.0;
        float tmp221 = left1_4331_43right_f32_f32(tmp219, tmp220);
        float tmp222 = 6.099999904632568359375;
        float mineral_detail = flowing_field_at_x_y_phase_f32_f32_f32(tmp217, tmp221, tmp222);
        float tmp223 = 0.1599999964237213134765625;
        float tmp224 = 0.61000001430511474609375;
        float tmp225 = 1.0;
        float tmp226 = left1_4351_43right_f32_f32(tmp225, normal_y);
        float exposed_rock = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp223, tmp224, tmp226);
        float tmp227 = 1.480000019073486328125;
        float tmp228 = 2.2799999713897705078125;
        float snow = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp227, tmp228, surface_height);
        float tmp229 = 0.017999999225139617919921875;
        float tmp230 = 0.046999998390674591064453125;
        float tmp231 = left1_4321_43right_f32_f32(material, tmp230);
        float tmp232 = left1_4331_43right_f32_f32(tmp229, tmp231);
        float tmp233 = 0.02099999971687793731689453125;
        float tmp234 = left1_4321_43right_f32_f32(ground_detail, tmp233);
        float ground_red = left1_4331_43right_f32_f32(tmp232, tmp234);
        float tmp235 = 0.07500000298023223876953125;
        float tmp236 = 0.1500000059604644775390625;
        float tmp237 = left1_4321_43right_f32_f32(material, tmp236);
        float tmp238 = left1_4331_43right_f32_f32(tmp235, tmp237);
        float tmp239 = 0.05200000107288360595703125;
        float tmp240 = left1_4321_43right_f32_f32(ground_detail, tmp239);
        float ground_green = left1_4331_43right_f32_f32(tmp238, tmp240);
        float tmp241 = 0.05200000107288360595703125;
        float tmp242 = 0.082999996840953826904296875;
        float tmp243 = left1_4321_43right_f32_f32(material, tmp242);
        float tmp244 = left1_4331_43right_f32_f32(tmp241, tmp243);
        float tmp245 = 0.0390000008046627044677734375;
        float tmp246 = left1_4321_43right_f32_f32(ground_detail, tmp245);
        float ground_blue = left1_4331_43right_f32_f32(tmp244, tmp246);
        float tmp247 = 0.115000002086162567138671875;
        float tmp248 = 0.10999999940395355224609375;
        float tmp249 = left1_4321_43right_f32_f32(material, tmp248);
        float tmp250 = left1_4331_43right_f32_f32(tmp247, tmp249);
        float tmp251 = 0.08699999749660491943359375;
        float tmp252 = left1_4321_43right_f32_f32(mineral_detail, tmp251);
        float rock_red = left1_4331_43right_f32_f32(tmp250, tmp252);
        float tmp253 = 0.097999997437000274658203125;
        float tmp254 = 0.07500000298023223876953125;
        float tmp255 = left1_4321_43right_f32_f32(material, tmp254);
        float tmp256 = left1_4331_43right_f32_f32(tmp253, tmp255);
        float tmp257 = 0.0610000006854534149169921875;
        float tmp258 = left1_4321_43right_f32_f32(mineral_detail, tmp257);
        float rock_green = left1_4331_43right_f32_f32(tmp256, tmp258);
        float tmp259 = 0.104999996721744537353515625;
        float tmp260 = 0.0949999988079071044921875;
        float tmp261 = left1_4321_43right_f32_f32(material, tmp260);
        float tmp262 = left1_4331_43right_f32_f32(tmp259, tmp261);
        float tmp263 = 0.07299999892711639404296875;
        float tmp264 = left1_4321_43right_f32_f32(mineral_detail, tmp263);
        float rock_blue = left1_4331_43right_f32_f32(tmp262, tmp264);
        float tmp265 = 1.0;
        float tmp266 = left1_4351_43right_f32_f32(tmp265, exposed_rock);
        float tmp267 = left1_4321_43right_f32_f32(ground_red, tmp266);
        float tmp268 = left1_4321_43right_f32_f32(rock_red, exposed_rock);
        ground_red = left1_4331_43right_f32_f32(tmp267, tmp268);
        float tmp269 = 1.0;
        float tmp270 = left1_4351_43right_f32_f32(tmp269, exposed_rock);
        float tmp271 = left1_4321_43right_f32_f32(ground_green, tmp270);
        float tmp272 = left1_4321_43right_f32_f32(rock_green, exposed_rock);
        ground_green = left1_4331_43right_f32_f32(tmp271, tmp272);
        float tmp273 = 1.0;
        float tmp274 = left1_4351_43right_f32_f32(tmp273, exposed_rock);
        float tmp275 = left1_4321_43right_f32_f32(ground_blue, tmp274);
        float tmp276 = left1_4321_43right_f32_f32(rock_blue, exposed_rock);
        ground_blue = left1_4331_43right_f32_f32(tmp275, tmp276);
        float tmp277 = 1.0;
        float tmp278 = left1_4351_43right_f32_f32(tmp277, snow);
        float tmp279 = left1_4321_43right_f32_f32(ground_red, tmp278);
        float tmp280 = 0.7799999713897705078125;
        float tmp281 = left1_4321_43right_f32_f32(tmp280, snow);
        ground_red = left1_4331_43right_f32_f32(tmp279, tmp281);
        float tmp282 = 1.0;
        float tmp283 = left1_4351_43right_f32_f32(tmp282, snow);
        float tmp284 = left1_4321_43right_f32_f32(ground_green, tmp283);
        float tmp285 = 0.839999973773956298828125;
        float tmp286 = left1_4321_43right_f32_f32(tmp285, snow);
        ground_green = left1_4331_43right_f32_f32(tmp284, tmp286);
        float tmp287 = 1.0;
        float tmp288 = left1_4351_43right_f32_f32(tmp287, snow);
        float tmp289 = left1_4321_43right_f32_f32(ground_blue, tmp288);
        float tmp290 = 0.88999998569488525390625;
        float tmp291 = left1_4321_43right_f32_f32(tmp290, snow);
        ground_blue = left1_4331_43right_f32_f32(tmp289, tmp291);
        float tmp292 = 0.10999999940395355224609375;
        float tmp293 = 1.12999999523162841796875;
        float tmp294 = left1_4321_43right_f32_f32(sunlight, tmp293);
        float tmp295 = left1_4331_43right_f32_f32(tmp292, tmp294);
        float tmp296 = 0.1599999964237213134765625;
        float tmp297 = left1_4321_43right_f32_f32(sky_fill, tmp296);
        float lighting = left1_4331_43right_f32_f32(tmp295, tmp297);
        float tmp298 = left1_4321_43right_f32_f32(ground_red, lighting);
        float tmp299 = 0.23999999463558197021484375;
        float tmp300 = left1_4321_43right_f32_f32(sunlight, tmp299);
        ground_red = left1_4331_43right_f32_f32(tmp298, tmp300);
        float tmp301 = left1_4321_43right_f32_f32(ground_green, lighting);
        float tmp302 = 0.17000000178813934326171875;
        float tmp303 = left1_4321_43right_f32_f32(sunlight, tmp302);
        ground_green = left1_4331_43right_f32_f32(tmp301, tmp303);
        float tmp304 = left1_4321_43right_f32_f32(ground_blue, lighting);
        float tmp305 = 0.0900000035762786865234375;
        float tmp306 = left1_4321_43right_f32_f32(sunlight, tmp305);
        ground_blue = left1_4331_43right_f32_f32(tmp304, tmp306);
        float tmp307 = 11.0;
        float tmp308 = 41.0;
        float fog = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp307, tmp308, hit_distance);
        float tmp309 = 0.189999997615814208984375;
        float tmp310 = 0.1599999964237213134765625;
        float tmp311 = left1_4321_43right_f32_f32(sun_halo, tmp310);
        float fog_red = left1_4331_43right_f32_f32(tmp309, tmp311);
        float tmp312 = 0.25;
        float tmp313 = 0.100000001490116119384765625;
        float tmp314 = left1_4321_43right_f32_f32(sun_halo, tmp313);
        float fog_green = left1_4331_43right_f32_f32(tmp312, tmp314);
        float tmp315 = 0.310000002384185791015625;
        float tmp316 = 0.0500000007450580596923828125;
        float tmp317 = left1_4321_43right_f32_f32(sun_halo, tmp316);
        float fog_blue = left1_4331_43right_f32_f32(tmp315, tmp317);
        float tmp318 = 1.0;
        float tmp319 = left1_4351_43right_f32_f32(tmp318, fog);
        float tmp320 = left1_4321_43right_f32_f32(ground_red, tmp319);
        float tmp321 = left1_4321_43right_f32_f32(fog_red, fog);
        red = left1_4331_43right_f32_f32(tmp320, tmp321);
        float tmp322 = 1.0;
        float tmp323 = left1_4351_43right_f32_f32(tmp322, fog);
        float tmp324 = left1_4321_43right_f32_f32(ground_green, tmp323);
        float tmp325 = left1_4321_43right_f32_f32(fog_green, fog);
        green = left1_4331_43right_f32_f32(tmp324, tmp325);
        float tmp326 = 1.0;
        float tmp327 = left1_4351_43right_f32_f32(tmp326, fog);
        float tmp328 = left1_4321_43right_f32_f32(ground_blue, tmp327);
        float tmp329 = left1_4321_43right_f32_f32(fog_blue, fog);
        blue = left1_4331_43right_f32_f32(tmp328, tmp329);
    }
    float tmp330 = left1_4371_43right_f32_f32(screen_x, aspect);
    float tmp331 = left1_4371_43right_f32_f32(screen_x, aspect);
    float tmp332 = left1_4321_43right_f32_f32(tmp330, tmp331);
    float tmp333 = left1_4321_43right_f32_f32(screen_y, screen_y);
    float tmp334 = left1_4331_43right_f32_f32(tmp332, tmp333);
    float vignette_radius = _the43_square_root_of_value_f32(tmp334);
    float tmp335 = 0.3400000035762786865234375;
    float tmp336 = 1.0;
    float tmp337 = 0.569999992847442626953125;
    float tmp338 = 1.33000004291534423828125;
    float tmp339 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp337, tmp338, vignette_radius);
    float tmp340 = left1_4351_43right_f32_f32(tmp336, tmp339);
    float tmp341 = 0.660000026226043701171875;
    float tmp342 = left1_4321_43right_f32_f32(tmp340, tmp341);
    float vignette = left1_4331_43right_f32_f32(tmp335, tmp342);
    float tmp343 = left1_4321_43right_f32_f32(red, vignette);
    float tmp344 = 1.0;
    float tmp345 = 0.4199999868869781494140625;
    float tmp346 = left1_4321_43right_f32_f32(red, tmp345);
    float tmp347 = left1_4331_43right_f32_f32(tmp344, tmp346);
    float tmp348 = left1_4371_43right_f32_f32(tmp343, tmp347);
    red = _the43_square_root_of_value_f32(tmp348);
    float tmp349 = left1_4321_43right_f32_f32(green, vignette);
    float tmp350 = 1.0;
    float tmp351 = 0.4199999868869781494140625;
    float tmp352 = left1_4321_43right_f32_f32(green, tmp351);
    float tmp353 = left1_4331_43right_f32_f32(tmp350, tmp352);
    float tmp354 = left1_4371_43right_f32_f32(tmp349, tmp353);
    green = _the43_square_root_of_value_f32(tmp354);
    float tmp355 = left1_4321_43right_f32_f32(blue, vignette);
    float tmp356 = 1.0;
    float tmp357 = 0.4199999868869781494140625;
    float tmp358 = left1_4321_43right_f32_f32(blue, tmp357);
    float tmp359 = left1_4331_43right_f32_f32(tmp356, tmp358);
    float tmp360 = left1_4371_43right_f32_f32(tmp355, tmp359);
    blue = _the43_square_root_of_value_f32(tmp360);
    vec4 _933 = vec4(0.0, 0.0, 0.0, 1.0);
    _933.z = blue;
    _933.y = green;
    _933.x = red;
    dynlexColor = _933;
}
