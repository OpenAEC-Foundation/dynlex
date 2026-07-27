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

float _the43_cosine_of_value_f32(float value)
{
    return cos(value);
}

float water_detail_visibility_at_distance_f32(float _distance)
{
    float tmp = 1.0;
    float tmp1 = 48.0;
    float tmp2 = 96.0;
    float tmp3 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp1, tmp2, _distance);
    return left1_4351_43right_f32_f32(tmp, tmp3);
}

float _the43_sine_of_value_f32(float value)
{
    return sin(value);
}

float terrain_camera_x_at_moment_f32(float moment)
{
    float tmp = 201.6999969482421875;
    float tmp1 = 0.090999998152256011962890625;
    float tmp2 = left1_4321_43right_f32_f32(moment, tmp1);
    float tmp3 = _the43_sine_of_value_f32(tmp2);
    float tmp4 = 4.19999980926513671875;
    float tmp5 = left1_4321_43right_f32_f32(tmp3, tmp4);
    float tmp6 = 0.037000000476837158203125;
    float tmp7 = left1_4321_43right_f32_f32(moment, tmp6);
    float tmp8 = _the43_cosine_of_value_f32(tmp7);
    float tmp9 = 1.60000002384185791015625;
    float tmp10 = left1_4321_43right_f32_f32(tmp8, tmp9);
    float tmp11 = left1_4331_43right_f32_f32(tmp5, tmp10);
    return left1_4331_43right_f32_f32(tmp, tmp11);
}

float terrain_maximum_possible_height()
{
    return 12.1000003814697265625;
}

float terrain_camera_altitude()
{
    float tmp = terrain_maximum_possible_height();
    float tmp1 = 0.699999988079071044921875;
    return left1_4331_43right_f32_f32(tmp, tmp1);
}

float terrain_camera_z_at_moment_f32(float moment)
{
    float tmp = 92.09999847412109375;
    float tmp1 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp);
    float tmp2 = 3.25;
    float tmp3 = left1_4321_43right_f32_f32(moment, tmp2);
    return left1_4331_43right_f32_f32(tmp1, tmp3);
}

