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
        float tmp127 = 0.430000007152557373046875;
        float tmp128 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp127);
        float tmp129 = left1_4321_43right_f32_f32(normal_x, tmp128);
        float tmp130 = 0.790000021457672119140625;
        float tmp131 = left1_4321_43right_f32_f32(normal_y, tmp130);
        float tmp132 = left1_4331_43right_f32_f32(tmp129, tmp131);
        float tmp133 = 0.439999997615814208984375;
        float tmp134 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp133);
        float tmp135 = left1_4321_43right_f32_f32(normal_z, tmp134);
        float tmp136 = left1_4331_43right_f32_f32(tmp132, tmp135);
        float sunlight = saturate_number_f32(tmp136);
        float tmp137 = 0.560000002384185791015625;
        float tmp138 = left1_4321_43right_f32_f32(normal_y, tmp137);
        float tmp139 = 0.439999997615814208984375;
        float tmp140 = left1_4331_43right_f32_f32(tmp138, tmp139);
        float sky_fill = saturate_number_f32(tmp140);
        float tmp143 = 1.5;
        if (left_2_right_f32_f32(surface_kind, tmp143))
        {
            float tmp144 = 0.1599999964237213134765625;
            float tmp145 = 7.0;
            float tmp146 = 82.0;
            float tmp147 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp145, tmp146, view_distance);
            float tmp148 = 0.579999983310699462890625;
            float tmp149 = left1_4321_43right_f32_f32(tmp147, tmp148);
            float water_fresnel = left1_4331_43right_f32_f32(tmp144, tmp149);
            float tmp150 = 0.75499999523162841796875;
            float tmp151 = 0.805000007152557373046875;
            float tmp152 = 0.5;
            float tmp153 = left1_4351_43right_f32_f32(surface_detail, tmp152);
            float tmp154 = 0.0240000002086162567138671875;
            float tmp155 = left1_4321_43right_f32_f32(tmp153, tmp154);
            float tmp156 = left1_4331_43right_f32_f32(sunlight, tmp155);
            float water_shimmer = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp150, tmp151, tmp156);
            water_shimmer = left1_4321_43right_f32_f32(water_shimmer, water_shimmer);
            float tmp157 = 0.00999999977648258209228515625;
            float tmp158 = 0.017999999225139617919921875;
            float tmp159 = left1_4321_43right_f32_f32(surface_variation, tmp158);
            float water_red = left1_4331_43right_f32_f32(tmp157, tmp159);
            float tmp160 = 0.0719999969005584716796875;
            float tmp161 = 0.05200000107288360595703125;
            float tmp162 = left1_4321_43right_f32_f32(surface_variation, tmp161);
            float water_green = left1_4331_43right_f32_f32(tmp160, tmp162);
            float tmp163 = 0.104999996721744537353515625;
            float tmp164 = 0.078000001609325408935546875;
            float tmp165 = left1_4321_43right_f32_f32(surface_variation, tmp164);
            float water_blue = left1_4331_43right_f32_f32(tmp163, tmp165);
            float tmp166 = 0.054999999701976776123046875;
            float tmp167 = 0.4799999892711639404296875;
            float tmp168 = left1_4321_43right_f32_f32(sun_halo, tmp167);
            float reflection_red = left1_4331_43right_f32_f32(tmp166, tmp168);
            float tmp169 = 0.14499999582767486572265625;
            float tmp170 = 0.37000000476837158203125;
            float tmp171 = left1_4321_43right_f32_f32(sun_halo, tmp170);
            float reflection_green = left1_4331_43right_f32_f32(tmp169, tmp171);
            float tmp172 = 0.24500000476837158203125;
            float tmp173 = 0.25;
            float tmp174 = left1_4321_43right_f32_f32(sun_halo, tmp173);
            float reflection_blue = left1_4331_43right_f32_f32(tmp172, tmp174);
            float tmp175 = 1.0;
            float tmp176 = left1_4351_43right_f32_f32(tmp175, water_fresnel);
            float tmp177 = left1_4321_43right_f32_f32(water_red, tmp176);
            float tmp178 = left1_4321_43right_f32_f32(reflection_red, water_fresnel);
            water_red = left1_4331_43right_f32_f32(tmp177, tmp178);
            float tmp179 = 1.0;
            float tmp180 = left1_4351_43right_f32_f32(tmp179, water_fresnel);
            float tmp181 = left1_4321_43right_f32_f32(water_green, tmp180);
            float tmp182 = left1_4321_43right_f32_f32(reflection_green, water_fresnel);
            water_green = left1_4331_43right_f32_f32(tmp181, tmp182);
            float tmp183 = 1.0;
            float tmp184 = left1_4351_43right_f32_f32(tmp183, water_fresnel);
            float tmp185 = left1_4321_43right_f32_f32(water_blue, tmp184);
            float tmp186 = left1_4321_43right_f32_f32(reflection_blue, water_fresnel);
            water_blue = left1_4331_43right_f32_f32(tmp185, tmp186);
            float tmp187 = 0.430000007152557373046875;
            float tmp188 = left1_4321_43right_f32_f32(water_shimmer, tmp187);
            water_red = left1_4331_43right_f32_f32(water_red, tmp188);
            float tmp189 = 0.3400000035762786865234375;
            float tmp190 = left1_4321_43right_f32_f32(water_shimmer, tmp189);
            water_green = left1_4331_43right_f32_f32(water_green, tmp190);
            float tmp191 = 0.2199999988079071044921875;
            float tmp192 = left1_4321_43right_f32_f32(water_shimmer, tmp191);
            water_blue = left1_4331_43right_f32_f32(water_blue, tmp192);
            float tmp193 = 58.0;
            float tmp194 = 94.0;
            float water_fog = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp193, tmp194, view_distance);
            float tmp195 = 1.0;
            float tmp196 = left1_4351_43right_f32_f32(tmp195, water_fog);
            float tmp197 = left1_4321_43right_f32_f32(water_red, tmp196);
            float tmp198 = 0.17000000178813934326171875;
            float tmp199 = left1_4321_43right_f32_f32(tmp198, water_fog);
            red = left1_4331_43right_f32_f32(tmp197, tmp199);
            float tmp200 = 1.0;
            float tmp201 = left1_4351_43right_f32_f32(tmp200, water_fog);
            float tmp202 = left1_4321_43right_f32_f32(water_green, tmp201);
            float tmp203 = 0.23000000417232513427734375;
            float tmp204 = left1_4321_43right_f32_f32(tmp203, water_fog);
            green = left1_4331_43right_f32_f32(tmp202, tmp204);
            float tmp205 = 1.0;
            float tmp206 = left1_4351_43right_f32_f32(tmp205, water_fog);
            float tmp207 = left1_4321_43right_f32_f32(water_blue, tmp206);
            float tmp208 = 0.310000002384185791015625;
            float tmp209 = left1_4321_43right_f32_f32(tmp208, water_fog);
            blue = left1_4331_43right_f32_f32(tmp207, tmp209);
        }
        else
        {
            float tmp211 = 1.0;
            float slope = left1_4351_43right_f32_f32(tmp211, normal_y);
            float tmp212 = 0.680000007152557373046875;
            float tmp213 = left1_4321_43right_f32_f32(surface_variation, tmp212);
            float tmp214 = 0.319999992847442626953125;
            float tmp215 = left1_4321_43right_f32_f32(surface_detail, tmp214);
            float material_variation = left1_4331_43right_f32_f32(tmp213, tmp215);
            float tmp216 = 8.69999980926513671875;
            float tmp217 = left1_4321_43right_f32_f32(world_y, tmp216);
            float tmp218 = 0.054999999701976776123046875;
            float tmp219 = left1_4321_43right_f32_f32(world_x, tmp218);
            float tmp220 = left1_4331_43right_f32_f32(tmp217, tmp219);
            float tmp221 = 0.0309999994933605194091796875;
            float tmp222 = left1_4321_43right_f32_f32(world_z, tmp221);
            float tmp223 = 2.400000095367431640625;
            float tmp224 = left1_4321_43right_f32_f32(surface_variation, tmp223);
            float tmp225 = left1_4331_43right_f32_f32(tmp222, tmp224);
            float tmp226 = left1_4351_43right_f32_f32(tmp220, tmp225);
            float tmp227 = _the43_sine_of_value_f32(tmp226);
            float tmp228 = 0.5;
            float tmp229 = left1_4321_43right_f32_f32(tmp227, tmp228);
            float tmp230 = 0.5;
            float strata = left1_4331_43right_f32_f32(tmp229, tmp230);
            float tmp231 = 0.319999992847442626953125;
            float tmp232 = left1_4351_43right_f32_f32(surface_detail, tmp231);
            float tmp233 = 1.35000002384185791015625;
            float tmp234 = left1_4321_43right_f32_f32(tmp232, tmp233);
            float tmp235 = 1.7000000476837158203125;
            float tmp236 = left1_4321_43right_f32_f32(slope, tmp235);
            float tmp237 = left1_4331_43right_f32_f32(tmp234, tmp236);
            float fracture = saturate_number_f32(tmp237);
            float tmp238 = 0.054999999701976776123046875;
            float tmp239 = 0.300000011920928955078125;
            float tmp240 = 0.5;
            float tmp241 = left1_4351_43right_f32_f32(fracture, tmp240);
            float tmp242 = 0.0949999988079071044921875;
            float tmp243 = left1_4321_43right_f32_f32(tmp241, tmp242);
            float tmp244 = left1_4331_43right_f32_f32(slope, tmp243);
            float exposed_rock = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp238, tmp239, tmp244);
            float tmp245 = 2.75;
            float tmp246 = 4.44999980926513671875;
            float tmp247 = 0.5;
            float tmp248 = left1_4351_43right_f32_f32(material_variation, tmp247);
            float tmp249 = 0.4799999892711639404296875;
            float tmp250 = left1_4321_43right_f32_f32(tmp248, tmp249);
            float tmp251 = left1_4331_43right_f32_f32(world_y, tmp250);
            float snow = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp245, tmp246, tmp251);
            float tmp252 = 0.25;
            float tmp253 = 0.660000026226043701171875;
            float tmp254 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp252, tmp253, normal_y);
            snow = left1_4321_43right_f32_f32(snow, tmp254);
            float tmp255 = 0.87999999523162841796875;
            float tmp256 = 0.119999997317790985107421875;
            float tmp257 = left1_4321_43right_f32_f32(surface_detail, tmp256);
            float tmp258 = left1_4331_43right_f32_f32(tmp255, tmp257);
            snow = left1_4321_43right_f32_f32(snow, tmp258);
            float tmp259 = 0.3499999940395355224609375;
            float tmp260 = 2.650000095367431640625;
            float alpine_zone = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp259, tmp260, world_y);
            float tmp261 = 0.37999999523162841796875;
            float tmp262 = left1_4321_43right_f32_f32(normal_x, tmp261);
            float tmp263 = 0.2599999904632568359375;
            float tmp264 = left1_4321_43right_f32_f32(normal_z, tmp263);
            float tmp265 = left1_4351_43right_f32_f32(tmp262, tmp264);
            float tmp266 = 0.4799999892711639404296875;
            float tmp267 = left1_4331_43right_f32_f32(tmp265, tmp266);
            float rock_warmth = saturate_number_f32(tmp267);
            float tmp268 = 0.0280000008642673492431640625;
            float tmp269 = 0.0500000007450580596923828125;
            float tmp270 = left1_4321_43right_f32_f32(material_variation, tmp269);
            float tmp271 = left1_4331_43right_f32_f32(tmp268, tmp270);
            float tmp272 = 0.034000001847743988037109375;
            float tmp273 = left1_4321_43right_f32_f32(alpine_zone, tmp272);
            float meadow_red = left1_4331_43right_f32_f32(tmp271, tmp273);
            float tmp274 = 0.09399999678134918212890625;
            float tmp275 = 0.086000002920627593994140625;
            float tmp276 = left1_4321_43right_f32_f32(material_variation, tmp275);
            float tmp277 = left1_4331_43right_f32_f32(tmp274, tmp276);
            float tmp278 = 0.0500000007450580596923828125;
            float tmp279 = left1_4321_43right_f32_f32(alpine_zone, tmp278);
            float meadow_green = left1_4351_43right_f32_f32(tmp277, tmp279);
            float tmp280 = 0.0430000014603137969970703125;
            float tmp281 = 0.03599999845027923583984375;
            float tmp282 = left1_4321_43right_f32_f32(material_variation, tmp281);
            float tmp283 = left1_4331_43right_f32_f32(tmp280, tmp282);
            float tmp284 = 0.0199999995529651641845703125;
            float tmp285 = left1_4321_43right_f32_f32(alpine_zone, tmp284);
            float meadow_blue = left1_4331_43right_f32_f32(tmp283, tmp285);
            float tmp286 = 0.104999996721744537353515625;
            float tmp287 = 0.06599999964237213134765625;
            float tmp288 = left1_4321_43right_f32_f32(material_variation, tmp287);
            float tmp289 = left1_4331_43right_f32_f32(tmp286, tmp288);
            float tmp290 = 0.07400000095367431640625;
            float tmp291 = left1_4321_43right_f32_f32(rock_warmth, tmp290);
            float tmp292 = left1_4331_43right_f32_f32(tmp289, tmp291);
            float tmp293 = 0.0280000008642673492431640625;
            float tmp294 = left1_4321_43right_f32_f32(strata, tmp293);
            float rock_red = left1_4331_43right_f32_f32(tmp292, tmp294);
            float tmp295 = 0.101000003516674041748046875;
            float tmp296 = 0.0579999983310699462890625;
            float tmp297 = left1_4321_43right_f32_f32(material_variation, tmp296);
            float tmp298 = left1_4331_43right_f32_f32(tmp295, tmp297);
            float tmp299 = 0.046999998390674591064453125;
            float tmp300 = left1_4321_43right_f32_f32(rock_warmth, tmp299);
            float tmp301 = left1_4331_43right_f32_f32(tmp298, tmp300);
            float tmp302 = 0.02199999988079071044921875;
            float tmp303 = left1_4321_43right_f32_f32(strata, tmp302);
            float rock_green = left1_4331_43right_f32_f32(tmp301, tmp303);
            float tmp304 = 0.1089999973773956298828125;
            float tmp305 = 0.0540000014007091522216796875;
            float tmp306 = left1_4321_43right_f32_f32(material_variation, tmp305);
            float tmp307 = left1_4331_43right_f32_f32(tmp304, tmp306);
            float tmp308 = 0.037000000476837158203125;
            float tmp309 = left1_4321_43right_f32_f32(rock_warmth, tmp308);
            float tmp310 = left1_4331_43right_f32_f32(tmp307, tmp309);
            float tmp311 = 0.01899999938905239105224609375;
            float tmp312 = left1_4321_43right_f32_f32(strata, tmp311);
            float rock_blue = left1_4331_43right_f32_f32(tmp310, tmp312);
            float tmp313 = 1.0;
            float tmp314 = left1_4351_43right_f32_f32(tmp313, exposed_rock);
            float tmp315 = left1_4321_43right_f32_f32(meadow_red, tmp314);
            float tmp316 = left1_4321_43right_f32_f32(rock_red, exposed_rock);
            float ground_red = left1_4331_43right_f32_f32(tmp315, tmp316);
            float tmp317 = 1.0;
            float tmp318 = left1_4351_43right_f32_f32(tmp317, exposed_rock);
            float tmp319 = left1_4321_43right_f32_f32(meadow_green, tmp318);
            float tmp320 = left1_4321_43right_f32_f32(rock_green, exposed_rock);
            float ground_green = left1_4331_43right_f32_f32(tmp319, tmp320);
            float tmp321 = 1.0;
            float tmp322 = left1_4351_43right_f32_f32(tmp321, exposed_rock);
            float tmp323 = left1_4321_43right_f32_f32(meadow_blue, tmp322);
            float tmp324 = left1_4321_43right_f32_f32(rock_blue, exposed_rock);
            float ground_blue = left1_4331_43right_f32_f32(tmp323, tmp324);
            float tmp325 = 1.0;
            float tmp326 = left1_4351_43right_f32_f32(tmp325, snow);
            float tmp327 = left1_4321_43right_f32_f32(ground_red, tmp326);
            float tmp328 = 0.7799999713897705078125;
            float tmp329 = left1_4321_43right_f32_f32(tmp328, snow);
            ground_red = left1_4331_43right_f32_f32(tmp327, tmp329);
            float tmp330 = 1.0;
            float tmp331 = left1_4351_43right_f32_f32(tmp330, snow);
            float tmp332 = left1_4321_43right_f32_f32(ground_green, tmp331);
            float tmp333 = 0.829999983310699462890625;
            float tmp334 = left1_4321_43right_f32_f32(tmp333, snow);
            ground_green = left1_4331_43right_f32_f32(tmp332, tmp334);
            float tmp335 = 1.0;
            float tmp336 = left1_4351_43right_f32_f32(tmp335, snow);
            float tmp337 = left1_4321_43right_f32_f32(ground_blue, tmp336);
            float tmp338 = 0.87000000476837158203125;
            float tmp339 = left1_4321_43right_f32_f32(tmp338, snow);
            ground_blue = left1_4331_43right_f32_f32(tmp337, tmp339);
            float tmp340 = 0.800000011920928955078125;
            float tmp341 = 0.2599999904632568359375;
            float tmp342 = left1_4321_43right_f32_f32(surface_detail, tmp341);
            float material_light = left1_4331_43right_f32_f32(tmp340, tmp342);
            float tmp343 = 0.17000000178813934326171875;
            float tmp344 = 1.15999996662139892578125;
            float tmp345 = left1_4321_43right_f32_f32(sunlight, tmp344);
            float tmp346 = left1_4331_43right_f32_f32(tmp343, tmp345);
            float tmp347 = 0.17000000178813934326171875;
            float tmp348 = left1_4321_43right_f32_f32(sky_fill, tmp347);
            float tmp349 = left1_4331_43right_f32_f32(tmp346, tmp348);
            float lighting = left1_4321_43right_f32_f32(tmp349, material_light);
            float tmp350 = left1_4321_43right_f32_f32(ground_red, lighting);
            float tmp351 = 0.12999999523162841796875;
            float tmp352 = left1_4321_43right_f32_f32(sunlight, tmp351);
            ground_red = left1_4331_43right_f32_f32(tmp350, tmp352);
            float tmp353 = left1_4321_43right_f32_f32(ground_green, lighting);
            float tmp354 = 0.100000001490116119384765625;
            float tmp355 = left1_4321_43right_f32_f32(sunlight, tmp354);
            ground_green = left1_4331_43right_f32_f32(tmp353, tmp355);
            float tmp356 = left1_4321_43right_f32_f32(ground_blue, lighting);
            float tmp357 = 0.070000000298023223876953125;
            float tmp358 = left1_4321_43right_f32_f32(sunlight, tmp357);
            ground_blue = left1_4331_43right_f32_f32(tmp356, tmp358);
            float tmp359 = 47.0;
            float tmp360 = 94.0;
            float fog = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp359, tmp360, view_distance);
            float tmp361 = 0.17000000178813934326171875;
            float tmp362 = 0.17000000178813934326171875;
            float tmp363 = left1_4321_43right_f32_f32(sun_halo, tmp362);
            float fog_red = left1_4331_43right_f32_f32(tmp361, tmp363);
            float tmp364 = 0.23000000417232513427734375;
            float tmp365 = 0.104999996721744537353515625;
            float tmp366 = left1_4321_43right_f32_f32(sun_halo, tmp365);
            float fog_green = left1_4331_43right_f32_f32(tmp364, tmp366);
            float tmp367 = 0.310000002384185791015625;
            float tmp368 = 0.05200000107288360595703125;
            float tmp369 = left1_4321_43right_f32_f32(sun_halo, tmp368);
            float fog_blue = left1_4331_43right_f32_f32(tmp367, tmp369);
            float tmp370 = 1.0;
            float tmp371 = left1_4351_43right_f32_f32(tmp370, fog);
            float tmp372 = left1_4321_43right_f32_f32(ground_red, tmp371);
            float tmp373 = left1_4321_43right_f32_f32(fog_red, fog);
            red = left1_4331_43right_f32_f32(tmp372, tmp373);
            float tmp374 = 1.0;
            float tmp375 = left1_4351_43right_f32_f32(tmp374, fog);
            float tmp376 = left1_4321_43right_f32_f32(ground_green, tmp375);
            float tmp377 = left1_4321_43right_f32_f32(fog_green, fog);
            green = left1_4331_43right_f32_f32(tmp376, tmp377);
            float tmp378 = 1.0;
            float tmp379 = left1_4351_43right_f32_f32(tmp378, fog);
            float tmp380 = left1_4321_43right_f32_f32(ground_blue, tmp379);
            float tmp381 = left1_4321_43right_f32_f32(fog_blue, fog);
            blue = left1_4331_43right_f32_f32(tmp380, tmp381);
        }
    }
    float tmp382 = left1_4371_43right_f32_f32(screen_x, aspect);
    float tmp383 = left1_4371_43right_f32_f32(screen_x, aspect);
    float tmp384 = left1_4321_43right_f32_f32(tmp382, tmp383);
    float tmp385 = left1_4321_43right_f32_f32(screen_y, screen_y);
    float tmp386 = left1_4331_43right_f32_f32(tmp384, tmp385);
    float vignette_radius = _the43_square_root_of_value_f32(tmp386);
    float tmp387 = 0.37999999523162841796875;
    float tmp388 = 1.0;
    float tmp389 = 0.579999983310699462890625;
    float tmp390 = 1.34000003337860107421875;
    float tmp391 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp389, tmp390, vignette_radius);
    float tmp392 = left1_4351_43right_f32_f32(tmp388, tmp391);
    float tmp393 = 0.62000000476837158203125;
    float tmp394 = left1_4321_43right_f32_f32(tmp392, tmp393);
    float vignette = left1_4331_43right_f32_f32(tmp387, tmp394);
    float tmp395 = left1_4321_43right_f32_f32(red, vignette);
    float tmp396 = 1.0;
    float tmp397 = 0.37999999523162841796875;
    float tmp398 = left1_4321_43right_f32_f32(red, tmp397);
    float tmp399 = left1_4331_43right_f32_f32(tmp396, tmp398);
    float tmp400 = left1_4371_43right_f32_f32(tmp395, tmp399);
    red = _the43_square_root_of_value_f32(tmp400);
    float tmp401 = left1_4321_43right_f32_f32(green, vignette);
    float tmp402 = 1.0;
    float tmp403 = 0.37999999523162841796875;
    float tmp404 = left1_4321_43right_f32_f32(green, tmp403);
    float tmp405 = left1_4331_43right_f32_f32(tmp402, tmp404);
    float tmp406 = left1_4371_43right_f32_f32(tmp401, tmp405);
    green = _the43_square_root_of_value_f32(tmp406);
    float tmp407 = left1_4321_43right_f32_f32(blue, vignette);
    float tmp408 = 1.0;
    float tmp409 = 0.37999999523162841796875;
    float tmp410 = left1_4321_43right_f32_f32(blue, tmp409);
    float tmp411 = left1_4331_43right_f32_f32(tmp408, tmp410);
    float tmp412 = left1_4371_43right_f32_f32(tmp407, tmp411);
    blue = _the43_square_root_of_value_f32(tmp412);
    vec4 _957 = vec4(0.0, 0.0, 0.0, 1.0);
    _957.z = blue;
    _957.y = green;
    _957.x = red;
    dynlexColor = _957;
}
