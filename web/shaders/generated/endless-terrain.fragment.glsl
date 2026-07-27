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
in vec4 dynlex_interpolant_7465727261696e5f706f736974696f6e;
in vec4 dynlex_interpolant_7465727261696e5f6e6f726d616c;
in vec4 dynlex_interpolant_7465727261696e5f6d6174657269616c;

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

float glow_from_inner_to_outer_at_sample_f32_f32_f32(float inner, float outer, float _sample)
{
    float tmp = 1.0;
    float tmp1 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(inner, outer, _sample);
    return left1_4351_43right_f32_f32(tmp, tmp1);
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
    float world_x = dynlex_interpolant_7465727261696e5f706f736974696f6e.x;
    float world_y = dynlex_interpolant_7465727261696e5f706f736974696f6e.y;
    float world_z = dynlex_interpolant_7465727261696e5f706f736974696f6e.z;
    float surface_kind = dynlex_interpolant_7465727261696e5f706f736974696f6e.w;
    float normal_x = dynlex_interpolant_7465727261696e5f6e6f726d616c.x;
    float normal_y = dynlex_interpolant_7465727261696e5f6e6f726d616c.y;
    float normal_z = dynlex_interpolant_7465727261696e5f6e6f726d616c.z;
    float view_distance = dynlex_interpolant_7465727261696e5f6e6f726d616c.w;
    float surface_variation = dynlex_interpolant_7465727261696e5f6d6174657269616c.x;
    float surface_detail = dynlex_interpolant_7465727261696e5f6d6174657269616c.y;
    float tmp32 = 0.519999980926513671875;
    float sun_x = left1_4351_43right_f32_f32(screen_x, tmp32);
    float tmp33 = 0.36000001430511474609375;
    float sun_y = left1_4351_43right_f32_f32(screen_y, tmp33);
    float tmp34 = left1_4321_43right_f32_f32(sun_x, sun_x);
    float tmp35 = left1_4321_43right_f32_f32(sun_y, sun_y);
    float tmp36 = left1_4331_43right_f32_f32(tmp34, tmp35);
    float sun_distance = _the43_square_root_of_value_f32(tmp36);
    float tmp37 = 0.01400000043213367462158203125;
    float tmp38 = 0.064999997615814208984375;
    float sun_core = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp37, tmp38, sun_distance);
    float tmp39 = 0.039999999105930328369140625;
    float tmp40 = 0.439999997615814208984375;
    float sun_halo = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp39, tmp40, sun_distance);
    float red = 0.02500000037252902984619140625;
    float green = 0.064000003039836883544921875;
    float blue = 0.12999999523162841796875;
    float tmp41 = 0.5;
    if (left_0_right_f32_f32(surface_kind, tmp41))
    {
        float tmp42 = 0.4799999892711639404296875;
        float tmp43 = left1_4321_43right_f32_f32(screen_y, tmp42);
        float tmp44 = 0.430000007152557373046875;
        float tmp45 = left1_4331_43right_f32_f32(tmp43, tmp44);
        float sky_height = saturate_number_f32(tmp45);
        float tmp46 = 0.02199999988079071044921875;
        float tmp47 = 0.104999996721744537353515625;
        float tmp48 = left1_4321_43right_f32_f32(sky_height, tmp47);
        red = left1_4331_43right_f32_f32(tmp46, tmp48);
        float tmp49 = 0.0610000006854534149169921875;
        float tmp50 = 0.180000007152557373046875;
        float tmp51 = left1_4321_43right_f32_f32(sky_height, tmp50);
        green = left1_4331_43right_f32_f32(tmp49, tmp51);
        float tmp52 = 0.14000000059604644775390625;
        float tmp53 = 0.3300000131130218505859375;
        float tmp54 = left1_4321_43right_f32_f32(sky_height, tmp53);
        blue = left1_4331_43right_f32_f32(tmp52, tmp54);
        float tmp55 = 1.21000003814697265625;
        float tmp56 = left1_4321_43right_f32_f32(screen_x, tmp55);
        float tmp57 = 1.769999980926513671875;
        float tmp58 = left1_4321_43right_f32_f32(screen_y, tmp57);
        float tmp59 = 0.02099999971687793731689453125;
        float tmp60 = left1_4321_43right_f32_f32(time, tmp59);
        float tmp61 = left1_4331_43right_f32_f32(tmp58, tmp60);
        float tmp62 = left1_4331_43right_f32_f32(tmp56, tmp61);
        float tmp63 = _the43_sine_of_value_f32(tmp62);
        float tmp64 = 0.5;
        float tmp65 = left1_4321_43right_f32_f32(tmp63, tmp64);
        float tmp66 = 0.5;
        float cloud_fold = left1_4331_43right_f32_f32(tmp65, tmp66);
        float tmp67 = 0.670000016689300537109375;
        float tmp68 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp67);
        float tmp69 = left1_4321_43right_f32_f32(screen_x, tmp68);
        float tmp70 = 2.9300000667572021484375;
        float tmp71 = left1_4321_43right_f32_f32(screen_y, tmp70);
        float tmp72 = 0.01600000075995922088623046875;
        float tmp73 = left1_4321_43right_f32_f32(time, tmp72);
        float tmp74 = left1_4351_43right_f32_f32(tmp71, tmp73);
        float tmp75 = left1_4331_43right_f32_f32(tmp69, tmp74);
        float tmp76 = _the43_cosine_of_value_f32(tmp75);
        float tmp77 = 0.5;
        float tmp78 = left1_4321_43right_f32_f32(tmp76, tmp77);
        float tmp79 = 0.5;
        float cloud_crossing = left1_4331_43right_f32_f32(tmp78, tmp79);
        float tmp80 = 2.4700000286102294921875;
        float tmp81 = left1_4321_43right_f32_f32(screen_x, tmp80);
        float tmp82 = 4.110000133514404296875;
        float tmp83 = left1_4321_43right_f32_f32(screen_y, tmp82);
        float tmp84 = 1.7999999523162841796875;
        float tmp85 = left1_4321_43right_f32_f32(cloud_fold, tmp84);
        float tmp86 = left1_4331_43right_f32_f32(tmp83, tmp85);
        float tmp87 = left1_4351_43right_f32_f32(tmp81, tmp86);
        float tmp88 = _the43_sine_of_value_f32(tmp87);
        float tmp89 = 0.5;
        float tmp90 = left1_4321_43right_f32_f32(tmp88, tmp89);
        float tmp91 = 0.5;
        float cloud_detail = left1_4331_43right_f32_f32(tmp90, tmp91);
        float tmp92 = 1.41999995708465576171875;
        float tmp93 = 2.1800000667572021484375;
        float tmp94 = left1_4331_43right_f32_f32(cloud_fold, cloud_crossing);
        float tmp95 = 0.4600000083446502685546875;
        float tmp96 = left1_4321_43right_f32_f32(cloud_detail, tmp95);
        float tmp97 = left1_4331_43right_f32_f32(tmp94, tmp96);
        float cloud_shape = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp92, tmp93, tmp97);
        float tmp98 = 0.07999999821186065673828125;
        float tmp99 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp98);
        float tmp100 = 0.37999999523162841796875;
        float cloud_altitude = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp99, tmp100, screen_y);
        float cloud_light = left1_4321_43right_f32_f32(cloud_shape, cloud_altitude);
        float tmp101 = 0.3300000131130218505859375;
        float tmp102 = left1_4321_43right_f32_f32(cloud_light, tmp101);
        red = left1_4331_43right_f32_f32(red, tmp102);
        float tmp103 = 0.310000002384185791015625;
        float tmp104 = left1_4321_43right_f32_f32(cloud_light, tmp103);
        green = left1_4331_43right_f32_f32(green, tmp104);
        float tmp105 = 0.2899999916553497314453125;
        float tmp106 = left1_4321_43right_f32_f32(cloud_light, tmp105);
        blue = left1_4331_43right_f32_f32(blue, tmp106);
        float tmp107 = 0.310000002384185791015625;
        float tmp108 = left1_4321_43right_f32_f32(sun_halo, tmp107);
        float tmp109 = left1_4331_43right_f32_f32(red, tmp108);
        float tmp110 = 1.36000001430511474609375;
        float tmp111 = left1_4321_43right_f32_f32(sun_core, tmp110);
        red = left1_4331_43right_f32_f32(tmp109, tmp111);
        float tmp112 = 0.189999997615814208984375;
        float tmp113 = left1_4321_43right_f32_f32(sun_halo, tmp112);
        float tmp114 = left1_4331_43right_f32_f32(green, tmp113);
        float tmp115 = 0.959999978542327880859375;
        float tmp116 = left1_4321_43right_f32_f32(sun_core, tmp115);
        green = left1_4331_43right_f32_f32(tmp114, tmp116);
        float tmp117 = 0.0900000035762786865234375;
        float tmp118 = left1_4321_43right_f32_f32(sun_halo, tmp117);
        float tmp119 = left1_4331_43right_f32_f32(blue, tmp118);
        float tmp120 = 0.550000011920928955078125;
        float tmp121 = left1_4321_43right_f32_f32(sun_core, tmp120);
        blue = left1_4331_43right_f32_f32(tmp119, tmp121);
    }
    else
    {
        float tmp122 = left1_4321_43right_f32_f32(normal_x, normal_x);
        float tmp123 = left1_4321_43right_f32_f32(normal_y, normal_y);
        float tmp124 = left1_4331_43right_f32_f32(tmp122, tmp123);
        float tmp125 = left1_4321_43right_f32_f32(normal_z, normal_z);
        float tmp126 = left1_4331_43right_f32_f32(tmp124, tmp125);
        float normal_length = _the43_square_root_of_value_f32(tmp126);
        normal_x = left1_4371_43right_f32_f32(normal_x, normal_length);
        normal_y = left1_4371_43right_f32_f32(normal_y, normal_length);
        normal_z = left1_4371_43right_f32_f32(normal_z, normal_length);
        float tmp127 = 1.0;
        float slope = left1_4351_43right_f32_f32(tmp127, normal_y);
        float tmp128 = 0.430000007152557373046875;
        float tmp129 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp128);
        float tmp130 = left1_4321_43right_f32_f32(normal_x, tmp129);
        float tmp131 = 0.790000021457672119140625;
        float tmp132 = left1_4321_43right_f32_f32(normal_y, tmp131);
        float tmp133 = left1_4331_43right_f32_f32(tmp130, tmp132);
        float tmp134 = 0.439999997615814208984375;
        float tmp135 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp134);
        float tmp136 = left1_4321_43right_f32_f32(normal_z, tmp135);
        float tmp137 = left1_4331_43right_f32_f32(tmp133, tmp136);
        float sunlight = saturate_number_f32(tmp137);
        float tmp138 = 0.560000002384185791015625;
        float tmp139 = left1_4321_43right_f32_f32(normal_y, tmp138);
        float tmp140 = 0.439999997615814208984375;
        float tmp141 = left1_4331_43right_f32_f32(tmp139, tmp140);
        float sky_fill = saturate_number_f32(tmp141);
        float tmp142 = 0.680000007152557373046875;
        float tmp143 = left1_4321_43right_f32_f32(surface_variation, tmp142);
        float tmp144 = 0.319999992847442626953125;
        float tmp145 = left1_4321_43right_f32_f32(surface_detail, tmp144);
        float material_variation = left1_4331_43right_f32_f32(tmp143, tmp145);
        float tmp146 = 8.69999980926513671875;
        float tmp147 = left1_4321_43right_f32_f32(world_y, tmp146);
        float tmp148 = 0.054999999701976776123046875;
        float tmp149 = left1_4321_43right_f32_f32(world_x, tmp148);
        float tmp150 = left1_4331_43right_f32_f32(tmp147, tmp149);
        float tmp151 = 0.0309999994933605194091796875;
        float tmp152 = left1_4321_43right_f32_f32(world_z, tmp151);
        float tmp153 = 2.400000095367431640625;
        float tmp154 = left1_4321_43right_f32_f32(surface_variation, tmp153);
        float tmp155 = left1_4331_43right_f32_f32(tmp152, tmp154);
        float tmp156 = left1_4351_43right_f32_f32(tmp150, tmp155);
        float tmp157 = _the43_sine_of_value_f32(tmp156);
        float tmp158 = 0.5;
        float tmp159 = left1_4321_43right_f32_f32(tmp157, tmp158);
        float tmp160 = 0.5;
        float strata = left1_4331_43right_f32_f32(tmp159, tmp160);
        float tmp161 = 0.319999992847442626953125;
        float tmp162 = left1_4351_43right_f32_f32(surface_detail, tmp161);
        float tmp163 = 1.35000002384185791015625;
        float tmp164 = left1_4321_43right_f32_f32(tmp162, tmp163);
        float tmp165 = 1.7000000476837158203125;
        float tmp166 = left1_4321_43right_f32_f32(slope, tmp165);
        float tmp167 = left1_4331_43right_f32_f32(tmp164, tmp166);
        float fracture = saturate_number_f32(tmp167);
        float tmp168 = 0.054999999701976776123046875;
        float tmp169 = 0.300000011920928955078125;
        float tmp170 = 0.5;
        float tmp171 = left1_4351_43right_f32_f32(fracture, tmp170);
        float tmp172 = 0.0949999988079071044921875;
        float tmp173 = left1_4321_43right_f32_f32(tmp171, tmp172);
        float tmp174 = left1_4331_43right_f32_f32(slope, tmp173);
        float exposed_rock = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp168, tmp169, tmp174);
        float tmp175 = 2.75;
        float tmp176 = 4.44999980926513671875;
        float tmp177 = 0.5;
        float tmp178 = left1_4351_43right_f32_f32(material_variation, tmp177);
        float tmp179 = 0.4799999892711639404296875;
        float tmp180 = left1_4321_43right_f32_f32(tmp178, tmp179);
        float tmp181 = left1_4331_43right_f32_f32(world_y, tmp180);
        float snow = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp175, tmp176, tmp181);
        float tmp182 = 0.25;
        float tmp183 = 0.660000026226043701171875;
        float tmp184 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp182, tmp183, normal_y);
        snow = left1_4321_43right_f32_f32(snow, tmp184);
        float tmp185 = 0.87999999523162841796875;
        float tmp186 = 0.119999997317790985107421875;
        float tmp187 = left1_4321_43right_f32_f32(surface_detail, tmp186);
        float tmp188 = left1_4331_43right_f32_f32(tmp185, tmp187);
        snow = left1_4321_43right_f32_f32(snow, tmp188);
        float tmp189 = 0.3499999940395355224609375;
        float tmp190 = 2.650000095367431640625;
        float alpine_zone = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp189, tmp190, world_y);
        float tmp191 = 0.37999999523162841796875;
        float tmp192 = left1_4321_43right_f32_f32(normal_x, tmp191);
        float tmp193 = 0.2599999904632568359375;
        float tmp194 = left1_4321_43right_f32_f32(normal_z, tmp193);
        float tmp195 = left1_4351_43right_f32_f32(tmp192, tmp194);
        float tmp196 = 0.4799999892711639404296875;
        float tmp197 = left1_4331_43right_f32_f32(tmp195, tmp196);
        float rock_warmth = saturate_number_f32(tmp197);
        float tmp198 = 0.0280000008642673492431640625;
        float tmp199 = 0.0500000007450580596923828125;
        float tmp200 = left1_4321_43right_f32_f32(material_variation, tmp199);
        float tmp201 = left1_4331_43right_f32_f32(tmp198, tmp200);
        float tmp202 = 0.034000001847743988037109375;
        float tmp203 = left1_4321_43right_f32_f32(alpine_zone, tmp202);
        float meadow_red = left1_4331_43right_f32_f32(tmp201, tmp203);
        float tmp204 = 0.09399999678134918212890625;
        float tmp205 = 0.086000002920627593994140625;
        float tmp206 = left1_4321_43right_f32_f32(material_variation, tmp205);
        float tmp207 = left1_4331_43right_f32_f32(tmp204, tmp206);
        float tmp208 = 0.0500000007450580596923828125;
        float tmp209 = left1_4321_43right_f32_f32(alpine_zone, tmp208);
        float meadow_green = left1_4351_43right_f32_f32(tmp207, tmp209);
        float tmp210 = 0.0430000014603137969970703125;
        float tmp211 = 0.03599999845027923583984375;
        float tmp212 = left1_4321_43right_f32_f32(material_variation, tmp211);
        float tmp213 = left1_4331_43right_f32_f32(tmp210, tmp212);
        float tmp214 = 0.0199999995529651641845703125;
        float tmp215 = left1_4321_43right_f32_f32(alpine_zone, tmp214);
        float meadow_blue = left1_4331_43right_f32_f32(tmp213, tmp215);
        float tmp216 = 0.104999996721744537353515625;
        float tmp217 = 0.06599999964237213134765625;
        float tmp218 = left1_4321_43right_f32_f32(material_variation, tmp217);
        float tmp219 = left1_4331_43right_f32_f32(tmp216, tmp218);
        float tmp220 = 0.07400000095367431640625;
        float tmp221 = left1_4321_43right_f32_f32(rock_warmth, tmp220);
        float tmp222 = left1_4331_43right_f32_f32(tmp219, tmp221);
        float tmp223 = 0.0280000008642673492431640625;
        float tmp224 = left1_4321_43right_f32_f32(strata, tmp223);
        float rock_red = left1_4331_43right_f32_f32(tmp222, tmp224);
        float tmp225 = 0.101000003516674041748046875;
        float tmp226 = 0.0579999983310699462890625;
        float tmp227 = left1_4321_43right_f32_f32(material_variation, tmp226);
        float tmp228 = left1_4331_43right_f32_f32(tmp225, tmp227);
        float tmp229 = 0.046999998390674591064453125;
        float tmp230 = left1_4321_43right_f32_f32(rock_warmth, tmp229);
        float tmp231 = left1_4331_43right_f32_f32(tmp228, tmp230);
        float tmp232 = 0.02199999988079071044921875;
        float tmp233 = left1_4321_43right_f32_f32(strata, tmp232);
        float rock_green = left1_4331_43right_f32_f32(tmp231, tmp233);
        float tmp234 = 0.1089999973773956298828125;
        float tmp235 = 0.0540000014007091522216796875;
        float tmp236 = left1_4321_43right_f32_f32(material_variation, tmp235);
        float tmp237 = left1_4331_43right_f32_f32(tmp234, tmp236);
        float tmp238 = 0.037000000476837158203125;
        float tmp239 = left1_4321_43right_f32_f32(rock_warmth, tmp238);
        float tmp240 = left1_4331_43right_f32_f32(tmp237, tmp239);
        float tmp241 = 0.01899999938905239105224609375;
        float tmp242 = left1_4321_43right_f32_f32(strata, tmp241);
        float rock_blue = left1_4331_43right_f32_f32(tmp240, tmp242);
        float tmp243 = 1.0;
        float tmp244 = left1_4351_43right_f32_f32(tmp243, exposed_rock);
        float tmp245 = left1_4321_43right_f32_f32(meadow_red, tmp244);
        float tmp246 = left1_4321_43right_f32_f32(rock_red, exposed_rock);
        float ground_red = left1_4331_43right_f32_f32(tmp245, tmp246);
        float tmp247 = 1.0;
        float tmp248 = left1_4351_43right_f32_f32(tmp247, exposed_rock);
        float tmp249 = left1_4321_43right_f32_f32(meadow_green, tmp248);
        float tmp250 = left1_4321_43right_f32_f32(rock_green, exposed_rock);
        float ground_green = left1_4331_43right_f32_f32(tmp249, tmp250);
        float tmp251 = 1.0;
        float tmp252 = left1_4351_43right_f32_f32(tmp251, exposed_rock);
        float tmp253 = left1_4321_43right_f32_f32(meadow_blue, tmp252);
        float tmp254 = left1_4321_43right_f32_f32(rock_blue, exposed_rock);
        float ground_blue = left1_4331_43right_f32_f32(tmp253, tmp254);
        float tmp255 = 1.0;
        float tmp256 = left1_4351_43right_f32_f32(tmp255, snow);
        float tmp257 = left1_4321_43right_f32_f32(ground_red, tmp256);
        float tmp258 = 0.7799999713897705078125;
        float tmp259 = left1_4321_43right_f32_f32(tmp258, snow);
        ground_red = left1_4331_43right_f32_f32(tmp257, tmp259);
        float tmp260 = 1.0;
        float tmp261 = left1_4351_43right_f32_f32(tmp260, snow);
        float tmp262 = left1_4321_43right_f32_f32(ground_green, tmp261);
        float tmp263 = 0.829999983310699462890625;
        float tmp264 = left1_4321_43right_f32_f32(tmp263, snow);
        ground_green = left1_4331_43right_f32_f32(tmp262, tmp264);
        float tmp265 = 1.0;
        float tmp266 = left1_4351_43right_f32_f32(tmp265, snow);
        float tmp267 = left1_4321_43right_f32_f32(ground_blue, tmp266);
        float tmp268 = 0.87000000476837158203125;
        float tmp269 = left1_4321_43right_f32_f32(tmp268, snow);
        ground_blue = left1_4331_43right_f32_f32(tmp267, tmp269);
        float tmp270 = 0.800000011920928955078125;
        float tmp271 = 0.2599999904632568359375;
        float tmp272 = left1_4321_43right_f32_f32(surface_detail, tmp271);
        float material_light = left1_4331_43right_f32_f32(tmp270, tmp272);
        float tmp273 = 0.17000000178813934326171875;
        float tmp274 = 1.15999996662139892578125;
        float tmp275 = left1_4321_43right_f32_f32(sunlight, tmp274);
        float tmp276 = left1_4331_43right_f32_f32(tmp273, tmp275);
        float tmp277 = 0.17000000178813934326171875;
        float tmp278 = left1_4321_43right_f32_f32(sky_fill, tmp277);
        float tmp279 = left1_4331_43right_f32_f32(tmp276, tmp278);
        float lighting = left1_4321_43right_f32_f32(tmp279, material_light);
        float tmp280 = left1_4321_43right_f32_f32(ground_red, lighting);
        float tmp281 = 0.12999999523162841796875;
        float tmp282 = left1_4321_43right_f32_f32(sunlight, tmp281);
        ground_red = left1_4331_43right_f32_f32(tmp280, tmp282);
        float tmp283 = left1_4321_43right_f32_f32(ground_green, lighting);
        float tmp284 = 0.100000001490116119384765625;
        float tmp285 = left1_4321_43right_f32_f32(sunlight, tmp284);
        ground_green = left1_4331_43right_f32_f32(tmp283, tmp285);
        float tmp286 = left1_4321_43right_f32_f32(ground_blue, lighting);
        float tmp287 = 0.070000000298023223876953125;
        float tmp288 = left1_4321_43right_f32_f32(sunlight, tmp287);
        ground_blue = left1_4331_43right_f32_f32(tmp286, tmp288);
        float tmp289 = 47.0;
        float tmp290 = 94.0;
        float fog = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp289, tmp290, view_distance);
        float tmp291 = 0.17000000178813934326171875;
        float tmp292 = 0.17000000178813934326171875;
        float tmp293 = left1_4321_43right_f32_f32(sun_halo, tmp292);
        float fog_red = left1_4331_43right_f32_f32(tmp291, tmp293);
        float tmp294 = 0.23000000417232513427734375;
        float tmp295 = 0.104999996721744537353515625;
        float tmp296 = left1_4321_43right_f32_f32(sun_halo, tmp295);
        float fog_green = left1_4331_43right_f32_f32(tmp294, tmp296);
        float tmp297 = 0.310000002384185791015625;
        float tmp298 = 0.05200000107288360595703125;
        float tmp299 = left1_4321_43right_f32_f32(sun_halo, tmp298);
        float fog_blue = left1_4331_43right_f32_f32(tmp297, tmp299);
        float tmp300 = 1.0;
        float tmp301 = left1_4351_43right_f32_f32(tmp300, fog);
        float tmp302 = left1_4321_43right_f32_f32(ground_red, tmp301);
        float tmp303 = left1_4321_43right_f32_f32(fog_red, fog);
        red = left1_4331_43right_f32_f32(tmp302, tmp303);
        float tmp304 = 1.0;
        float tmp305 = left1_4351_43right_f32_f32(tmp304, fog);
        float tmp306 = left1_4321_43right_f32_f32(ground_green, tmp305);
        float tmp307 = left1_4321_43right_f32_f32(fog_green, fog);
        green = left1_4331_43right_f32_f32(tmp306, tmp307);
        float tmp308 = 1.0;
        float tmp309 = left1_4351_43right_f32_f32(tmp308, fog);
        float tmp310 = left1_4321_43right_f32_f32(ground_blue, tmp309);
        float tmp311 = left1_4321_43right_f32_f32(fog_blue, fog);
        blue = left1_4331_43right_f32_f32(tmp310, tmp311);
    }
    float tmp312 = left1_4371_43right_f32_f32(screen_x, aspect);
    float tmp313 = left1_4371_43right_f32_f32(screen_x, aspect);
    float tmp314 = left1_4321_43right_f32_f32(tmp312, tmp313);
    float tmp315 = left1_4321_43right_f32_f32(screen_y, screen_y);
    float tmp316 = left1_4331_43right_f32_f32(tmp314, tmp315);
    float vignette_radius = _the43_square_root_of_value_f32(tmp316);
    float tmp317 = 0.37999999523162841796875;
    float tmp318 = 1.0;
    float tmp319 = 0.579999983310699462890625;
    float tmp320 = 1.34000003337860107421875;
    float tmp321 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp319, tmp320, vignette_radius);
    float tmp322 = left1_4351_43right_f32_f32(tmp318, tmp321);
    float tmp323 = 0.62000000476837158203125;
    float tmp324 = left1_4321_43right_f32_f32(tmp322, tmp323);
    float vignette = left1_4331_43right_f32_f32(tmp317, tmp324);
    float tmp325 = left1_4321_43right_f32_f32(red, vignette);
    float tmp326 = 1.0;
    float tmp327 = 0.37999999523162841796875;
    float tmp328 = left1_4321_43right_f32_f32(red, tmp327);
    float tmp329 = left1_4331_43right_f32_f32(tmp326, tmp328);
    float tmp330 = left1_4371_43right_f32_f32(tmp325, tmp329);
    red = _the43_square_root_of_value_f32(tmp330);
    float tmp331 = left1_4321_43right_f32_f32(green, vignette);
    float tmp332 = 1.0;
    float tmp333 = 0.37999999523162841796875;
    float tmp334 = left1_4321_43right_f32_f32(green, tmp333);
    float tmp335 = left1_4331_43right_f32_f32(tmp332, tmp334);
    float tmp336 = left1_4371_43right_f32_f32(tmp331, tmp335);
    green = _the43_square_root_of_value_f32(tmp336);
    float tmp337 = left1_4321_43right_f32_f32(blue, vignette);
    float tmp338 = 1.0;
    float tmp339 = 0.37999999523162841796875;
    float tmp340 = left1_4321_43right_f32_f32(blue, tmp339);
    float tmp341 = left1_4331_43right_f32_f32(tmp338, tmp340);
    float tmp342 = left1_4371_43right_f32_f32(tmp337, tmp341);
    blue = _the43_square_root_of_value_f32(tmp342);
    vec4 _809 = vec4(0.0, 0.0, 0.0, 1.0);
    _809.z = blue;
    _809.y = green;
    _809.x = red;
    dynlexColor = _809;
}
