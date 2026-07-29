#version 300 es
precision highp float;
precision highp int;

struct _class
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

struct class_2
{
    bool _m0;
    bool _m1;
    float _m2;
    float _m3;
    float _m4;
    uint _m5;
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

float the_sine_of_value_f32(float value)
{
    return sin(value);
}

float the_cosine_of_value_f32(float value)
{
    return cos(value);
}

float left1_4331_43right_f32_f32(float left, float right)
{
    return left + right;
}

float the_square_root_of_value_f32(float value)
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

float _the_negative_of_4the_opposite_of_453value_f32(float value)
{
    return -value;
}

float the_signed_flow_at_3shader_point8point5_during_3any8phase5_shader_point_f32(_class point, float phase)
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

float the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(_class point, float phase)
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

float the_ridged_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(_class point, float phase)
{
    float tmp = the_signed_flow_at_3shader_point8point5_during_3any8phase5_shader_point_f32(point, phase);
    float wave = the_absolute_value_of_magnitude_f32(tmp);
    float tmp1 = 1.0;
    float tmp2 = 1.0;
    float tmp3 = the_minimum_of_left_and_right_f32_f32(wave, tmp2);
    float ridge = left1_4351_43right_f32_f32(tmp1, tmp3);
    return left1_4321_43right_f32_f32(ridge, ridge);
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

float the_glow_from_inner_to_outer_at_sample_f32_f32_f32(float inner, float outer, float _sample)
{
    float tmp = 1.0;
    float tmp1 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(inner, outer, _sample);
    return left1_4351_43right_f32_f32(tmp, tmp1);
}

bool value_as_3type8destination5_i32_type_ct_destination_type(uint value)
{
    return value != 0u;
}

float the_terrain_height_at_3terrain_coordinate8position5_terrain_coordinate(_class position)
{
    float tmp = 0.082999996840953826904296875;
    float tmp1 = left1_4321_43right_f32_f32(position._m0, tmp);
    float tmp3 = 0.046999998390674591064453125;
    float tmp4 = left1_4321_43right_f32_f32(position._m1, tmp3);
    float tmp5 = left1_4331_43right_f32_f32(tmp1, tmp4);
    float tmp6 = the_sine_of_value_f32(tmp5);
    float tmp8 = 0.0309999994933605194091796875;
    float tmp9 = _the_negative_of_4the_opposite_of_453value_f32(tmp8);
    float tmp10 = left1_4321_43right_f32_f32(position._m0, tmp9);
    float tmp12 = 0.071000002324581146240234375;
    float tmp13 = left1_4321_43right_f32_f32(position._m1, tmp12);
    float tmp14 = left1_4331_43right_f32_f32(tmp10, tmp13);
    float tmp15 = the_cosine_of_value_f32(tmp14);
    float tmp16 = 0.5299999713897705078125;
    float tmp17 = left1_4321_43right_f32_f32(tmp15, tmp16);
    _class class_tmp = _class(0.0, 0.0);
    class_tmp._m0 = left1_4331_43right_f32_f32(tmp6, tmp17);
    float tmp20 = 0.0570000000298023223876953125;
    float tmp21 = _the_negative_of_4the_opposite_of_453value_f32(tmp20);
    float tmp22 = left1_4321_43right_f32_f32(position._m0, tmp21);
    float tmp24 = 0.0610000006854534149169921875;
    float tmp25 = left1_4321_43right_f32_f32(position._m1, tmp24);
    float tmp26 = left1_4331_43right_f32_f32(tmp22, tmp25);
    float tmp27 = the_cosine_of_value_f32(tmp26);
    float tmp29 = 0.068999998271465301513671875;
    float tmp30 = left1_4321_43right_f32_f32(position._m0, tmp29);
    float tmp32 = 0.02899999916553497314453125;
    float tmp33 = left1_4321_43right_f32_f32(position._m1, tmp32);
    float tmp34 = left1_4331_43right_f32_f32(tmp30, tmp33);
    float tmp35 = the_sine_of_value_f32(tmp34);
    float tmp36 = 0.4699999988079071044921875;
    float tmp37 = left1_4321_43right_f32_f32(tmp35, tmp36);
    class_tmp._m1 = left1_4331_43right_f32_f32(tmp27, tmp37);
    _class warp = class_tmp;
    float tmp42 = 3.400000095367431640625;
    float tmp43 = left1_4321_43right_f32_f32(warp._m0, tmp42);
    _class class_tmp39 = _class(0.0, 0.0);
    class_tmp39._m0 = left1_4331_43right_f32_f32(position._m0, tmp43);
    float tmp47 = 3.400000095367431640625;
    float tmp48 = left1_4321_43right_f32_f32(warp._m1, tmp47);
    class_tmp39._m1 = left1_4331_43right_f32_f32(position._m1, tmp48);
    _class bent = class_tmp39;
    float tmp52 = 0.07299999892711639404296875;
    float tmp53 = left1_4321_43right_f32_f32(bent._m0, tmp52);
    float tmp55 = 0.05099999904632568359375;
    float tmp56 = left1_4321_43right_f32_f32(bent._m1, tmp55);
    float tmp57 = left1_4331_43right_f32_f32(tmp53, tmp56);
    float tmp58 = the_sine_of_value_f32(tmp57);
    float tmp59 = 0.62000000476837158203125;
    float continental = left1_4321_43right_f32_f32(tmp58, tmp59);
    float tmp61 = 0.0489999987185001373291015625;
    float tmp62 = _the_negative_of_4the_opposite_of_453value_f32(tmp61);
    float tmp63 = left1_4321_43right_f32_f32(bent._m0, tmp62);
    float tmp65 = 0.090999998152256011962890625;
    float tmp66 = left1_4321_43right_f32_f32(bent._m1, tmp65);
    float tmp67 = left1_4331_43right_f32_f32(tmp63, tmp66);
    float tmp68 = the_cosine_of_value_f32(tmp67);
    float tmp69 = 0.3400000035762786865234375;
    float crossing = left1_4321_43right_f32_f32(tmp68, tmp69);
    float tmp71 = 0.17000000178813934326171875;
    float tmp72 = left1_4321_43right_f32_f32(bent._m0, tmp71);
    float tmp74 = 0.10999999940395355224609375;
    float tmp75 = left1_4321_43right_f32_f32(bent._m1, tmp74);
    float tmp76 = left1_4351_43right_f32_f32(tmp72, tmp75);
    float tmp78 = 0.310000002384185791015625;
    float tmp79 = left1_4321_43right_f32_f32(warp._m1, tmp78);
    float tmp80 = left1_4331_43right_f32_f32(tmp76, tmp79);
    float tmp81 = the_cosine_of_value_f32(tmp80);
    float tmp82 = 0.5;
    float tmp83 = left1_4321_43right_f32_f32(tmp81, tmp82);
    float tmp84 = 0.5;
    float ridge = left1_4331_43right_f32_f32(tmp83, tmp84);
    float tmp86 = 0.12999999523162841796875;
    float tmp87 = _the_negative_of_4the_opposite_of_453value_f32(tmp86);
    float tmp88 = left1_4321_43right_f32_f32(bent._m0, tmp87);
    float tmp90 = 0.23000000417232513427734375;
    float tmp91 = left1_4321_43right_f32_f32(bent._m1, tmp90);
    float tmp92 = left1_4351_43right_f32_f32(tmp88, tmp91);
    float tmp94 = 0.2700000107288360595703125;
    float tmp95 = left1_4321_43right_f32_f32(warp._m0, tmp94);
    float tmp96 = left1_4351_43right_f32_f32(tmp92, tmp95);
    float tmp97 = the_cosine_of_value_f32(tmp96);
    float tmp98 = 0.5;
    float tmp99 = left1_4321_43right_f32_f32(tmp97, tmp98);
    float tmp100 = 0.5;
    float spur = left1_4331_43right_f32_f32(tmp99, tmp100);
    float tmp102 = 0.2899999916553497314453125;
    float tmp103 = left1_4321_43right_f32_f32(bent._m0, tmp102);
    float tmp105 = 0.070000000298023223876953125;
    float tmp106 = left1_4321_43right_f32_f32(bent._m1, tmp105);
    float tmp107 = left1_4331_43right_f32_f32(tmp103, tmp106);
    float tmp109 = 0.189999997615814208984375;
    float tmp110 = left1_4321_43right_f32_f32(warp._m1, tmp109);
    float tmp111 = left1_4331_43right_f32_f32(tmp107, tmp110);
    float tmp112 = the_cosine_of_value_f32(tmp111);
    float tmp113 = 0.5;
    float tmp114 = left1_4321_43right_f32_f32(tmp112, tmp113);
    float tmp115 = 0.5;
    float crest = left1_4331_43right_f32_f32(tmp114, tmp115);
    float tmp116 = left1_4321_43right_f32_f32(ridge, ridge);
    float tmp117 = left1_4321_43right_f32_f32(ridge, ridge);
    ridge = left1_4321_43right_f32_f32(tmp116, tmp117);
    float tmp118 = left1_4321_43right_f32_f32(spur, spur);
    float tmp119 = left1_4321_43right_f32_f32(spur, spur);
    spur = left1_4321_43right_f32_f32(tmp118, tmp119);
    float tmp120 = left1_4321_43right_f32_f32(crest, crest);
    float tmp121 = left1_4321_43right_f32_f32(crest, crest);
    crest = left1_4321_43right_f32_f32(tmp120, tmp121);
    float tmp122 = 2.0799999237060546875;
    float tmp123 = left1_4321_43right_f32_f32(ridge, tmp122);
    float tmp124 = 1.46000003814697265625;
    float tmp125 = left1_4321_43right_f32_f32(spur, tmp124);
    float tmp126 = left1_4331_43right_f32_f32(tmp123, tmp125);
    float tmp127 = 0.920000016689300537109375;
    float tmp128 = left1_4321_43right_f32_f32(crest, tmp127);
    float peaks = left1_4331_43right_f32_f32(tmp126, tmp128);
    float tmp129 = 0.4600000083446502685546875;
    float tmp130 = _the_negative_of_4the_opposite_of_453value_f32(tmp129);
    float tmp131 = 0.540000021457672119140625;
    float tmp132 = left1_4331_43right_f32_f32(continental, crossing);
    float mask = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp130, tmp131, tmp132);
    float tmp133 = 0.310000002384185791015625;
    float tmp134 = 0.7799999713897705078125;
    float tmp135 = left1_4321_43right_f32_f32(mask, tmp134);
    float tmp136 = left1_4331_43right_f32_f32(tmp133, tmp135);
    float elevation = left1_4321_43right_f32_f32(peaks, tmp136);
    float tmp138 = 0.5099999904632568359375;
    float tmp139 = left1_4321_43right_f32_f32(bent._m0, tmp138);
    float tmp141 = 0.37000000476837158203125;
    float tmp142 = left1_4321_43right_f32_f32(bent._m1, tmp141);
    float tmp143 = left1_4331_43right_f32_f32(tmp139, tmp142);
    float tmp144 = the_sine_of_value_f32(tmp143);
    float tmp145 = 0.100000001490116119384765625;
    float tmp146 = left1_4321_43right_f32_f32(tmp144, tmp145);
    float tmp148 = 0.829999983310699462890625;
    float tmp149 = _the_negative_of_4the_opposite_of_453value_f32(tmp148);
    float tmp150 = left1_4321_43right_f32_f32(bent._m0, tmp149);
    float tmp152 = 0.61000001430511474609375;
    float tmp153 = left1_4321_43right_f32_f32(bent._m1, tmp152);
    float tmp154 = left1_4331_43right_f32_f32(tmp150, tmp153);
    float tmp155 = the_cosine_of_value_f32(tmp154);
    float tmp156 = 0.04500000178813934326171875;
    float tmp157 = left1_4321_43right_f32_f32(tmp155, tmp156);
    float detail = left1_4331_43right_f32_f32(tmp146, tmp157);
    float tmp158 = 0.310000002384185791015625;
    float tmp159 = left1_4331_43right_f32_f32(tmp158, continental);
    float tmp160 = _the_negative_of_4the_opposite_of_453value_f32(tmp159);
    float tmp161 = 0.540000021457672119140625;
    float tmp162 = left1_4321_43right_f32_f32(crossing, tmp161);
    float tmp163 = left1_4331_43right_f32_f32(tmp160, tmp162);
    float tmp164 = left1_4331_43right_f32_f32(tmp163, elevation);
    return left1_4331_43right_f32_f32(tmp164, detail);
}

bool left_0_right_i32_i32(uint left, uint right)
{
    return int(left) < int(right);
}

bool _terrain_traversal8traversal5_continues_searching_terrain_traversal(class_2 traversal)
{
    return traversal._m1;
}

bool _boolean8left5_and_3boolean8right5_bool_bool(bool left, bool right)
{
    return left && right;
}

bool _terrain_traversal8traversal5_found_terrain_terrain_traversal(class_2 traversal)
{
    return traversal._m0;
}

uint left1_4331_43right_i32_i32(uint left, uint right)
{
    return left + right;
}

void main()
{
    _class class_tmp = _class(0.0, 0.0);
    class_tmp._m0 = gl_FragCoord.x;
    class_tmp._m1 = gl_FragCoord.y;
    _class pixel = class_tmp;
    float time = dynlexUniform0.value;
    float tmp = dynlexUniform1.value;
    float tmp5 = 1.0;
    _class class_tmp4 = _class(0.0, 0.0);
    class_tmp4._m0 = the_maximum_of_left_and_right_f32_f32(tmp, tmp5);
    float tmp7 = dynlexUniform2.value;
    float tmp8 = 1.0;
    class_tmp4._m1 = the_maximum_of_left_and_right_f32_f32(tmp7, tmp8);
    _class frame = class_tmp4;
    float aspect = left1_4371_43right_f32_f32(frame._m0, frame._m1);
    float tmp16 = left1_4371_43right_f32_f32(pixel._m0, frame._m0);
    float tmp17 = 2.0;
    float tmp18 = left1_4321_43right_f32_f32(tmp16, tmp17);
    float tmp19 = 1.0;
    float tmp20 = left1_4351_43right_f32_f32(tmp18, tmp19);
    _class class_tmp13 = _class(0.0, 0.0);
    class_tmp13._m0 = left1_4321_43right_f32_f32(tmp20, aspect);
    float tmp24 = left1_4371_43right_f32_f32(pixel._m1, frame._m1);
    float tmp25 = 2.0;
    float tmp26 = left1_4321_43right_f32_f32(tmp24, tmp25);
    float tmp27 = 1.0;
    class_tmp13._m1 = left1_4351_43right_f32_f32(tmp26, tmp27);
    _class screen = class_tmp13;
    float tmp31 = 0.12999999523162841796875;
    float tmp32 = left1_4321_43right_f32_f32(time, tmp31);
    float tmp33 = the_sine_of_value_f32(tmp32);
    float tmp34 = 3.099999904632568359375;
    float tmp35 = left1_4321_43right_f32_f32(tmp33, tmp34);
    float tmp36 = 0.071000002324581146240234375;
    float tmp37 = left1_4321_43right_f32_f32(time, tmp36);
    float tmp38 = the_cosine_of_value_f32(tmp37);
    float tmp39 = 1.39999997615814208984375;
    float tmp40 = left1_4321_43right_f32_f32(tmp38, tmp39);
    class_0 class_tmp30 = class_0(0.0, 0.0, 0.0);
    class_tmp30._m0 = left1_4331_43right_f32_f32(tmp35, tmp40);
    float tmp42 = 3.2599999904632568359375;
    float tmp43 = 0.189999997615814208984375;
    float tmp44 = left1_4321_43right_f32_f32(time, tmp43);
    float tmp45 = the_sine_of_value_f32(tmp44);
    float tmp46 = 0.12999999523162841796875;
    float tmp47 = left1_4321_43right_f32_f32(tmp45, tmp46);
    class_tmp30._m1 = left1_4331_43right_f32_f32(tmp42, tmp47);
    float tmp49 = 76.0;
    float tmp50 = 3.0499999523162841796875;
    float tmp51 = left1_4321_43right_f32_f32(time, tmp50);
    class_tmp30._m2 = left1_4331_43right_f32_f32(tmp49, tmp51);
    class_0 camera = class_tmp30;
    float tmp54 = 0.09700000286102294921875;
    float tmp55 = left1_4321_43right_f32_f32(time, tmp54);
    float tmp56 = the_sine_of_value_f32(tmp55);
    float tmp57 = 0.1599999964237213134765625;
    float tmp58 = left1_4321_43right_f32_f32(tmp56, tmp57);
    float tmp59 = 0.0610000006854534149169921875;
    float tmp60 = left1_4321_43right_f32_f32(time, tmp59);
    float tmp61 = the_cosine_of_value_f32(tmp60);
    float tmp62 = 0.070000000298023223876953125;
    float tmp63 = left1_4321_43right_f32_f32(tmp61, tmp62);
    float yaw = left1_4331_43right_f32_f32(tmp58, tmp63);
    float sine = the_sine_of_value_f32(yaw);
    float cosine = the_cosine_of_value_f32(yaw);
    float tmp64 = 0.189999997615814208984375;
    float tmp65 = left1_4321_43right_f32_f32(time, tmp64);
    float tmp66 = the_sine_of_value_f32(tmp65);
    float tmp67 = 0.02500000037252902984619140625;
    float bank = left1_4321_43right_f32_f32(tmp66, tmp67);
    float tmp70 = the_cosine_of_value_f32(bank);
    float tmp71 = left1_4321_43right_f32_f32(screen._m0, tmp70);
    float tmp73 = the_sine_of_value_f32(bank);
    float tmp74 = left1_4321_43right_f32_f32(screen._m1, tmp73);
    _class class_tmp68 = _class(0.0, 0.0);
    class_tmp68._m0 = left1_4351_43right_f32_f32(tmp71, tmp74);
    float tmp77 = the_sine_of_value_f32(bank);
    float tmp78 = left1_4321_43right_f32_f32(screen._m0, tmp77);
    float tmp80 = the_cosine_of_value_f32(bank);
    float tmp81 = left1_4321_43right_f32_f32(screen._m1, tmp80);
    class_tmp68._m1 = left1_4331_43right_f32_f32(tmp78, tmp81);
    _class tilt = class_tmp68;
    float tmp86 = 0.680000007152557373046875;
    float tmp87 = left1_4321_43right_f32_f32(tilt._m0, tmp86);
    float tmp88 = left1_4321_43right_f32_f32(tmp87, cosine);
    float tmp89 = 1.2400000095367431640625;
    float tmp90 = left1_4321_43right_f32_f32(tmp89, sine);
    class_0 class_tmp84 = class_0(0.0, 0.0, 0.0);
    class_tmp84._m0 = left1_4331_43right_f32_f32(tmp88, tmp90);
    float tmp93 = 0.680000007152557373046875;
    float tmp94 = left1_4321_43right_f32_f32(tilt._m1, tmp93);
    float tmp95 = 0.189999997615814208984375;
    class_tmp84._m1 = left1_4351_43right_f32_f32(tmp94, tmp95);
    float tmp97 = 1.2400000095367431640625;
    float tmp98 = left1_4321_43right_f32_f32(tmp97, cosine);
    float tmp100 = 0.680000007152557373046875;
    float tmp101 = left1_4321_43right_f32_f32(tilt._m0, tmp100);
    float tmp102 = left1_4321_43right_f32_f32(tmp101, sine);
    class_tmp84._m2 = left1_4351_43right_f32_f32(tmp98, tmp102);
    class_0 direction = class_tmp84;
    float tmp107 = left1_4321_43right_f32_f32(direction._m0, direction._m0);
    float tmp110 = left1_4321_43right_f32_f32(direction._m1, direction._m1);
    float tmp111 = left1_4331_43right_f32_f32(tmp107, tmp110);
    float tmp114 = left1_4321_43right_f32_f32(direction._m2, direction._m2);
    float tmp115 = left1_4331_43right_f32_f32(tmp111, tmp114);
    float _length = the_square_root_of_value_f32(tmp115);
    direction._m0 = left1_4371_43right_f32_f32(direction._m0, _length);
    direction._m1 = left1_4371_43right_f32_f32(direction._m1, _length);
    direction._m2 = left1_4371_43right_f32_f32(direction._m2, _length);
    float tmp123 = 0.540000021457672119140625;
    float tmp124 = left1_4321_43right_f32_f32(screen._m1, tmp123);
    float tmp125 = 0.449999988079071044921875;
    float tmp126 = left1_4331_43right_f32_f32(tmp124, tmp125);
    float altitude = number_saturated_f32(tmp126);
    float tmp128 = 0.017999999225139617919921875;
    float tmp129 = 0.07500000298023223876953125;
    float tmp130 = left1_4321_43right_f32_f32(altitude, tmp129);
    class_0 class_tmp127 = class_0(0.0, 0.0, 0.0);
    class_tmp127._m0 = left1_4331_43right_f32_f32(tmp128, tmp130);
    float tmp132 = 0.05200000107288360595703125;
    float tmp133 = 0.1599999964237213134765625;
    float tmp134 = left1_4321_43right_f32_f32(altitude, tmp133);
    class_tmp127._m1 = left1_4331_43right_f32_f32(tmp132, tmp134);
    float tmp136 = 0.119999997317790985107421875;
    float tmp137 = 0.3400000035762786865234375;
    float tmp138 = left1_4321_43right_f32_f32(altitude, tmp137);
    class_tmp127._m2 = left1_4331_43right_f32_f32(tmp136, tmp138);
    class_0 color = class_tmp127;
    float tmp143 = 1.25;
    float tmp144 = left1_4321_43right_f32_f32(screen._m0, tmp143);
    float tmp145 = 0.017999999225139617919921875;
    float tmp146 = left1_4321_43right_f32_f32(time, tmp145);
    _class class_tmp141 = _class(0.0, 0.0);
    class_tmp141._m0 = left1_4331_43right_f32_f32(tmp144, tmp146);
    float tmp149 = 2.400000095367431640625;
    float tmp150 = left1_4321_43right_f32_f32(screen._m1, tmp149);
    float tmp151 = 3.0;
    class_tmp141._m1 = left1_4331_43right_f32_f32(tmp150, tmp151);
    _class tmp154 = class_tmp141;
    float tmp155 = 2.099999904632568359375;
    float sweep = the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp154, tmp155);
    float tmp158 = 2.099999904632568359375;
    float tmp159 = left1_4321_43right_f32_f32(screen._m0, tmp158);
    float tmp160 = 8.0;
    _class class_tmp156 = _class(0.0, 0.0);
    class_tmp156._m0 = left1_4351_43right_f32_f32(tmp159, tmp160);
    float tmp163 = 3.7000000476837158203125;
    float tmp164 = left1_4321_43right_f32_f32(screen._m1, tmp163);
    float tmp165 = 0.01200000010430812835693359375;
    float tmp166 = left1_4321_43right_f32_f32(time, tmp165);
    class_tmp156._m1 = left1_4331_43right_f32_f32(tmp164, tmp166);
    _class tmp169 = class_tmp156;
    float tmp170 = 5.69999980926513671875;
    float ridges = the_ridged_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp169, tmp170);
    float tmp171 = 0.540000021457672119140625;
    float tmp172 = 0.819999992847442626953125;
    float tmp173 = 0.7599999904632568359375;
    float tmp174 = left1_4321_43right_f32_f32(sweep, tmp173);
    float tmp175 = 0.23999999463558197021484375;
    float tmp176 = left1_4321_43right_f32_f32(ridges, tmp175);
    float tmp177 = left1_4331_43right_f32_f32(tmp174, tmp176);
    float shape = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp171, tmp172, tmp177);
    float tmp178 = 0.0500000007450580596923828125;
    float tmp179 = _the_negative_of_4the_opposite_of_453value_f32(tmp178);
    float tmp180 = 0.310000002384185791015625;
    float elevation = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp179, tmp180, screen._m1);
    float cloud = left1_4321_43right_f32_f32(shape, elevation);
    float tmp183 = 0.3400000035762786865234375;
    float tmp184 = left1_4321_43right_f32_f32(cloud, tmp183);
    color._m0 = left1_4331_43right_f32_f32(color._m0, tmp184);
    float tmp187 = 0.310000002384185791015625;
    float tmp188 = left1_4321_43right_f32_f32(cloud, tmp187);
    color._m1 = left1_4331_43right_f32_f32(color._m1, tmp188);
    float tmp191 = 0.2700000107288360595703125;
    float tmp192 = left1_4321_43right_f32_f32(cloud, tmp191);
    color._m2 = left1_4331_43right_f32_f32(color._m2, tmp192);
    float tmp196 = 0.560000002384185791015625;
    _class class_tmp194 = _class(0.0, 0.0);
    class_tmp194._m0 = left1_4351_43right_f32_f32(screen._m0, tmp196);
    float tmp199 = 0.38999998569488525390625;
    class_tmp194._m1 = left1_4351_43right_f32_f32(screen._m1, tmp199);
    _class sun = class_tmp194;
    float tmp204 = left1_4321_43right_f32_f32(sun._m0, sun._m0);
    float tmp207 = left1_4321_43right_f32_f32(sun._m1, sun._m1);
    float tmp208 = left1_4331_43right_f32_f32(tmp204, tmp207);
    float _distance = the_square_root_of_value_f32(tmp208);
    float tmp209 = 0.017999999225139617919921875;
    float tmp210 = 0.07500000298023223876953125;
    float core = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp209, tmp210, _distance);
    float tmp211 = 0.0599999986588954925537109375;
    float tmp212 = 0.4199999868869781494140625;
    float halo = the_glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp211, tmp212, _distance);
    float tmp214 = 0.2700000107288360595703125;
    float tmp215 = left1_4321_43right_f32_f32(halo, tmp214);
    float tmp216 = left1_4331_43right_f32_f32(color._m0, tmp215);
    float tmp217 = 1.2999999523162841796875;
    float tmp218 = left1_4321_43right_f32_f32(core, tmp217);
    color._m0 = left1_4331_43right_f32_f32(tmp216, tmp218);
    float tmp221 = 0.1599999964237213134765625;
    float tmp222 = left1_4321_43right_f32_f32(halo, tmp221);
    float tmp223 = left1_4331_43right_f32_f32(color._m1, tmp222);
    float tmp224 = 0.920000016689300537109375;
    float tmp225 = left1_4321_43right_f32_f32(core, tmp224);
    color._m1 = left1_4331_43right_f32_f32(tmp223, tmp225);
    float tmp228 = 0.07999999821186065673828125;
    float tmp229 = left1_4321_43right_f32_f32(halo, tmp228);
    float tmp230 = left1_4331_43right_f32_f32(color._m2, tmp229);
    float tmp231 = 0.519999980926513671875;
    float tmp232 = left1_4321_43right_f32_f32(core, tmp231);
    color._m2 = left1_4331_43right_f32_f32(tmp230, tmp232);
    uint tmp235 = 0u;
    class_2 class_tmp234 = class_2(false, false, 0.0, 0.0, 0.0, 0u);
    class_tmp234._m0 = value_as_3type8destination5_i32_type_ct_destination_type(tmp235);
    uint tmp237 = 1u;
    class_tmp234._m1 = value_as_3type8destination5_i32_type_ct_destination_type(tmp237);
    class_tmp234._m2 = 0.119999997317790985107421875;
    class_tmp234._m3 = 0.119999997317790985107421875;
    class_tmp234._m4 = 0.119999997317790985107421875;
    class_tmp234._m5 = 0u;
    class_2 traversal = class_tmp234;
    class_0 _sample = camera;
    _class class_tmp244 = _class(0.0, 0.0);
    class_tmp244._m0 = _sample._m0;
    class_tmp244._m1 = _sample._m2;
    _class tmp250 = class_tmp244;
    float height = the_terrain_height_at_3terrain_coordinate8position5_terrain_coordinate(tmp250);
    uint tmp300 = 0u;
    _class class_tmp270 = _class(0.0, 0.0);
    bool tmp254 = false;
    bool tmp253 = false;
    uint tmp252 = 0u;
    for (;;)
    {
        tmp252 = 96u;
        tmp253 = left_0_right_i32_i32(traversal._m5, tmp252);
        tmp254 = _terrain_traversal8traversal5_continues_searching_terrain_traversal(traversal);
        if (_boolean8left5_and_3boolean8right5_bool_bool(tmp253, tmp254))
        {
            float tmp258 = left1_4321_43right_f32_f32(direction._m0, traversal._m2);
            _sample._m0 = left1_4331_43right_f32_f32(camera._m0, tmp258);
            float tmp263 = left1_4321_43right_f32_f32(direction._m1, traversal._m2);
            _sample._m1 = left1_4331_43right_f32_f32(camera._m1, tmp263);
            float tmp268 = left1_4321_43right_f32_f32(direction._m2, traversal._m2);
            _sample._m2 = left1_4331_43right_f32_f32(camera._m2, tmp268);
            class_tmp270._m0 = _sample._m0;
            class_tmp270._m1 = _sample._m2;
            _class tmp278 = class_tmp270;
            height = the_terrain_height_at_3terrain_coordinate8position5_terrain_coordinate(tmp278);
            if (left_0_right_f32_f32(_sample._m1, height))
            {
                uint tmp280 = 1u;
                traversal._m0 = value_as_3type8destination5_i32_type_ct_destination_type(tmp280);
                uint tmp282 = 0u;
                traversal._m1 = value_as_3type8destination5_i32_type_ct_destination_type(tmp282);
                traversal._m4 = traversal._m2;
            }
            else
            {
                traversal._m3 = traversal._m2;
                float tmp290 = 0.4600000083446502685546875;
                traversal._m2 = left1_4331_43right_f32_f32(traversal._m2, tmp290);
                float tmp295 = 44.0;
                if (left_2_right_f32_f32(traversal._m2, tmp295))
                {
                    traversal._m5 = 96u;
                    uint tmp297 = 0u;
                    traversal._m1 = value_as_3type8destination5_i32_type_ct_destination_type(tmp297);
                }
            }
            tmp300 = 1u;
            traversal._m5 = left1_4331_43right_i32_i32(traversal._m5, tmp300);
            continue;
        }
        else
        {
            break;
        }
    }
    uint refinement = 0u;
    uint tmp342 = 0u;
    _class class_tmp326 = _class(0.0, 0.0);
    class_0 class_tmp312 = class_0(0.0, 0.0, 0.0);
    bool tmp307 = false;
    bool tmp306 = false;
    uint tmp305 = 0u;
    for (;;)
    {
        tmp305 = 6u;
        tmp306 = left_0_right_i32_i32(refinement, tmp305);
        tmp307 = _terrain_traversal8traversal5_found_terrain_terrain_traversal(traversal);
        if (_boolean8left5_and_3boolean8right5_bool_bool(tmp306, tmp307))
        {
            float tmp310 = left1_4331_43right_f32_f32(traversal._m3, traversal._m4);
            float tmp311 = 0.5;
            float middle = left1_4321_43right_f32_f32(tmp310, tmp311);
            float tmp315 = left1_4321_43right_f32_f32(direction._m0, middle);
            class_tmp312._m0 = left1_4331_43right_f32_f32(camera._m0, tmp315);
            float tmp319 = left1_4321_43right_f32_f32(direction._m1, middle);
            class_tmp312._m1 = left1_4331_43right_f32_f32(camera._m1, tmp319);
            float tmp323 = left1_4321_43right_f32_f32(direction._m2, middle);
            class_tmp312._m2 = left1_4331_43right_f32_f32(camera._m2, tmp323);
            _sample = class_tmp312;
            class_tmp326._m0 = _sample._m0;
            class_tmp326._m1 = _sample._m2;
            _class tmp334 = class_tmp326;
            height = the_terrain_height_at_3terrain_coordinate8position5_terrain_coordinate(tmp334);
            if (left_0_right_f32_f32(_sample._m1, height))
            {
                traversal._m4 = middle;
            }
            else
            {
                traversal._m3 = middle;
            }
            tmp342 = 1u;
            refinement = left1_4331_43right_i32_i32(refinement, tmp342);
            continue;
        }
        else
        {
            break;
        }
    }
    if (_terrain_traversal8traversal5_found_terrain_terrain_traversal(traversal))
    {
        float tmp349 = left1_4321_43right_f32_f32(direction._m0, traversal._m4);
        class_0 class_tmp345 = class_0(0.0, 0.0, 0.0);
        class_tmp345._m0 = left1_4331_43right_f32_f32(camera._m0, tmp349);
        float tmp354 = left1_4321_43right_f32_f32(direction._m1, traversal._m4);
        class_tmp345._m1 = left1_4331_43right_f32_f32(camera._m1, tmp354);
        float tmp359 = left1_4321_43right_f32_f32(direction._m2, traversal._m4);
        class_tmp345._m2 = left1_4331_43right_f32_f32(camera._m2, tmp359);
        class_0 world = class_tmp345;
        _class class_tmp362 = _class(0.0, 0.0);
        class_tmp362._m0 = world._m0;
        class_tmp362._m1 = world._m2;
        _class tmp370 = class_tmp362;
        height = the_terrain_height_at_3terrain_coordinate8position5_terrain_coordinate(tmp370);
        float tmp371 = 0.054999999701976776123046875;
        float tmp373 = 0.00179999996908009052276611328125;
        float tmp374 = left1_4321_43right_f32_f32(traversal._m4, tmp373);
        float stride = left1_4331_43right_f32_f32(tmp371, tmp374);
        _class class_tmp375 = _class(0.0, 0.0);
        class_tmp375._m0 = left1_4351_43right_f32_f32(world._m0, stride);
        class_tmp375._m1 = world._m2;
        _class tmp382 = class_tmp375;
        float west = the_terrain_height_at_3terrain_coordinate8position5_terrain_coordinate(tmp382);
        _class class_tmp383 = _class(0.0, 0.0);
        class_tmp383._m0 = left1_4331_43right_f32_f32(world._m0, stride);
        class_tmp383._m1 = world._m2;
        _class tmp390 = class_tmp383;
        float east = the_terrain_height_at_3terrain_coordinate8position5_terrain_coordinate(tmp390);
        _class class_tmp391 = _class(0.0, 0.0);
        class_tmp391._m0 = world._m0;
        class_tmp391._m1 = left1_4351_43right_f32_f32(world._m2, stride);
        _class tmp398 = class_tmp391;
        float south = the_terrain_height_at_3terrain_coordinate8position5_terrain_coordinate(tmp398);
        _class class_tmp399 = _class(0.0, 0.0);
        class_tmp399._m0 = world._m0;
        class_tmp399._m1 = left1_4331_43right_f32_f32(world._m2, stride);
        _class tmp406 = class_tmp399;
        float north = the_terrain_height_at_3terrain_coordinate8position5_terrain_coordinate(tmp406);
        class_0 class_tmp407 = class_0(0.0, 0.0, 0.0);
        class_tmp407._m0 = left1_4351_43right_f32_f32(west, east);
        float tmp409 = 2.0;
        class_tmp407._m1 = left1_4321_43right_f32_f32(stride, tmp409);
        class_tmp407._m2 = left1_4351_43right_f32_f32(south, north);
        class_0 normal = class_tmp407;
        float tmp415 = left1_4321_43right_f32_f32(normal._m0, normal._m0);
        float tmp418 = left1_4321_43right_f32_f32(normal._m1, normal._m1);
        float tmp419 = left1_4331_43right_f32_f32(tmp415, tmp418);
        float tmp422 = left1_4321_43right_f32_f32(normal._m2, normal._m2);
        float tmp423 = left1_4331_43right_f32_f32(tmp419, tmp422);
        _length = the_square_root_of_value_f32(tmp423);
        normal._m0 = left1_4371_43right_f32_f32(normal._m0, _length);
        normal._m1 = left1_4371_43right_f32_f32(normal._m1, _length);
        normal._m2 = left1_4371_43right_f32_f32(normal._m2, _length);
        float tmp431 = 0.4799999892711639404296875;
        float tmp432 = left1_4321_43right_f32_f32(normal._m0, tmp431);
        float tmp434 = 0.7599999904632568359375;
        float tmp435 = left1_4321_43right_f32_f32(normal._m1, tmp434);
        float tmp436 = left1_4331_43right_f32_f32(tmp432, tmp435);
        float tmp438 = 0.439999997615814208984375;
        float tmp439 = left1_4321_43right_f32_f32(normal._m2, tmp438);
        float tmp440 = left1_4351_43right_f32_f32(tmp436, tmp439);
        float sunlight = number_saturated_f32(tmp440);
        float tmp442 = 0.5;
        float tmp443 = left1_4321_43right_f32_f32(normal._m1, tmp442);
        float tmp444 = 0.5;
        float tmp445 = left1_4331_43right_f32_f32(tmp443, tmp444);
        float skylight = number_saturated_f32(tmp445);
        float tmp448 = 0.7200000286102294921875;
        _class class_tmp446 = _class(0.0, 0.0);
        class_tmp446._m0 = left1_4321_43right_f32_f32(world._m0, tmp448);
        float tmp451 = 0.7200000286102294921875;
        class_tmp446._m1 = left1_4321_43right_f32_f32(world._m2, tmp451);
        _class tmp454 = class_tmp446;
        float tmp455 = 9.3999996185302734375;
        float material = the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp454, tmp455);
        float tmp458 = 1.90999996662139892578125;
        float tmp459 = left1_4321_43right_f32_f32(world._m0, tmp458);
        float tmp460 = 4.0;
        _class class_tmp456 = _class(0.0, 0.0);
        class_tmp456._m0 = left1_4331_43right_f32_f32(tmp459, tmp460);
        float tmp463 = 1.90999996662139892578125;
        float tmp464 = left1_4321_43right_f32_f32(world._m2, tmp463);
        float tmp465 = 7.0;
        class_tmp456._m1 = left1_4351_43right_f32_f32(tmp464, tmp465);
        _class tmp468 = class_tmp456;
        float tmp469 = 3.7999999523162841796875;
        float detail = the_ridged_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp468, tmp469);
        float tmp472 = 3.400000095367431640625;
        float tmp473 = left1_4321_43right_f32_f32(world._m0, tmp472);
        float tmp474 = 9.0;
        _class class_tmp470 = _class(0.0, 0.0);
        class_tmp470._m0 = left1_4351_43right_f32_f32(tmp473, tmp474);
        float tmp477 = 3.400000095367431640625;
        float tmp478 = left1_4321_43right_f32_f32(world._m2, tmp477);
        float tmp479 = 2.0;
        class_tmp470._m1 = left1_4331_43right_f32_f32(tmp478, tmp479);
        _class tmp482 = class_tmp470;
        float tmp483 = 6.099999904632568359375;
        float mineral = the_flowing_field_at_3shader_point8point5_during_3any8phase5_shader_point_f32(tmp482, tmp483);
        float tmp484 = 0.1599999964237213134765625;
        float tmp485 = 0.61000001430511474609375;
        float tmp486 = 1.0;
        float tmp488 = left1_4351_43right_f32_f32(tmp486, normal._m1);
        float exposure = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp484, tmp485, tmp488);
        float tmp489 = 1.480000019073486328125;
        float tmp490 = 2.2799999713897705078125;
        float snow = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp489, tmp490, height);
        float tmp492 = 0.017999999225139617919921875;
        float tmp493 = 0.046999998390674591064453125;
        float tmp494 = left1_4321_43right_f32_f32(material, tmp493);
        float tmp495 = left1_4331_43right_f32_f32(tmp492, tmp494);
        float tmp496 = 0.02099999971687793731689453125;
        float tmp497 = left1_4321_43right_f32_f32(detail, tmp496);
        class_0 class_tmp491 = class_0(0.0, 0.0, 0.0);
        class_tmp491._m0 = left1_4331_43right_f32_f32(tmp495, tmp497);
        float tmp499 = 0.07500000298023223876953125;
        float tmp500 = 0.1500000059604644775390625;
        float tmp501 = left1_4321_43right_f32_f32(material, tmp500);
        float tmp502 = left1_4331_43right_f32_f32(tmp499, tmp501);
        float tmp503 = 0.05200000107288360595703125;
        float tmp504 = left1_4321_43right_f32_f32(detail, tmp503);
        class_tmp491._m1 = left1_4331_43right_f32_f32(tmp502, tmp504);
        float tmp506 = 0.05200000107288360595703125;
        float tmp507 = 0.082999996840953826904296875;
        float tmp508 = left1_4321_43right_f32_f32(material, tmp507);
        float tmp509 = left1_4331_43right_f32_f32(tmp506, tmp508);
        float tmp510 = 0.0390000008046627044677734375;
        float tmp511 = left1_4321_43right_f32_f32(detail, tmp510);
        class_tmp491._m2 = left1_4331_43right_f32_f32(tmp509, tmp511);
        class_0 ground = class_tmp491;
        float tmp515 = 0.115000002086162567138671875;
        float tmp516 = 0.10999999940395355224609375;
        float tmp517 = left1_4321_43right_f32_f32(material, tmp516);
        float tmp518 = left1_4331_43right_f32_f32(tmp515, tmp517);
        float tmp519 = 0.08699999749660491943359375;
        float tmp520 = left1_4321_43right_f32_f32(mineral, tmp519);
        class_0 class_tmp514 = class_0(0.0, 0.0, 0.0);
        class_tmp514._m0 = left1_4331_43right_f32_f32(tmp518, tmp520);
        float tmp522 = 0.097999997437000274658203125;
        float tmp523 = 0.07500000298023223876953125;
        float tmp524 = left1_4321_43right_f32_f32(material, tmp523);
        float tmp525 = left1_4331_43right_f32_f32(tmp522, tmp524);
        float tmp526 = 0.0610000006854534149169921875;
        float tmp527 = left1_4321_43right_f32_f32(mineral, tmp526);
        class_tmp514._m1 = left1_4331_43right_f32_f32(tmp525, tmp527);
        float tmp529 = 0.104999996721744537353515625;
        float tmp530 = 0.0949999988079071044921875;
        float tmp531 = left1_4321_43right_f32_f32(material, tmp530);
        float tmp532 = left1_4331_43right_f32_f32(tmp529, tmp531);
        float tmp533 = 0.07299999892711639404296875;
        float tmp534 = left1_4321_43right_f32_f32(mineral, tmp533);
        class_tmp514._m2 = left1_4331_43right_f32_f32(tmp532, tmp534);
        class_0 rock = class_tmp514;
        float tmp538 = 1.0;
        float tmp539 = left1_4351_43right_f32_f32(tmp538, exposure);
        float tmp540 = left1_4321_43right_f32_f32(ground._m0, tmp539);
        float tmp542 = left1_4321_43right_f32_f32(rock._m0, exposure);
        ground._m0 = left1_4331_43right_f32_f32(tmp540, tmp542);
        float tmp545 = 1.0;
        float tmp546 = left1_4351_43right_f32_f32(tmp545, exposure);
        float tmp547 = left1_4321_43right_f32_f32(ground._m1, tmp546);
        float tmp549 = left1_4321_43right_f32_f32(rock._m1, exposure);
        ground._m1 = left1_4331_43right_f32_f32(tmp547, tmp549);
        float tmp552 = 1.0;
        float tmp553 = left1_4351_43right_f32_f32(tmp552, exposure);
        float tmp554 = left1_4321_43right_f32_f32(ground._m2, tmp553);
        float tmp556 = left1_4321_43right_f32_f32(rock._m2, exposure);
        ground._m2 = left1_4331_43right_f32_f32(tmp554, tmp556);
        float tmp559 = 1.0;
        float tmp560 = left1_4351_43right_f32_f32(tmp559, snow);
        float tmp561 = left1_4321_43right_f32_f32(ground._m0, tmp560);
        float tmp562 = 0.7799999713897705078125;
        float tmp563 = left1_4321_43right_f32_f32(tmp562, snow);
        ground._m0 = left1_4331_43right_f32_f32(tmp561, tmp563);
        float tmp566 = 1.0;
        float tmp567 = left1_4351_43right_f32_f32(tmp566, snow);
        float tmp568 = left1_4321_43right_f32_f32(ground._m1, tmp567);
        float tmp569 = 0.839999973773956298828125;
        float tmp570 = left1_4321_43right_f32_f32(tmp569, snow);
        ground._m1 = left1_4331_43right_f32_f32(tmp568, tmp570);
        float tmp573 = 1.0;
        float tmp574 = left1_4351_43right_f32_f32(tmp573, snow);
        float tmp575 = left1_4321_43right_f32_f32(ground._m2, tmp574);
        float tmp576 = 0.88999998569488525390625;
        float tmp577 = left1_4321_43right_f32_f32(tmp576, snow);
        ground._m2 = left1_4331_43right_f32_f32(tmp575, tmp577);
        float tmp579 = 0.10999999940395355224609375;
        float tmp580 = 1.12999999523162841796875;
        float tmp581 = left1_4321_43right_f32_f32(sunlight, tmp580);
        float tmp582 = left1_4331_43right_f32_f32(tmp579, tmp581);
        float tmp583 = 0.1599999964237213134765625;
        float tmp584 = left1_4321_43right_f32_f32(skylight, tmp583);
        float lighting = left1_4331_43right_f32_f32(tmp582, tmp584);
        float tmp586 = left1_4321_43right_f32_f32(ground._m0, lighting);
        float tmp587 = 0.23999999463558197021484375;
        float tmp588 = left1_4321_43right_f32_f32(sunlight, tmp587);
        ground._m0 = left1_4331_43right_f32_f32(tmp586, tmp588);
        float tmp591 = left1_4321_43right_f32_f32(ground._m1, lighting);
        float tmp592 = 0.17000000178813934326171875;
        float tmp593 = left1_4321_43right_f32_f32(sunlight, tmp592);
        ground._m1 = left1_4331_43right_f32_f32(tmp591, tmp593);
        float tmp596 = left1_4321_43right_f32_f32(ground._m2, lighting);
        float tmp597 = 0.0900000035762786865234375;
        float tmp598 = left1_4321_43right_f32_f32(sunlight, tmp597);
        ground._m2 = left1_4331_43right_f32_f32(tmp596, tmp598);
        float tmp600 = 11.0;
        float tmp601 = 41.0;
        float fog = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp600, tmp601, traversal._m4);
        float tmp604 = 0.189999997615814208984375;
        float tmp605 = 0.1599999964237213134765625;
        float tmp606 = left1_4321_43right_f32_f32(halo, tmp605);
        class_0 class_tmp603 = class_0(0.0, 0.0, 0.0);
        class_tmp603._m0 = left1_4331_43right_f32_f32(tmp604, tmp606);
        float tmp608 = 0.25;
        float tmp609 = 0.100000001490116119384765625;
        float tmp610 = left1_4321_43right_f32_f32(halo, tmp609);
        class_tmp603._m1 = left1_4331_43right_f32_f32(tmp608, tmp610);
        float tmp612 = 0.310000002384185791015625;
        float tmp613 = 0.0500000007450580596923828125;
        float tmp614 = left1_4321_43right_f32_f32(halo, tmp613);
        class_tmp603._m2 = left1_4331_43right_f32_f32(tmp612, tmp614);
        class_0 atmosphere = class_tmp603;
        float tmp618 = 1.0;
        float tmp619 = left1_4351_43right_f32_f32(tmp618, fog);
        float tmp620 = left1_4321_43right_f32_f32(ground._m0, tmp619);
        float tmp622 = left1_4321_43right_f32_f32(atmosphere._m0, fog);
        color._m0 = left1_4331_43right_f32_f32(tmp620, tmp622);
        float tmp625 = 1.0;
        float tmp626 = left1_4351_43right_f32_f32(tmp625, fog);
        float tmp627 = left1_4321_43right_f32_f32(ground._m1, tmp626);
        float tmp629 = left1_4321_43right_f32_f32(atmosphere._m1, fog);
        color._m1 = left1_4331_43right_f32_f32(tmp627, tmp629);
        float tmp632 = 1.0;
        float tmp633 = left1_4351_43right_f32_f32(tmp632, fog);
        float tmp634 = left1_4321_43right_f32_f32(ground._m2, tmp633);
        float tmp636 = left1_4321_43right_f32_f32(atmosphere._m2, fog);
        color._m2 = left1_4331_43right_f32_f32(tmp634, tmp636);
    }
    float tmp639 = left1_4371_43right_f32_f32(screen._m0, aspect);
    float tmp641 = left1_4371_43right_f32_f32(screen._m0, aspect);
    float tmp642 = left1_4321_43right_f32_f32(tmp639, tmp641);
    float tmp645 = left1_4321_43right_f32_f32(screen._m1, screen._m1);
    float tmp646 = left1_4331_43right_f32_f32(tmp642, tmp645);
    float edge = the_square_root_of_value_f32(tmp646);
    float tmp647 = 0.3400000035762786865234375;
    float tmp648 = 1.0;
    float tmp649 = 0.569999992847442626953125;
    float tmp650 = 1.33000004291534423828125;
    float tmp651 = the_smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp649, tmp650, edge);
    float tmp652 = left1_4351_43right_f32_f32(tmp648, tmp651);
    float tmp653 = 0.660000026226043701171875;
    float tmp654 = left1_4321_43right_f32_f32(tmp652, tmp653);
    float vignette = left1_4331_43right_f32_f32(tmp647, tmp654);
    float tmp656 = left1_4321_43right_f32_f32(color._m0, vignette);
    float tmp657 = 1.0;
    float tmp659 = 0.4199999868869781494140625;
    float tmp660 = left1_4321_43right_f32_f32(color._m0, tmp659);
    float tmp661 = left1_4331_43right_f32_f32(tmp657, tmp660);
    float tmp662 = left1_4371_43right_f32_f32(tmp656, tmp661);
    color._m0 = the_square_root_of_value_f32(tmp662);
    float tmp665 = left1_4321_43right_f32_f32(color._m1, vignette);
    float tmp666 = 1.0;
    float tmp668 = 0.4199999868869781494140625;
    float tmp669 = left1_4321_43right_f32_f32(color._m1, tmp668);
    float tmp670 = left1_4331_43right_f32_f32(tmp666, tmp669);
    float tmp671 = left1_4371_43right_f32_f32(tmp665, tmp670);
    color._m1 = the_square_root_of_value_f32(tmp671);
    float tmp674 = left1_4321_43right_f32_f32(color._m2, vignette);
    float tmp675 = 1.0;
    float tmp677 = 0.4199999868869781494140625;
    float tmp678 = left1_4321_43right_f32_f32(color._m2, tmp677);
    float tmp679 = left1_4331_43right_f32_f32(tmp675, tmp678);
    float tmp680 = left1_4371_43right_f32_f32(tmp674, tmp679);
    color._m2 = the_square_root_of_value_f32(tmp680);
    vec4 _1188 = vec4(0.0, 0.0, 0.0, 1.0);
    _1188.z = color._m2;
    _1188.y = color._m1;
    _1188.x = color._m0;
    dynlexColor = _1188;
}