float _the43_absolute_value_of_magnitude_f32(float magnitude)
{
    return abs(magnitude);
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
    float ray_distance = dynlex_interpolant_7465727261696e5f6e6f726d616c.w;
    float surface_variation = dynlex_interpolant_7465727261696e5f6d6174657269616c.x;
    float surface_detail = dynlex_interpolant_7465727261696e5f6d6174657269616c.y;
    float water_depth = dynlex_interpolant_7465727261696e5f6d6174657269616c.z;
    float tmp34 = 0.519999980926513671875;
    float sun_x = left1_4351_43right_f32_f32(screen_x, tmp34);
    float tmp35 = 0.36000001430511474609375;
    float sun_y = left1_4351_43right_f32_f32(screen_y, tmp35);
    float tmp36 = left1_4321_43right_f32_f32(sun_x, sun_x);
    float tmp37 = left1_4321_43right_f32_f32(sun_y, sun_y);
    float tmp38 = left1_4331_43right_f32_f32(tmp36, tmp37);
    float sun_distance = _the43_square_root_of_value_f32(tmp38);
    float tmp39 = 0.01400000043213367462158203125;
    float tmp40 = 0.064999997615814208984375;
    float sun_core = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp39, tmp40, sun_distance);
    float tmp41 = 0.039999999105930328369140625;
    float tmp42 = 0.439999997615814208984375;
    float sun_halo = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp41, tmp42, sun_distance);
    float red = 0.02500000037252902984619140625;
    float green = 0.064000003039836883544921875;
    float blue = 0.12999999523162841796875;
    float tmp43 = 0.5;
    if (left_0_right_f32_f32(surface_kind, tmp43))
    {
        float tmp44 = 0.4799999892711639404296875;
        float tmp45 = left1_4321_43right_f32_f32(screen_y, tmp44);
        float tmp46 = 0.430000007152557373046875;
        float tmp47 = left1_4331_43right_f32_f32(tmp45, tmp46);
        float sky_height = saturate_number_f32(tmp47);
        float tmp48 = 0.02199999988079071044921875;
        float tmp49 = 0.104999996721744537353515625;
        float tmp50 = left1_4321_43right_f32_f32(sky_height, tmp49);
        red = left1_4331_43right_f32_f32(tmp48, tmp50);
        float tmp51 = 0.0610000006854534149169921875;
        float tmp52 = 0.180000007152557373046875;
        float tmp53 = left1_4321_43right_f32_f32(sky_height, tmp52);
        green = left1_4331_43right_f32_f32(tmp51, tmp53);
        float tmp54 = 0.14000000059604644775390625;
        float tmp55 = 0.3300000131130218505859375;
        float tmp56 = left1_4321_43right_f32_f32(sky_height, tmp55);
        blue = left1_4331_43right_f32_f32(tmp54, tmp56);
        float tmp57 = 1.21000003814697265625;
        float tmp58 = left1_4321_43right_f32_f32(screen_x, tmp57);
        float tmp59 = 1.769999980926513671875;
        float tmp60 = left1_4321_43right_f32_f32(screen_y, tmp59);
        float tmp61 = 0.02099999971687793731689453125;
        float tmp62 = left1_4321_43right_f32_f32(time, tmp61);
        float tmp63 = left1_4331_43right_f32_f32(tmp60, tmp62);
        float tmp64 = left1_4331_43right_f32_f32(tmp58, tmp63);
        float tmp65 = _the43_sine_of_value_f32(tmp64);
        float tmp66 = 0.5;
        float tmp67 = left1_4321_43right_f32_f32(tmp65, tmp66);
        float tmp68 = 0.5;
        float cloud_fold = left1_4331_43right_f32_f32(tmp67, tmp68);
        float tmp69 = 0.670000016689300537109375;
        float tmp70 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp69);
        float tmp71 = left1_4321_43right_f32_f32(screen_x, tmp70);
        float tmp72 = 2.9300000667572021484375;
        float tmp73 = left1_4321_43right_f32_f32(screen_y, tmp72);
        float tmp74 = 0.01600000075995922088623046875;
        float tmp75 = left1_4321_43right_f32_f32(time, tmp74);
        float tmp76 = left1_4351_43right_f32_f32(tmp73, tmp75);
        float tmp77 = left1_4331_43right_f32_f32(tmp71, tmp76);
        float tmp78 = _the43_cosine_of_value_f32(tmp77);
        float tmp79 = 0.5;
        float tmp80 = left1_4321_43right_f32_f32(tmp78, tmp79);
        float tmp81 = 0.5;
        float cloud_crossing = left1_4331_43right_f32_f32(tmp80, tmp81);
        float tmp82 = 2.4700000286102294921875;
        float tmp83 = left1_4321_43right_f32_f32(screen_x, tmp82);
        float tmp84 = 4.110000133514404296875;
        float tmp85 = left1_4321_43right_f32_f32(screen_y, tmp84);
        float tmp86 = 1.7999999523162841796875;
        float tmp87 = left1_4321_43right_f32_f32(cloud_fold, tmp86);
        float tmp88 = left1_4331_43right_f32_f32(tmp85, tmp87);
        float tmp89 = left1_4351_43right_f32_f32(tmp83, tmp88);
        float tmp90 = _the43_sine_of_value_f32(tmp89);
        float tmp91 = 0.5;
        float tmp92 = left1_4321_43right_f32_f32(tmp90, tmp91);
        float tmp93 = 0.5;
        float cloud_detail = left1_4331_43right_f32_f32(tmp92, tmp93);
        float tmp94 = 1.41999995708465576171875;
        float tmp95 = 2.1800000667572021484375;
        float tmp96 = left1_4331_43right_f32_f32(cloud_fold, cloud_crossing);
        float tmp97 = 0.4600000083446502685546875;
        float tmp98 = left1_4321_43right_f32_f32(cloud_detail, tmp97);
        float tmp99 = left1_4331_43right_f32_f32(tmp96, tmp98);
        float cloud_shape = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp94, tmp95, tmp99);
        float tmp100 = 0.07999999821186065673828125;
        float tmp101 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp100);
        float tmp102 = 0.37999999523162841796875;
        float cloud_altitude = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp101, tmp102, screen_y);
        float cloud_light = left1_4321_43right_f32_f32(cloud_shape, cloud_altitude);
        float tmp103 = 0.3300000131130218505859375;
        float tmp104 = left1_4321_43right_f32_f32(cloud_light, tmp103);
        red = left1_4331_43right_f32_f32(red, tmp104);
        float tmp105 = 0.310000002384185791015625;
        float tmp106 = left1_4321_43right_f32_f32(cloud_light, tmp105);
        green = left1_4331_43right_f32_f32(green, tmp106);
        float tmp107 = 0.2899999916553497314453125;
        float tmp108 = left1_4321_43right_f32_f32(cloud_light, tmp107);
        blue = left1_4331_43right_f32_f32(blue, tmp108);
        float tmp109 = 0.310000002384185791015625;
        float tmp110 = left1_4321_43right_f32_f32(sun_halo, tmp109);
        float tmp111 = left1_4331_43right_f32_f32(red, tmp110);
        float tmp112 = 1.36000001430511474609375;
        float tmp113 = left1_4321_43right_f32_f32(sun_core, tmp112);
        red = left1_4331_43right_f32_f32(tmp111, tmp113);
        float tmp114 = 0.189999997615814208984375;
        float tmp115 = left1_4321_43right_f32_f32(sun_halo, tmp114);
        float tmp116 = left1_4331_43right_f32_f32(green, tmp115);
        float tmp117 = 0.959999978542327880859375;
        float tmp118 = left1_4321_43right_f32_f32(sun_core, tmp117);
        green = left1_4331_43right_f32_f32(tmp116, tmp118);
        float tmp119 = 0.0900000035762786865234375;
        float tmp120 = left1_4321_43right_f32_f32(sun_halo, tmp119);
        float tmp121 = left1_4331_43right_f32_f32(blue, tmp120);
        float tmp122 = 0.550000011920928955078125;
        float tmp123 = left1_4321_43right_f32_f32(sun_core, tmp122);
        blue = left1_4331_43right_f32_f32(tmp121, tmp123);
    }
    else
    {
        float tmp124 = left1_4321_43right_f32_f32(normal_x, normal_x);
        float tmp125 = left1_4321_43right_f32_f32(normal_y, normal_y);
        float tmp126 = left1_4331_43right_f32_f32(tmp124, tmp125);
        float tmp127 = left1_4321_43right_f32_f32(normal_z, normal_z);
        float tmp128 = left1_4331_43right_f32_f32(tmp126, tmp127);
        float normal_length = _the43_square_root_of_value_f32(tmp128);
        normal_x = left1_4371_43right_f32_f32(normal_x, normal_length);
        normal_y = left1_4371_43right_f32_f32(normal_y, normal_length);
        normal_z = left1_4371_43right_f32_f32(normal_z, normal_length);
        float tmp129 = 0.430000007152557373046875;
        float tmp130 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp129);
        float tmp131 = left1_4321_43right_f32_f32(normal_x, tmp130);
        float tmp132 = 0.790000021457672119140625;
        float tmp133 = left1_4321_43right_f32_f32(normal_y, tmp132);
        float tmp134 = left1_4331_43right_f32_f32(tmp131, tmp133);
        float tmp135 = 0.439999997615814208984375;
        float tmp136 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp135);
        float tmp137 = left1_4321_43right_f32_f32(normal_z, tmp136);
        float tmp138 = left1_4331_43right_f32_f32(tmp134, tmp137);
        float sunlight = saturate_number_f32(tmp138);
        float tmp139 = 0.560000002384185791015625;
        float tmp140 = left1_4321_43right_f32_f32(normal_y, tmp139);
        float tmp141 = 0.439999997615814208984375;
        float tmp142 = left1_4331_43right_f32_f32(tmp140, tmp141);
        float sky_fill = saturate_number_f32(tmp142);
        float tmp145 = 1.5;
        if (left_2_right_f32_f32(surface_kind, tmp145))
        {
            float tmp146 = 1.37000000476837158203125;
            float tmp147 = left1_4321_43right_f32_f32(world_x, tmp146);
            float tmp148 = 0.910000026226043701171875;
            float tmp149 = left1_4321_43right_f32_f32(world_z, tmp148);
            float tmp150 = left1_4331_43right_f32_f32(tmp147, tmp149);
            float tmp151 = 1.12000000476837158203125;
            float tmp152 = left1_4321_43right_f32_f32(time, tmp151);
            float ripple_angle_one = left1_4331_43right_f32_f32(tmp150, tmp152);
            float tmp153 = 0.829999983310699462890625;
            float tmp154 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp153);
            float tmp155 = left1_4321_43right_f32_f32(world_x, tmp154);
            float tmp156 = 1.61000001430511474609375;
            float tmp157 = left1_4321_43right_f32_f32(world_z, tmp156);
            float tmp158 = left1_4331_43right_f32_f32(tmp155, tmp157);
            float tmp159 = 0.939999997615814208984375;
            float tmp160 = left1_4321_43right_f32_f32(time, tmp159);
            float ripple_angle_two = left1_4351_43right_f32_f32(tmp158, tmp160);
            float tmp161 = 2.1099998950958251953125;
            float tmp162 = left1_4321_43right_f32_f32(world_x, tmp161);
            float tmp163 = 0.569999992847442626953125;
            float tmp164 = left1_4321_43right_f32_f32(world_z, tmp163);
            float tmp165 = left1_4351_43right_f32_f32(tmp162, tmp164);
            float tmp166 = 0.709999978542327880859375;
            float tmp167 = left1_4321_43right_f32_f32(time, tmp166);
            float ripple_angle_three = left1_4331_43right_f32_f32(tmp165, tmp167);
            float ripple_cosine_one = _the43_cosine_of_value_f32(ripple_angle_one);
            float ripple_cosine_two = _the43_cosine_of_value_f32(ripple_angle_two);
            float ripple_cosine_three = _the43_cosine_of_value_f32(ripple_angle_three);
            float water_ripple_visibility = water_detail_visibility_at_distance_f32(ray_distance);
            float tmp168 = 0.0709500014781951904296875;
            float tmp169 = left1_4321_43right_f32_f32(ripple_cosine_one, tmp168);
            float tmp170 = 0.0283499993383884429931640625;
            float tmp171 = left1_4321_43right_f32_f32(ripple_cosine_two, tmp170);
            float tmp172 = left1_4351_43right_f32_f32(tmp169, tmp171);
            float tmp173 = 0.0319500006735324859619140625;
            float tmp174 = left1_4321_43right_f32_f32(ripple_cosine_three, tmp173);
            float tmp175 = left1_4331_43right_f32_f32(tmp172, tmp174);
            float ripple_normal_x = left1_4321_43right_f32_f32(tmp175, water_ripple_visibility);
            float tmp176 = 0.051150001585483551025390625;
            float tmp177 = left1_4321_43right_f32_f32(ripple_cosine_one, tmp176);
            float tmp178 = 0.0535500012338161468505859375;
            float tmp179 = left1_4321_43right_f32_f32(ripple_cosine_two, tmp178);
            float tmp180 = left1_4331_43right_f32_f32(tmp177, tmp179);
            float tmp181 = 0.008550000376999378204345703125;
            float tmp182 = left1_4321_43right_f32_f32(ripple_cosine_three, tmp181);
            float tmp183 = left1_4351_43right_f32_f32(tmp180, tmp182);
            float ripple_normal_z = left1_4321_43right_f32_f32(tmp183, water_ripple_visibility);
            normal_x = left1_4351_43right_f32_f32(normal_x, ripple_normal_x);
            normal_z = left1_4351_43right_f32_f32(normal_z, ripple_normal_z);
            float tmp184 = left1_4321_43right_f32_f32(normal_x, normal_x);
            float tmp185 = left1_4321_43right_f32_f32(normal_y, normal_y);
            float tmp186 = left1_4331_43right_f32_f32(tmp184, tmp185);
            float tmp187 = left1_4321_43right_f32_f32(normal_z, normal_z);
            float tmp188 = left1_4331_43right_f32_f32(tmp186, tmp187);
            float water_normal_length = _the43_square_root_of_value_f32(tmp188);
            normal_x = left1_4371_43right_f32_f32(normal_x, water_normal_length);
            normal_y = left1_4371_43right_f32_f32(normal_y, water_normal_length);
            normal_z = left1_4371_43right_f32_f32(normal_z, water_normal_length);
            float camera_x = terrain_camera_x_at_moment_f32(time);
            float camera_y = terrain_camera_altitude();
            float camera_z = terrain_camera_z_at_moment_f32(time);
            float water_view_x = left1_4351_43right_f32_f32(camera_x, world_x);
            float water_view_y = left1_4351_43right_f32_f32(camera_y, world_y);
            float water_view_z = left1_4351_43right_f32_f32(camera_z, world_z);
            float tmp189 = left1_4321_43right_f32_f32(water_view_x, water_view_x);
            float tmp190 = left1_4321_43right_f32_f32(water_view_y, water_view_y);
            float tmp191 = left1_4331_43right_f32_f32(tmp189, tmp190);
            float tmp192 = left1_4321_43right_f32_f32(water_view_z, water_view_z);
            float tmp193 = left1_4331_43right_f32_f32(tmp191, tmp192);
            float water_view_length = _the43_square_root_of_value_f32(tmp193);
            water_view_x = left1_4371_43right_f32_f32(water_view_x, water_view_length);
            water_view_y = left1_4371_43right_f32_f32(water_view_y, water_view_length);
            water_view_z = left1_4371_43right_f32_f32(water_view_z, water_view_length);
            float tmp194 = left1_4321_43right_f32_f32(normal_x, water_view_x);
            float tmp195 = left1_4321_43right_f32_f32(normal_y, water_view_y);
            float tmp196 = left1_4331_43right_f32_f32(tmp194, tmp195);
            float tmp197 = left1_4321_43right_f32_f32(normal_z, water_view_z);
            float tmp198 = left1_4331_43right_f32_f32(tmp196, tmp197);
            float water_view_facing = saturate_number_f32(tmp198);
            float tmp199 = 1.0;
            float fresnel_grazing = left1_4351_43right_f32_f32(tmp199, water_view_facing);
            float fresnel_squared = left1_4321_43right_f32_f32(fresnel_grazing, fresnel_grazing);
            float fresnel_fourth = left1_4321_43right_f32_f32(fresnel_squared, fresnel_squared);
            float tmp200 = 0.0199999995529651641845703125;
            float tmp201 = left1_4321_43right_f32_f32(fresnel_fourth, fresnel_grazing);
            float tmp202 = 0.980000019073486328125;
            float tmp203 = left1_4321_43right_f32_f32(tmp201, tmp202);
            float water_fresnel = left1_4331_43right_f32_f32(tmp200, tmp203);
            float tmp204 = left1_4321_43right_f32_f32(normal_x, water_view_facing);
            float tmp205 = 2.0;
            float tmp206 = left1_4321_43right_f32_f32(tmp204, tmp205);
            float reflected_x = left1_4351_43right_f32_f32(tmp206, water_view_x);
            float tmp207 = left1_4321_43right_f32_f32(normal_y, water_view_facing);
            float tmp208 = 2.0;
            float tmp209 = left1_4321_43right_f32_f32(tmp207, tmp208);
            float reflected_y = left1_4351_43right_f32_f32(tmp209, water_view_y);
            float tmp210 = left1_4321_43right_f32_f32(normal_z, water_view_facing);
            float tmp211 = 2.0;
            float tmp212 = left1_4321_43right_f32_f32(tmp210, tmp211);
            float reflected_z = left1_4351_43right_f32_f32(tmp212, water_view_z);
            float tmp213 = 0.7200000286102294921875;
            float tmp214 = left1_4321_43right_f32_f32(reflected_y, tmp213);
            float tmp215 = 0.2800000011920928955078125;
            float tmp216 = left1_4331_43right_f32_f32(tmp214, tmp215);
            float reflected_sky_height = saturate_number_f32(tmp216);
            float tmp217 = 0.1599999964237213134765625;
            float tmp218 = 1.0;
            float tmp219 = left1_4351_43right_f32_f32(tmp218, reflected_sky_height);
            float tmp220 = left1_4321_43right_f32_f32(tmp217, tmp219);
            float tmp221 = 0.02500000037252902984619140625;
            float tmp222 = left1_4321_43right_f32_f32(tmp221, reflected_sky_height);
            float reflection_red = left1_4331_43right_f32_f32(tmp220, tmp222);
            float tmp223 = 0.23999999463558197021484375;
            float tmp224 = 1.0;
            float tmp225 = left1_4351_43right_f32_f32(tmp224, reflected_sky_height);
            float tmp226 = left1_4321_43right_f32_f32(tmp223, tmp225);
            float tmp227 = 0.0949999988079071044921875;
            float tmp228 = left1_4321_43right_f32_f32(tmp227, reflected_sky_height);
            float reflection_green = left1_4331_43right_f32_f32(tmp226, tmp228);
            float tmp229 = 0.319999992847442626953125;
            float tmp230 = 1.0;
            float tmp231 = left1_4351_43right_f32_f32(tmp230, reflected_sky_height);
            float tmp232 = left1_4321_43right_f32_f32(tmp229, tmp231);
            float tmp233 = 0.23999999463558197021484375;
            float tmp234 = left1_4321_43right_f32_f32(tmp233, reflected_sky_height);
            float reflection_blue = left1_4331_43right_f32_f32(tmp232, tmp234);
            float tmp235 = 2.099999904632568359375;
            float tmp236 = left1_4321_43right_f32_f32(reflected_x, tmp235);
            float tmp237 = 1.2999999523162841796875;
            float tmp238 = left1_4321_43right_f32_f32(reflected_z, tmp237);
            float tmp239 = left1_4331_43right_f32_f32(tmp236, tmp238);
            float tmp240 = 0.017999999225139617919921875;
            float tmp241 = left1_4321_43right_f32_f32(time, tmp240);
            float tmp242 = left1_4331_43right_f32_f32(tmp239, tmp241);
            float tmp243 = _the43_sine_of_value_f32(tmp242);
            float tmp244 = 0.5;
            float tmp245 = left1_4321_43right_f32_f32(tmp243, tmp244);
            float tmp246 = 0.5;
            float reflected_cloud_one = left1_4331_43right_f32_f32(tmp245, tmp246);
            float tmp247 = 1.39999997615814208984375;
            float tmp248 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp247);
            float tmp249 = left1_4321_43right_f32_f32(reflected_x, tmp248);
            float tmp250 = 2.7000000476837158203125;
            float tmp251 = left1_4321_43right_f32_f32(reflected_z, tmp250);
            float tmp252 = left1_4331_43right_f32_f32(tmp249, tmp251);
            float tmp253 = 0.0130000002682209014892578125;
            float tmp254 = left1_4321_43right_f32_f32(time, tmp253);
            float tmp255 = left1_4351_43right_f32_f32(tmp252, tmp254);
            float tmp256 = _the43_cosine_of_value_f32(tmp255);
            float tmp257 = 0.5;
            float tmp258 = left1_4321_43right_f32_f32(tmp256, tmp257);
            float tmp259 = 0.5;
            float reflected_cloud_two = left1_4331_43right_f32_f32(tmp258, tmp259);
            float tmp260 = 1.25;
            float tmp261 = 1.7200000286102294921875;
            float tmp262 = left1_4331_43right_f32_f32(reflected_cloud_one, reflected_cloud_two);
            float reflected_cloud = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp260, tmp261, tmp262);
            float tmp263 = 0.0280000008642673492431640625;
            float tmp264 = left1_4321_43right_f32_f32(reflected_cloud, tmp263);
            reflection_red = left1_4331_43right_f32_f32(reflection_red, tmp264);
            float tmp265 = 0.03599999845027923583984375;
            float tmp266 = left1_4321_43right_f32_f32(reflected_cloud, tmp265);
            reflection_green = left1_4331_43right_f32_f32(reflection_green, tmp266);
            float tmp267 = 0.04500000178813934326171875;
            float tmp268 = left1_4321_43right_f32_f32(reflected_cloud, tmp267);
            reflection_blue = left1_4331_43right_f32_f32(reflection_blue, tmp268);
            float tmp269 = 0.38999998569488525390625;
            float tmp270 = left1_4321_43right_f32_f32(reflected_x, tmp269);
            float tmp271 = 0.319999992847442626953125;
            float tmp272 = left1_4321_43right_f32_f32(reflected_y, tmp271);
            float tmp273 = left1_4331_43right_f32_f32(tmp270, tmp272);
            float tmp274 = 0.86000001430511474609375;
            float tmp275 = left1_4321_43right_f32_f32(reflected_z, tmp274);
            float tmp276 = left1_4331_43right_f32_f32(tmp273, tmp275);
            float water_sun_glint = saturate_number_f32(tmp276);
            water_sun_glint = left1_4321_43right_f32_f32(water_sun_glint, water_sun_glint);
            water_sun_glint = left1_4321_43right_f32_f32(water_sun_glint, water_sun_glint);
            water_sun_glint = left1_4321_43right_f32_f32(water_sun_glint, water_sun_glint);
            water_sun_glint = left1_4321_43right_f32_f32(water_sun_glint, water_sun_glint);
            water_sun_glint = left1_4321_43right_f32_f32(water_sun_glint, water_sun_glint);
            float water_sun_glow = water_sun_glint;
            water_sun_glint = left1_4321_43right_f32_f32(water_sun_glint, water_sun_glint);
            float tmp277 = 0.3499999940395355224609375;
            float tmp278 = 0.64999997615814208984375;
            float tmp279 = left1_4321_43right_f32_f32(water_fresnel, tmp278);
            float tmp280 = left1_4331_43right_f32_f32(tmp277, tmp279);
            water_sun_glint = left1_4321_43right_f32_f32(water_sun_glint, tmp280);
            float tmp281 = 1.0;
            float tmp282 = 0.180000007152557373046875;
            float tmp283 = 3.7999999523162841796875;
            float tmp284 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp282, tmp283, water_depth);
            float tmp285 = left1_4351_43right_f32_f32(tmp281, tmp284);
            float shallow_water = left1_4321_43right_f32_f32(tmp285, water_ripple_visibility);
            float tmp286 = 24.0;
            float tmp287 = 220.0;
            float water_absorption = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp286, tmp287, ray_distance);
            float tmp288 = 0.0040000001899898052215576171875;
            float tmp289 = 0.008000000379979610443115234375;
            float tmp290 = left1_4321_43right_f32_f32(surface_variation, tmp289);
            float tmp291 = left1_4331_43right_f32_f32(tmp288, tmp290);
            float tmp292 = 1.0;
            float tmp293 = 0.4199999868869781494140625;
            float tmp294 = left1_4321_43right_f32_f32(water_absorption, tmp293);
            float tmp295 = left1_4351_43right_f32_f32(tmp292, tmp294);
            float water_body_red = left1_4321_43right_f32_f32(tmp291, tmp295);
            float tmp296 = 0.017999999225139617919921875;
            float tmp297 = 0.0379999987781047821044921875;
            float tmp298 = left1_4321_43right_f32_f32(surface_variation, tmp297);
            float tmp299 = left1_4331_43right_f32_f32(tmp296, tmp298);
            float tmp300 = 1.0;
            float tmp301 = 0.2800000011920928955078125;
            float tmp302 = left1_4321_43right_f32_f32(water_absorption, tmp301);
            float tmp303 = left1_4351_43right_f32_f32(tmp300, tmp302);
            float water_body_green = left1_4321_43right_f32_f32(tmp299, tmp303);
            float tmp304 = 0.0419999994337558746337890625;
            float tmp305 = 0.0579999983310699462890625;
            float tmp306 = left1_4321_43right_f32_f32(surface_variation, tmp305);
            float tmp307 = left1_4331_43right_f32_f32(tmp304, tmp306);
            float tmp308 = 1.0;
            float tmp309 = 0.119999997317790985107421875;
            float tmp310 = left1_4321_43right_f32_f32(water_absorption, tmp309);
            float tmp311 = left1_4351_43right_f32_f32(tmp308, tmp310);
            float water_body_blue = left1_4321_43right_f32_f32(tmp307, tmp311);
            float tmp312 = 1.0;
            float tmp313 = left1_4351_43right_f32_f32(tmp312, shallow_water);
            float tmp314 = left1_4321_43right_f32_f32(water_body_red, tmp313);
            float tmp315 = 0.017999999225139617919921875;
            float tmp316 = 0.01200000010430812835693359375;
            float tmp317 = left1_4321_43right_f32_f32(surface_detail, tmp316);
            float tmp318 = left1_4331_43right_f32_f32(tmp315, tmp317);
            float tmp319 = left1_4321_43right_f32_f32(tmp318, shallow_water);
            water_body_red = left1_4331_43right_f32_f32(tmp314, tmp319);
            float tmp320 = 1.0;
            float tmp321 = left1_4351_43right_f32_f32(tmp320, shallow_water);
            float tmp322 = left1_4321_43right_f32_f32(water_body_green, tmp321);
            float tmp323 = 0.104999996721744537353515625;
            float tmp324 = 0.0500000007450580596923828125;
            float tmp325 = left1_4321_43right_f32_f32(surface_detail, tmp324);
            float tmp326 = left1_4331_43right_f32_f32(tmp323, tmp325);
            float tmp327 = left1_4321_43right_f32_f32(tmp326, shallow_water);
            water_body_green = left1_4331_43right_f32_f32(tmp322, tmp327);
            float tmp328 = 1.0;
            float tmp329 = left1_4351_43right_f32_f32(tmp328, shallow_water);
            float tmp330 = left1_4321_43right_f32_f32(water_body_blue, tmp329);
            float tmp331 = 0.125;
            float tmp332 = 0.054999999701976776123046875;
            float tmp333 = left1_4321_43right_f32_f32(surface_detail, tmp332);
            float tmp334 = left1_4331_43right_f32_f32(tmp331, tmp333);
            float tmp335 = left1_4321_43right_f32_f32(tmp334, shallow_water);
            water_body_blue = left1_4331_43right_f32_f32(tmp330, tmp335);
            float tmp336 = 1.0;
            float tmp337 = 0.0500000007450580596923828125;
            float tmp338 = 0.64999997615814208984375;
            float tmp339 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp337, tmp338, water_depth);
            float tmp340 = left1_4351_43right_f32_f32(tmp336, tmp339);
            float water_caustic_depth = left1_4321_43right_f32_f32(tmp340, water_ripple_visibility);
            float tmp341 = 0.7200000286102294921875;
            float tmp342 = left1_4321_43right_f32_f32(ripple_cosine_two, tmp341);
            float tmp343 = left1_4331_43right_f32_f32(ripple_cosine_one, tmp342);
            float tmp344 = 0.2800000011920928955078125;
            float tmp345 = left1_4321_43right_f32_f32(ripple_cosine_three, tmp344);
            float tmp346 = left1_4331_43right_f32_f32(tmp343, tmp345);
            float water_caustic_distance = _the43_absolute_value_of_magnitude_f32(tmp346);
            float tmp347 = 0.0;
            float tmp348 = 0.180000007152557373046875;
            float tmp349 = glow_from_inner_to_outer_at_sample_f32_f32_f32(tmp347, tmp348, water_caustic_distance);
            float water_caustic = left1_4321_43right_f32_f32(tmp349, water_caustic_depth);
            float tmp350 = 0.006000000052154064178466796875;
            float tmp351 = left1_4321_43right_f32_f32(water_caustic, tmp350);
            water_body_red = left1_4331_43right_f32_f32(water_body_red, tmp351);
            float tmp352 = 0.0320000015199184417724609375;
            float tmp353 = left1_4321_43right_f32_f32(water_caustic, tmp352);
            water_body_green = left1_4331_43right_f32_f32(water_body_green, tmp353);
            float tmp354 = 0.039999999105930328369140625;
            float tmp355 = left1_4321_43right_f32_f32(water_caustic, tmp354);
            water_body_blue = left1_4331_43right_f32_f32(water_body_blue, tmp355);
            float tmp356 = 1.0;
            float tmp357 = left1_4351_43right_f32_f32(tmp356, water_fresnel);
            float tmp358 = left1_4321_43right_f32_f32(water_body_red, tmp357);
            float tmp359 = left1_4321_43right_f32_f32(reflection_red, water_fresnel);
            float water_red = left1_4331_43right_f32_f32(tmp358, tmp359);
            float tmp360 = 1.0;
            float tmp361 = left1_4351_43right_f32_f32(tmp360, water_fresnel);
            float tmp362 = left1_4321_43right_f32_f32(water_body_green, tmp361);
            float tmp363 = left1_4321_43right_f32_f32(reflection_green, water_fresnel);
            float water_green = left1_4331_43right_f32_f32(tmp362, tmp363);
            float tmp364 = 1.0;
            float tmp365 = left1_4351_43right_f32_f32(tmp364, water_fresnel);
            float tmp366 = left1_4321_43right_f32_f32(water_body_blue, tmp365);
            float tmp367 = left1_4321_43right_f32_f32(reflection_blue, water_fresnel);
            float water_blue = left1_4331_43right_f32_f32(tmp366, tmp367);
            float tmp368 = 0.07999999821186065673828125;
            float tmp369 = left1_4321_43right_f32_f32(water_sun_glow, tmp368);
            water_red = left1_4331_43right_f32_f32(water_red, tmp369);
            float tmp370 = 0.04500000178813934326171875;
            float tmp371 = left1_4321_43right_f32_f32(water_sun_glow, tmp370);
            water_green = left1_4331_43right_f32_f32(water_green, tmp371);
            float tmp372 = 0.017999999225139617919921875;
            float tmp373 = left1_4321_43right_f32_f32(water_sun_glow, tmp372);
            water_blue = left1_4331_43right_f32_f32(water_blue, tmp373);
            float tmp374 = 0.699999988079071044921875;
            float tmp375 = left1_4321_43right_f32_f32(water_sun_glint, tmp374);
            water_red = left1_4331_43right_f32_f32(water_red, tmp375);
            float tmp376 = 0.4199999868869781494140625;
            float tmp377 = left1_4321_43right_f32_f32(water_sun_glint, tmp376);
            water_green = left1_4331_43right_f32_f32(water_green, tmp377);
            float tmp378 = 0.1599999964237213134765625;
            float tmp379 = left1_4321_43right_f32_f32(water_sun_glint, tmp378);
            water_blue = left1_4331_43right_f32_f32(water_blue, tmp379);
            float tmp380 = 232.0;
            float tmp381 = 376.0;
            float water_fog = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp380, tmp381, ray_distance);
            float tmp382 = 1.0;
            float tmp383 = left1_4351_43right_f32_f32(tmp382, water_fog);
            float tmp384 = left1_4321_43right_f32_f32(water_red, tmp383);
            float tmp385 = 0.17000000178813934326171875;
            float tmp386 = left1_4321_43right_f32_f32(tmp385, water_fog);
            red = left1_4331_43right_f32_f32(tmp384, tmp386);
            float tmp387 = 1.0;
            float tmp388 = left1_4351_43right_f32_f32(tmp387, water_fog);
            float tmp389 = left1_4321_43right_f32_f32(water_green, tmp388);
            float tmp390 = 0.23000000417232513427734375;
            float tmp391 = left1_4321_43right_f32_f32(tmp390, water_fog);
            green = left1_4331_43right_f32_f32(tmp389, tmp391);
            float tmp392 = 1.0;
            float tmp393 = left1_4351_43right_f32_f32(tmp392, water_fog);
            float tmp394 = left1_4321_43right_f32_f32(water_blue, tmp393);
            float tmp395 = 0.310000002384185791015625;
            float tmp396 = left1_4321_43right_f32_f32(tmp395, water_fog);
            blue = left1_4331_43right_f32_f32(tmp394, tmp396);
        }
        else
        {
            float tmp398 = 1.0;
            float slope = left1_4351_43right_f32_f32(tmp398, normal_y);
            float tmp399 = 0.680000007152557373046875;
            float tmp400 = left1_4321_43right_f32_f32(surface_variation, tmp399);
            float tmp401 = 0.319999992847442626953125;
            float tmp402 = left1_4321_43right_f32_f32(surface_detail, tmp401);
            float material_variation = left1_4331_43right_f32_f32(tmp400, tmp402);
            float tmp403 = 0.319999992847442626953125;
            float tmp404 = left1_4351_43right_f32_f32(surface_detail, tmp403);
            float tmp405 = 1.35000002384185791015625;
            float tmp406 = left1_4321_43right_f32_f32(tmp404, tmp405);
            float tmp407 = 1.7000000476837158203125;
            float tmp408 = left1_4321_43right_f32_f32(slope, tmp407);
            float tmp409 = left1_4331_43right_f32_f32(tmp406, tmp408);
            float fracture = saturate_number_f32(tmp409);
            float tmp410 = 0.054999999701976776123046875;
            float tmp411 = 0.300000011920928955078125;
            float tmp412 = 0.5;
            float tmp413 = left1_4351_43right_f32_f32(fracture, tmp412);
            float tmp414 = 0.0949999988079071044921875;
            float tmp415 = left1_4321_43right_f32_f32(tmp413, tmp414);
            float tmp416 = left1_4331_43right_f32_f32(slope, tmp415);
            float exposed_rock = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp410, tmp411, tmp416);
            float tmp417 = 2.75;
            float tmp418 = 4.44999980926513671875;
            float tmp419 = 0.5;
            float tmp420 = left1_4351_43right_f32_f32(material_variation, tmp419);
            float tmp421 = 0.4799999892711639404296875;
            float tmp422 = left1_4321_43right_f32_f32(tmp420, tmp421);
            float tmp423 = left1_4331_43right_f32_f32(world_y, tmp422);
            float snow = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp417, tmp418, tmp423);
            float tmp424 = 0.25;
            float tmp425 = 0.660000026226043701171875;
            float tmp426 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp424, tmp425, normal_y);
            snow = left1_4321_43right_f32_f32(snow, tmp426);
            float tmp427 = 0.87999999523162841796875;
            float tmp428 = 0.119999997317790985107421875;
            float tmp429 = left1_4321_43right_f32_f32(surface_detail, tmp428);
            float tmp430 = left1_4331_43right_f32_f32(tmp427, tmp429);
            snow = left1_4321_43right_f32_f32(snow, tmp430);
            float tmp431 = 0.3499999940395355224609375;
            float tmp432 = 2.650000095367431640625;
            float alpine_zone = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp431, tmp432, world_y);
            float tmp433 = 0.37999999523162841796875;
            float tmp434 = left1_4321_43right_f32_f32(normal_x, tmp433);
            float tmp435 = 0.2599999904632568359375;
            float tmp436 = left1_4321_43right_f32_f32(normal_z, tmp435);
            float tmp437 = left1_4351_43right_f32_f32(tmp434, tmp436);
            float tmp438 = 0.4799999892711639404296875;
            float tmp439 = left1_4331_43right_f32_f32(tmp437, tmp438);
            float rock_warmth = saturate_number_f32(tmp439);
            float tmp440 = 0.0280000008642673492431640625;
            float tmp441 = 0.0500000007450580596923828125;
            float tmp442 = left1_4321_43right_f32_f32(material_variation, tmp441);
            float tmp443 = left1_4331_43right_f32_f32(tmp440, tmp442);
            float tmp444 = 0.034000001847743988037109375;
            float tmp445 = left1_4321_43right_f32_f32(alpine_zone, tmp444);
            float meadow_red = left1_4331_43right_f32_f32(tmp443, tmp445);
            float tmp446 = 0.09399999678134918212890625;
            float tmp447 = 0.086000002920627593994140625;
            float tmp448 = left1_4321_43right_f32_f32(material_variation, tmp447);
            float tmp449 = left1_4331_43right_f32_f32(tmp446, tmp448);
            float tmp450 = 0.0500000007450580596923828125;
            float tmp451 = left1_4321_43right_f32_f32(alpine_zone, tmp450);
            float meadow_green = left1_4351_43right_f32_f32(tmp449, tmp451);
            float tmp452 = 0.0430000014603137969970703125;
            float tmp453 = 0.03599999845027923583984375;
            float tmp454 = left1_4321_43right_f32_f32(material_variation, tmp453);
            float tmp455 = left1_4331_43right_f32_f32(tmp452, tmp454);
            float tmp456 = 0.0199999995529651641845703125;
            float tmp457 = left1_4321_43right_f32_f32(alpine_zone, tmp456);
            float meadow_blue = left1_4331_43right_f32_f32(tmp455, tmp457);
            float tmp458 = 0.11900000274181365966796875;
            float tmp459 = 0.06599999964237213134765625;
            float tmp460 = left1_4321_43right_f32_f32(material_variation, tmp459);
            float tmp461 = left1_4331_43right_f32_f32(tmp458, tmp460);
            float tmp462 = 0.07400000095367431640625;
            float tmp463 = left1_4321_43right_f32_f32(rock_warmth, tmp462);
            float rock_red = left1_4331_43right_f32_f32(tmp461, tmp463);
            float tmp464 = 0.11200000345706939697265625;
            float tmp465 = 0.0579999983310699462890625;
            float tmp466 = left1_4321_43right_f32_f32(material_variation, tmp465);
            float tmp467 = left1_4331_43right_f32_f32(tmp464, tmp466);
            float tmp468 = 0.046999998390674591064453125;
            float tmp469 = left1_4321_43right_f32_f32(rock_warmth, tmp468);
            float rock_green = left1_4331_43right_f32_f32(tmp467, tmp469);
            float tmp470 = 0.1180000007152557373046875;
            float tmp471 = 0.0540000014007091522216796875;
            float tmp472 = left1_4321_43right_f32_f32(material_variation, tmp471);
            float tmp473 = left1_4331_43right_f32_f32(tmp470, tmp472);
            float tmp474 = 0.037000000476837158203125;
            float tmp475 = left1_4321_43right_f32_f32(rock_warmth, tmp474);
            float rock_blue = left1_4331_43right_f32_f32(tmp473, tmp475);
            float tmp476 = 1.0;
            float tmp477 = left1_4351_43right_f32_f32(tmp476, exposed_rock);
            float tmp478 = left1_4321_43right_f32_f32(meadow_red, tmp477);
            float tmp479 = left1_4321_43right_f32_f32(rock_red, exposed_rock);
            float ground_red = left1_4331_43right_f32_f32(tmp478, tmp479);
            float tmp480 = 1.0;
            float tmp481 = left1_4351_43right_f32_f32(tmp480, exposed_rock);
            float tmp482 = left1_4321_43right_f32_f32(meadow_green, tmp481);
            float tmp483 = left1_4321_43right_f32_f32(rock_green, exposed_rock);
            float ground_green = left1_4331_43right_f32_f32(tmp482, tmp483);
            float tmp484 = 1.0;
            float tmp485 = left1_4351_43right_f32_f32(tmp484, exposed_rock);
            float tmp486 = left1_4321_43right_f32_f32(meadow_blue, tmp485);
            float tmp487 = left1_4321_43right_f32_f32(rock_blue, exposed_rock);
            float ground_blue = left1_4331_43right_f32_f32(tmp486, tmp487);
            float tmp488 = 1.0;
            float tmp489 = left1_4351_43right_f32_f32(tmp488, snow);
            float tmp490 = left1_4321_43right_f32_f32(ground_red, tmp489);
            float tmp491 = 0.7799999713897705078125;
            float tmp492 = left1_4321_43right_f32_f32(tmp491, snow);
            ground_red = left1_4331_43right_f32_f32(tmp490, tmp492);
            float tmp493 = 1.0;
            float tmp494 = left1_4351_43right_f32_f32(tmp493, snow);
            float tmp495 = left1_4321_43right_f32_f32(ground_green, tmp494);
            float tmp496 = 0.829999983310699462890625;
            float tmp497 = left1_4321_43right_f32_f32(tmp496, snow);
            ground_green = left1_4331_43right_f32_f32(tmp495, tmp497);
            float tmp498 = 1.0;
            float tmp499 = left1_4351_43right_f32_f32(tmp498, snow);
            float tmp500 = left1_4321_43right_f32_f32(ground_blue, tmp499);
            float tmp501 = 0.87000000476837158203125;
            float tmp502 = left1_4321_43right_f32_f32(tmp501, snow);
            ground_blue = left1_4331_43right_f32_f32(tmp500, tmp502);
            float tmp503 = 0.800000011920928955078125;
            float tmp504 = 0.2599999904632568359375;
            float tmp505 = left1_4321_43right_f32_f32(surface_detail, tmp504);
            float material_light = left1_4331_43right_f32_f32(tmp503, tmp505);
            float tmp506 = 0.17000000178813934326171875;
            float tmp507 = 1.15999996662139892578125;
            float tmp508 = left1_4321_43right_f32_f32(sunlight, tmp507);
            float tmp509 = left1_4331_43right_f32_f32(tmp506, tmp508);
            float tmp510 = 0.17000000178813934326171875;
            float tmp511 = left1_4321_43right_f32_f32(sky_fill, tmp510);
            float tmp512 = left1_4331_43right_f32_f32(tmp509, tmp511);
            float lighting = left1_4321_43right_f32_f32(tmp512, material_light);
            float tmp513 = left1_4321_43right_f32_f32(ground_red, lighting);
            float tmp514 = 0.12999999523162841796875;
            float tmp515 = left1_4321_43right_f32_f32(sunlight, tmp514);
            ground_red = left1_4331_43right_f32_f32(tmp513, tmp515);
            float tmp516 = left1_4321_43right_f32_f32(ground_green, lighting);
            float tmp517 = 0.100000001490116119384765625;
            float tmp518 = left1_4321_43right_f32_f32(sunlight, tmp517);
            ground_green = left1_4331_43right_f32_f32(tmp516, tmp518);
            float tmp519 = left1_4321_43right_f32_f32(ground_blue, lighting);
            float tmp520 = 0.070000000298023223876953125;
            float tmp521 = left1_4321_43right_f32_f32(sunlight, tmp520);
            ground_blue = left1_4331_43right_f32_f32(tmp519, tmp521);
            float tmp522 = 188.0;
            float tmp523 = 376.0;
            float fog = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp522, tmp523, ray_distance);
            float tmp524 = 0.17000000178813934326171875;
            float tmp525 = 0.17000000178813934326171875;
            float tmp526 = left1_4321_43right_f32_f32(sun_halo, tmp525);
            float fog_red = left1_4331_43right_f32_f32(tmp524, tmp526);
            float tmp527 = 0.23000000417232513427734375;
            float tmp528 = 0.104999996721744537353515625;
            float tmp529 = left1_4321_43right_f32_f32(sun_halo, tmp528);
            float fog_green = left1_4331_43right_f32_f32(tmp527, tmp529);
            float tmp530 = 0.310000002384185791015625;
            float tmp531 = 0.05200000107288360595703125;
            float tmp532 = left1_4321_43right_f32_f32(sun_halo, tmp531);
            float fog_blue = left1_4331_43right_f32_f32(tmp530, tmp532);
            float tmp533 = 1.0;
            float tmp534 = left1_4351_43right_f32_f32(tmp533, fog);
            float tmp535 = left1_4321_43right_f32_f32(ground_red, tmp534);
            float tmp536 = left1_4321_43right_f32_f32(fog_red, fog);
            red = left1_4331_43right_f32_f32(tmp535, tmp536);
            float tmp537 = 1.0;
            float tmp538 = left1_4351_43right_f32_f32(tmp537, fog);
            float tmp539 = left1_4321_43right_f32_f32(ground_green, tmp538);
            float tmp540 = left1_4321_43right_f32_f32(fog_green, fog);
            green = left1_4331_43right_f32_f32(tmp539, tmp540);
            float tmp541 = 1.0;
            float tmp542 = left1_4351_43right_f32_f32(tmp541, fog);
            float tmp543 = left1_4321_43right_f32_f32(ground_blue, tmp542);
            float tmp544 = left1_4321_43right_f32_f32(fog_blue, fog);
            blue = left1_4331_43right_f32_f32(tmp543, tmp544);
        }
    }
    float tmp545 = left1_4371_43right_f32_f32(screen_x, aspect);
    float tmp546 = left1_4371_43right_f32_f32(screen_x, aspect);
    float tmp547 = left1_4321_43right_f32_f32(tmp545, tmp546);
    float tmp548 = left1_4321_43right_f32_f32(screen_y, screen_y);
    float tmp549 = left1_4331_43right_f32_f32(tmp547, tmp548);
    float vignette_radius = _the43_square_root_of_value_f32(tmp549);
    float tmp550 = 0.37999999523162841796875;
    float tmp551 = 1.0;
    float tmp552 = 0.579999983310699462890625;
    float tmp553 = 1.34000003337860107421875;
    float tmp554 = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp552, tmp553, vignette_radius);
    float tmp555 = left1_4351_43right_f32_f32(tmp551, tmp554);
    float tmp556 = 0.62000000476837158203125;
    float tmp557 = left1_4321_43right_f32_f32(tmp555, tmp556);
    float vignette = left1_4331_43right_f32_f32(tmp550, tmp557);
    float tmp558 = left1_4321_43right_f32_f32(red, vignette);
    float tmp559 = 1.0;
    float tmp560 = 0.37999999523162841796875;
    float tmp561 = left1_4321_43right_f32_f32(red, tmp560);
    float tmp562 = left1_4331_43right_f32_f32(tmp559, tmp561);
    float tmp563 = left1_4371_43right_f32_f32(tmp558, tmp562);
    red = _the43_square_root_of_value_f32(tmp563);
    float tmp564 = left1_4321_43right_f32_f32(green, vignette);
    float tmp565 = 1.0;
    float tmp566 = 0.37999999523162841796875;
    float tmp567 = left1_4321_43right_f32_f32(green, tmp566);
    float tmp568 = left1_4331_43right_f32_f32(tmp565, tmp567);
    float tmp569 = left1_4371_43right_f32_f32(tmp564, tmp568);
    green = _the43_square_root_of_value_f32(tmp569);
    float tmp570 = left1_4321_43right_f32_f32(blue, vignette);
    float tmp571 = 1.0;
    float tmp572 = 0.37999999523162841796875;
    float tmp573 = left1_4321_43right_f32_f32(blue, tmp572);
    float tmp574 = left1_4331_43right_f32_f32(tmp571, tmp573);
    float tmp575 = left1_4371_43right_f32_f32(tmp570, tmp574);
    blue = _the43_square_root_of_value_f32(tmp575);
    vec4 _1357 = vec4(0.0, 0.0, 0.0, 1.0);
    _1357.z = blue;
    _1357.y = green;
    _1357.x = red;
    dynlexColor = _1357;
}
