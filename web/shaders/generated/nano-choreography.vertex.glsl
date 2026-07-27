#version 300 es

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

layout(location = 0) in vec4 in_Position;

float left1_4371_43right_f32_f32(float left, float right)
{
    return left / right;
}

float _the43_floor_of_value_f32(float value)
{
    return floor(value);
}

float left1_4321_43right_f32_f32(float left, float right)
{
    return left * right;
}

float left1_4351_43right_f32_f32(float left, float right)
{
    return left - right;
}

float _the43_maximum_of_a_and_b_f32_f32(float a, float b)
{
    return isnan(b) ? a : (isnan(a) ? b : max(a, b));
}

float _the43_minimum_of_a_and_b_f32_f32(float a, float b)
{
    return isnan(b) ? a : (isnan(a) ? b : min(a, b));
}

float left1_4331_43right_f32_f32(float left, float right)
{
    return left + right;
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

float _the_431negative_1of_34opposite_1of_3453value_f32(float value)
{
    return -value;
}

bool left_1is_greater_than_or_equal_to4213_right_f32_f32(float left, float right)
{
    return left >= right;
}

void main()
{
    float packed_x = in_Position.x;
    float packed_y = in_Position.y;
    float packed_z = in_Position.z;
    float triangle_corner = in_Position.w;
    float tmp = 4096.0;
    float tmp7 = left1_4371_43right_f32_f32(packed_x, tmp);
    float target_quantized_x = _the43_floor_of_value_f32(tmp7);
    float tmp8 = 4096.0;
    float tmp9 = left1_4371_43right_f32_f32(packed_y, tmp8);
    float target_quantized_y = _the43_floor_of_value_f32(tmp9);
    float tmp10 = 4096.0;
    float tmp11 = left1_4371_43right_f32_f32(packed_z, tmp10);
    float target_quantized_z = _the43_floor_of_value_f32(tmp11);
    float tmp12 = 4096.0;
    float tmp13 = left1_4321_43right_f32_f32(target_quantized_x, tmp12);
    float motorcycle_quantized_x = left1_4351_43right_f32_f32(packed_x, tmp13);
    float tmp14 = 4096.0;
    float tmp15 = left1_4321_43right_f32_f32(target_quantized_y, tmp14);
    float motorcycle_quantized_y = left1_4351_43right_f32_f32(packed_y, tmp15);
    float tmp16 = 4096.0;
    float tmp17 = left1_4321_43right_f32_f32(target_quantized_z, tmp16);
    float motorcycle_quantized_z = left1_4351_43right_f32_f32(packed_z, tmp17);
    float tmp18 = 4095.0;
    float tmp19 = left1_4371_43right_f32_f32(target_quantized_x, tmp18);
    float tmp20 = 4.0;
    float tmp21 = left1_4321_43right_f32_f32(tmp19, tmp20);
    float tmp22 = 2.0;
    float point_x = left1_4351_43right_f32_f32(tmp21, tmp22);
    float tmp23 = 4095.0;
    float tmp24 = left1_4371_43right_f32_f32(target_quantized_y, tmp23);
    float tmp25 = 4.0;
    float tmp26 = left1_4321_43right_f32_f32(tmp24, tmp25);
    float tmp27 = 2.0;
    float point_y = left1_4351_43right_f32_f32(tmp26, tmp27);
    float tmp28 = 4095.0;
    float tmp29 = left1_4371_43right_f32_f32(target_quantized_z, tmp28);
    float tmp30 = 4.0;
    float tmp31 = left1_4321_43right_f32_f32(tmp29, tmp30);
    float tmp32 = 2.0;
    float point_z = left1_4351_43right_f32_f32(tmp31, tmp32);
    float tmp33 = 4095.0;
    float tmp34 = left1_4371_43right_f32_f32(motorcycle_quantized_x, tmp33);
    float tmp35 = 4.0;
    float tmp36 = left1_4321_43right_f32_f32(tmp34, tmp35);
    float tmp37 = 2.0;
    float motorcycle_local_x = left1_4351_43right_f32_f32(tmp36, tmp37);
    float tmp38 = 4095.0;
    float tmp39 = left1_4371_43right_f32_f32(motorcycle_quantized_y, tmp38);
    float tmp40 = 4.0;
    float tmp41 = left1_4321_43right_f32_f32(tmp39, tmp40);
    float tmp42 = 2.0;
    float motorcycle_local_y = left1_4351_43right_f32_f32(tmp41, tmp42);
    float tmp43 = 4095.0;
    float tmp44 = left1_4371_43right_f32_f32(motorcycle_quantized_z, tmp43);
    float tmp45 = 4.0;
    float tmp46 = left1_4321_43right_f32_f32(tmp44, tmp45);
    float tmp47 = 2.0;
    float motorcycle_local_z = left1_4351_43right_f32_f32(tmp46, tmp47);
    float time = dynlexUniform0.value;
    float tmp48 = dynlexUniform2.value;
    float tmp49 = 1.0;
    float width = _the43_maximum_of_a_and_b_f32_f32(tmp48, tmp49);
    float tmp50 = dynlexUniform3.value;
    float tmp51 = 1.0;
    float height = _the43_maximum_of_a_and_b_f32_f32(tmp50, tmp51);
    float render_pass = dynlexUniform1.value;
    float aspect = left1_4371_43right_f32_f32(width, height);
    float tmp52 = 10.3999996185302734375;
    float moment = _the43_minimum_of_a_and_b_f32_f32(time, tmp52);
    float tmp53 = 0.0;
    float tmp54 = 11.0;
    float visibility = scene_window_from_opening_to_closing_at_moment_f32_f32_f32(tmp53, tmp54, moment);
    float tmp55 = 0.0;
    float tmp56 = 4.25;
    float motorcycle_progress = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp55, tmp56, moment);
    float tmp57 = 0.0;
    float tmp58 = 1.17999994754791259765625;
    float tmp59 = left1_4351_43right_f32_f32(tmp57, tmp58);
    float tmp60 = 0.07999999821186065673828125;
    float tmp61 = left1_4321_43right_f32_f32(motorcycle_progress, tmp60);
    float motorcycle_yaw = left1_4331_43right_f32_f32(tmp59, tmp61);
    float motorcycle_yaw_sine = _the43_sine_of_value_f32(motorcycle_yaw);
    float motorcycle_yaw_cosine = _the43_cosine_of_value_f32(motorcycle_yaw);
    float tmp62 = left1_4321_43right_f32_f32(motorcycle_local_x, motorcycle_yaw_cosine);
    float tmp63 = left1_4321_43right_f32_f32(motorcycle_local_z, motorcycle_yaw_sine);
    float motorcycle_turned_x = left1_4331_43right_f32_f32(tmp62, tmp63);
    float tmp64 = left1_4321_43right_f32_f32(motorcycle_local_z, motorcycle_yaw_cosine);
    float tmp65 = left1_4321_43right_f32_f32(motorcycle_local_x, motorcycle_yaw_sine);
    float motorcycle_turned_z = left1_4351_43right_f32_f32(tmp64, tmp65);
    float tmp66 = 0.0;
    float tmp67 = 1.2599999904632568359375;
    float tmp68 = left1_4351_43right_f32_f32(tmp66, tmp67);
    float tmp69 = 1.17999994754791259765625;
    float tmp70 = left1_4321_43right_f32_f32(motorcycle_progress, tmp69);
    float tmp71 = left1_4331_43right_f32_f32(tmp68, tmp70);
    float motorcycle_world_x = left1_4331_43right_f32_f32(motorcycle_turned_x, tmp71);
    float tmp72 = 0.0;
    float tmp73 = 0.189999997615814208984375;
    float tmp74 = left1_4351_43right_f32_f32(tmp72, tmp73);
    float tmp75 = 0.119999997317790985107421875;
    float tmp76 = left1_4321_43right_f32_f32(motorcycle_progress, tmp75);
    float tmp77 = left1_4331_43right_f32_f32(tmp74, tmp76);
    float motorcycle_world_y = left1_4331_43right_f32_f32(motorcycle_local_y, tmp77);
    float tmp78 = 2.7999999523162841796875;
    float tmp79 = 4.900000095367431640625;
    float tmp80 = left1_4321_43right_f32_f32(motorcycle_progress, tmp79);
    float tmp81 = left1_4351_43right_f32_f32(tmp78, tmp80);
    float motorcycle_world_z = left1_4331_43right_f32_f32(motorcycle_turned_z, tmp81);
    float tmp82 = 4.19999980926513671875;
    float tmp83 = left1_4331_43right_f32_f32(motorcycle_world_z, tmp82);
    float tmp84 = 0.20000000298023223876953125;
    float motorcycle_depth = _the43_maximum_of_a_and_b_f32_f32(tmp83, tmp84);
    float tmp85 = 1.7200000286102294921875;
    float tmp86 = left1_4321_43right_f32_f32(motorcycle_world_x, tmp85);
    float tmp87 = 1.0;
    float tmp88 = _the43_maximum_of_a_and_b_f32_f32(aspect, tmp87);
    float tmp89 = left1_4321_43right_f32_f32(motorcycle_depth, tmp88);
    float motorcycle_ndc_x = left1_4371_43right_f32_f32(tmp86, tmp89);
    float tmp90 = 1.7200000286102294921875;
    float tmp91 = left1_4321_43right_f32_f32(motorcycle_world_y, tmp90);
    float motorcycle_ndc_y = left1_4371_43right_f32_f32(tmp91, motorcycle_depth);
    float tmp92 = 0.310000002384185791015625;
    float tmp93 = left1_4321_43right_f32_f32(time, tmp92);
    float tmp94 = _the43_sine_of_value_f32(tmp93);
    float tmp95 = 0.3400000035762786865234375;
    float target_yaw = left1_4321_43right_f32_f32(tmp94, tmp95);
    float target_yaw_sine = _the43_sine_of_value_f32(target_yaw);
    float target_yaw_cosine = _the43_cosine_of_value_f32(target_yaw);
    float tmp96 = left1_4321_43right_f32_f32(point_x, target_yaw_cosine);
    float tmp97 = left1_4321_43right_f32_f32(point_z, target_yaw_sine);
    float target_turned_x = left1_4331_43right_f32_f32(tmp96, tmp97);
    float tmp98 = left1_4321_43right_f32_f32(point_z, target_yaw_cosine);
    float tmp99 = left1_4321_43right_f32_f32(point_x, target_yaw_sine);
    float target_turned_z = left1_4351_43right_f32_f32(tmp98, tmp99);
    float tmp100 = 3.25;
    float tmp101 = 0.519999980926513671875;
    float tmp102 = left1_4321_43right_f32_f32(target_turned_z, tmp101);
    float target_depth = left1_4331_43right_f32_f32(tmp100, tmp102);
    float tmp103 = 1.65999996662139892578125;
    float tmp104 = 1.0;
    float tmp105 = _the43_maximum_of_a_and_b_f32_f32(aspect, tmp104);
    float horizontal_scale = left1_4371_43right_f32_f32(tmp103, tmp105);
    float tmp106 = left1_4321_43right_f32_f32(target_turned_x, horizontal_scale);
    float target_ndc_x = left1_4371_43right_f32_f32(tmp106, target_depth);
    float tmp107 = 1.86000001430511474609375;
    float tmp108 = left1_4321_43right_f32_f32(point_y, tmp107);
    float tmp109 = 0.039999999105930328369140625;
    float tmp110 = left1_4351_43right_f32_f32(tmp108, tmp109);
    float target_ndc_y = left1_4371_43right_f32_f32(tmp110, target_depth);
    float tmp111 = 3.13000011444091796875;
    float tmp112 = left1_4321_43right_f32_f32(point_x, tmp111);
    float tmp113 = 2.71000003814697265625;
    float tmp114 = left1_4321_43right_f32_f32(point_y, tmp113);
    float tmp115 = left1_4331_43right_f32_f32(tmp112, tmp114);
    float tmp116 = 4.190000057220458984375;
    float tmp117 = left1_4321_43right_f32_f32(point_z, tmp116);
    float tmp118 = left1_4331_43right_f32_f32(tmp115, tmp117);
    float tmp119 = _the43_sine_of_value_f32(tmp118);
    float tmp120 = 0.5;
    float tmp121 = left1_4321_43right_f32_f32(tmp119, tmp120);
    float tmp122 = 0.5;
    float drone_seed_a = left1_4331_43right_f32_f32(tmp121, tmp122);
    float tmp123 = 2.1700000762939453125;
    float tmp124 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp123);
    float tmp125 = left1_4321_43right_f32_f32(point_x, tmp124);
    float tmp126 = 3.9100000858306884765625;
    float tmp127 = left1_4321_43right_f32_f32(point_y, tmp126);
    float tmp128 = left1_4331_43right_f32_f32(tmp125, tmp127);
    float tmp129 = 2.4300000667572021484375;
    float tmp130 = left1_4321_43right_f32_f32(point_z, tmp129);
    float tmp131 = left1_4331_43right_f32_f32(tmp128, tmp130);
    float tmp132 = _the43_sine_of_value_f32(tmp131);
    float tmp133 = 0.5;
    float tmp134 = left1_4321_43right_f32_f32(tmp132, tmp133);
    float tmp135 = 0.5;
    float drone_seed_b = left1_4331_43right_f32_f32(tmp134, tmp135);
    float tmp136 = 0.75;
    float drone_delay = left1_4321_43right_f32_f32(drone_seed_a, tmp136);
    float tmp137 = 4.44999980926513671875;
    float tmp138 = left1_4331_43right_f32_f32(tmp137, drone_delay);
    float tmp139 = 6.650000095367431640625;
    float tmp140 = left1_4331_43right_f32_f32(tmp139, drone_delay);
    float assembly_progress = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp138, tmp140, moment);
    float tmp141 = 3.1415927410125732421875;
    float tmp142 = left1_4321_43right_f32_f32(assembly_progress, tmp141);
    float tmp143 = _the43_sine_of_value_f32(tmp142);
    float tmp144 = 0.100000001490116119384765625;
    float tmp145 = 0.23999999463558197021484375;
    float tmp146 = left1_4321_43right_f32_f32(drone_seed_b, tmp145);
    float tmp147 = left1_4331_43right_f32_f32(tmp144, tmp146);
    float flight_arc = left1_4321_43right_f32_f32(tmp143, tmp147);
    float tmp148 = 6.283185482025146484375;
    float tmp149 = left1_4321_43right_f32_f32(drone_seed_b, tmp148);
    float tmp150 = 3.2000000476837158203125;
    float tmp151 = left1_4321_43right_f32_f32(assembly_progress, tmp150);
    float flight_phase = left1_4331_43right_f32_f32(tmp149, tmp151);
    float tmp152 = 3.1415927410125732421875;
    float tmp153 = left1_4321_43right_f32_f32(assembly_progress, tmp152);
    float tmp154 = _the43_sine_of_value_f32(tmp153);
    float tmp155 = 0.119999997317790985107421875;
    float flight_sweep = left1_4321_43right_f32_f32(tmp154, tmp155);
    float tmp156 = _the43_cosine_of_value_f32(flight_phase);
    float tmp157 = left1_4321_43right_f32_f32(tmp156, flight_arc);
    float flight_x = left1_4331_43right_f32_f32(tmp157, flight_sweep);
    float tmp158 = _the43_sine_of_value_f32(flight_phase);
    float tmp159 = left1_4321_43right_f32_f32(tmp158, flight_arc);
    float tmp160 = 0.7200000286102294921875;
    float flight_y = left1_4321_43right_f32_f32(tmp159, tmp160);
    float tmp161 = left1_4351_43right_f32_f32(target_ndc_x, motorcycle_ndc_x);
    float tmp162 = left1_4321_43right_f32_f32(tmp161, assembly_progress);
    float tmp163 = left1_4331_43right_f32_f32(motorcycle_ndc_x, tmp162);
    float assembled_ndc_x = left1_4331_43right_f32_f32(tmp163, flight_x);
    float tmp164 = left1_4351_43right_f32_f32(target_ndc_y, motorcycle_ndc_y);
    float tmp165 = left1_4321_43right_f32_f32(tmp164, assembly_progress);
    float tmp166 = left1_4331_43right_f32_f32(motorcycle_ndc_y, tmp165);
    float assembled_ndc_y = left1_4331_43right_f32_f32(tmp166, flight_y);
    float tmp167 = left1_4351_43right_f32_f32(target_depth, motorcycle_depth);
    float tmp168 = left1_4321_43right_f32_f32(tmp167, assembly_progress);
    float depth = left1_4331_43right_f32_f32(motorcycle_depth, tmp168);
    float tmp169 = 0.0010400000028312206268310546875;
    float tmp170 = 0.959999978542327880859375;
    float tmp171 = 0.039999999105930328369140625;
    float tmp172 = left1_4321_43right_f32_f32(render_pass, tmp171);
    float tmp173 = left1_4331_43right_f32_f32(tmp170, tmp172);
    float relative_point_size = left1_4321_43right_f32_f32(tmp169, tmp173);
    float point_size_x = left1_4371_43right_f32_f32(relative_point_size, aspect);
    float point_size_y = relative_point_size;
    float tmp174 = 0.0;
    float corner_x = left1_4351_43right_f32_f32(tmp174, point_size_x);
    float tmp175 = 0.0;
    float corner_y = left1_4351_43right_f32_f32(tmp175, point_size_y);
    float tmp176 = 1.0;
    if (left_1is_greater_than_or_equal_to4213_right_f32_f32(triangle_corner, tmp176))
    {
        corner_x = point_size_x;
    }
    float tmp179 = 2.0;
    if (left_1is_greater_than_or_equal_to4213_right_f32_f32(triangle_corner, tmp179))
    {
        corner_x = 0.0;
        float tmp180 = 1.4500000476837158203125;
        corner_y = left1_4321_43right_f32_f32(point_size_y, tmp180);
    }
    float tmp181 = left1_4331_43right_f32_f32(assembled_ndc_x, corner_x);
    float tmp182 = left1_4331_43right_f32_f32(assembled_ndc_y, corner_y);
    float tmp185 = 0.001000000047497451305389404296875;
    float _502 = 0.0;
    if (left_0_right_f32_f32(visibility, tmp185))
    {
        float tmp186 = 3.0;
        _502 = left1_4321_43right_f32_f32(depth, tmp186);
    }
    else
    {
        _502 = left1_4321_43right_f32_f32(tmp181, depth);
    }
    float tmp187 = 0.4199999868869781494140625;
    vec4 _505 = vec4(0.0);
    _505.w = depth;
    _505.z = left1_4321_43right_f32_f32(depth, tmp187);
    _505.y = left1_4321_43right_f32_f32(tmp182, depth);
    _505.x = _502;
    gl_Position = _505;
}
