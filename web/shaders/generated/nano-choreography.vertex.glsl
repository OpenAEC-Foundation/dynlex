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
    float encoded_triangle_corner = in_Position.w;
    float tmp = 4.0;
    float tmp7 = left1_4371_43right_f32_f32(encoded_triangle_corner, tmp);
    float wheel_point = _the43_floor_of_value_f32(tmp7);
    float tmp8 = 4.0;
    float tmp9 = left1_4321_43right_f32_f32(wheel_point, tmp8);
    float triangle_corner = left1_4351_43right_f32_f32(encoded_triangle_corner, tmp9);
    float tmp10 = 4096.0;
    float tmp11 = left1_4371_43right_f32_f32(packed_x, tmp10);
    float target_quantized_x = _the43_floor_of_value_f32(tmp11);
    float tmp12 = 4096.0;
    float tmp13 = left1_4371_43right_f32_f32(packed_y, tmp12);
    float target_quantized_y = _the43_floor_of_value_f32(tmp13);
    float tmp14 = 4096.0;
    float tmp15 = left1_4371_43right_f32_f32(packed_z, tmp14);
    float target_quantized_z = _the43_floor_of_value_f32(tmp15);
    float tmp16 = 4096.0;
    float tmp17 = left1_4321_43right_f32_f32(target_quantized_x, tmp16);
    float motorcycle_quantized_x = left1_4351_43right_f32_f32(packed_x, tmp17);
    float tmp18 = 4096.0;
    float tmp19 = left1_4321_43right_f32_f32(target_quantized_y, tmp18);
    float motorcycle_quantized_y = left1_4351_43right_f32_f32(packed_y, tmp19);
    float tmp20 = 4096.0;
    float tmp21 = left1_4321_43right_f32_f32(target_quantized_z, tmp20);
    float motorcycle_quantized_z = left1_4351_43right_f32_f32(packed_z, tmp21);
    float tmp22 = 4095.0;
    float tmp23 = left1_4371_43right_f32_f32(target_quantized_x, tmp22);
    float tmp24 = 4.0;
    float tmp25 = left1_4321_43right_f32_f32(tmp23, tmp24);
    float tmp26 = 2.0;
    float point_x = left1_4351_43right_f32_f32(tmp25, tmp26);
    float tmp27 = 4095.0;
    float tmp28 = left1_4371_43right_f32_f32(target_quantized_y, tmp27);
    float tmp29 = 4.0;
    float tmp30 = left1_4321_43right_f32_f32(tmp28, tmp29);
    float tmp31 = 2.0;
    float point_y = left1_4351_43right_f32_f32(tmp30, tmp31);
    float tmp32 = 4095.0;
    float tmp33 = left1_4371_43right_f32_f32(target_quantized_z, tmp32);
    float tmp34 = 4.0;
    float tmp35 = left1_4321_43right_f32_f32(tmp33, tmp34);
    float tmp36 = 2.0;
    float point_z = left1_4351_43right_f32_f32(tmp35, tmp36);
    float tmp37 = 4095.0;
    float tmp38 = left1_4371_43right_f32_f32(motorcycle_quantized_x, tmp37);
    float tmp39 = 4.0;
    float tmp40 = left1_4321_43right_f32_f32(tmp38, tmp39);
    float tmp41 = 2.0;
    float motorcycle_local_x = left1_4351_43right_f32_f32(tmp40, tmp41);
    float tmp42 = 4095.0;
    float tmp43 = left1_4371_43right_f32_f32(motorcycle_quantized_y, tmp42);
    float tmp44 = 4.0;
    float tmp45 = left1_4321_43right_f32_f32(tmp43, tmp44);
    float tmp46 = 2.0;
    float motorcycle_local_y = left1_4351_43right_f32_f32(tmp45, tmp46);
    float tmp47 = 4095.0;
    float tmp48 = left1_4371_43right_f32_f32(motorcycle_quantized_z, tmp47);
    float tmp49 = 4.0;
    float tmp50 = left1_4321_43right_f32_f32(tmp48, tmp49);
    float tmp51 = 2.0;
    float motorcycle_local_z = left1_4351_43right_f32_f32(tmp50, tmp51);
    float time = dynlexUniform0.value;
    float tmp52 = dynlexUniform2.value;
    float tmp53 = 1.0;
    float width = _the43_maximum_of_a_and_b_f32_f32(tmp52, tmp53);
    float tmp54 = dynlexUniform3.value;
    float tmp55 = 1.0;
    float height = _the43_maximum_of_a_and_b_f32_f32(tmp54, tmp55);
    float render_pass = dynlexUniform1.value;
    float aspect = left1_4371_43right_f32_f32(width, height);
    float tmp56 = 10.3999996185302734375;
    float moment = _the43_minimum_of_a_and_b_f32_f32(time, tmp56);
    float tmp57 = 0.0;
    float tmp58 = 11.0;
    float visibility = scene_window_from_opening_to_closing_at_moment_f32_f32_f32(tmp57, tmp58, moment);
    float tmp59 = 0.0;
    float tmp60 = left1_4351_43right_f32_f32(tmp59, time);
    float tmp61 = 9.3999996185302734375;
    float motorcycle_wheel_spin = left1_4321_43right_f32_f32(tmp60, tmp61);
    float wheel_spin_sine = _the43_sine_of_value_f32(motorcycle_wheel_spin);
    float wheel_spin_cosine = _the43_cosine_of_value_f32(motorcycle_wheel_spin);
    float tmp62 = 0.7200000286102294921875;
    float wheel_center_x = _the_431negative_1of_34opposite_1of_3453value_f32(tmp62);
    float tmp63 = 0.0;
    if (left_2_right_f32_f32(motorcycle_local_x, tmp63))
    {
        wheel_center_x = 0.7200000286102294921875;
    }
    float wheel_offset_x = left1_4351_43right_f32_f32(motorcycle_local_x, wheel_center_x);
    float tmp64 = 0.4199999868869781494140625;
    float wheel_offset_y = left1_4331_43right_f32_f32(motorcycle_local_y, tmp64);
    float tmp65 = left1_4321_43right_f32_f32(wheel_offset_x, wheel_spin_cosine);
    float tmp66 = left1_4321_43right_f32_f32(wheel_offset_y, wheel_spin_sine);
    float wheel_rotated_x = left1_4351_43right_f32_f32(tmp65, tmp66);
    float tmp67 = left1_4321_43right_f32_f32(wheel_offset_x, wheel_spin_sine);
    float tmp68 = left1_4321_43right_f32_f32(wheel_offset_y, wheel_spin_cosine);
    float wheel_rotated_y = left1_4331_43right_f32_f32(tmp67, tmp68);
    float tmp71 = 0.5;
    if (left_2_right_f32_f32(wheel_point, tmp71))
    {
        motorcycle_local_x = left1_4331_43right_f32_f32(wheel_center_x, wheel_rotated_x);
        float tmp72 = 0.4199999868869781494140625;
        float tmp73 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp72);
        motorcycle_local_y = left1_4331_43right_f32_f32(tmp73, wheel_rotated_y);
    }
    float tmp74 = 0.0;
    float tmp75 = 4.25;
    float motorcycle_progress = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp74, tmp75, moment);
    float tmp76 = 1.2999999523162841796875;
    float tmp77 = 0.039999999105930328369140625;
    float tmp78 = left1_4321_43right_f32_f32(motorcycle_progress, tmp77);
    float motorcycle_yaw = left1_4331_43right_f32_f32(tmp76, tmp78);
    float motorcycle_yaw_sine = _the43_sine_of_value_f32(motorcycle_yaw);
    float motorcycle_yaw_cosine = _the43_cosine_of_value_f32(motorcycle_yaw);
    float tmp79 = left1_4321_43right_f32_f32(motorcycle_local_x, motorcycle_yaw_cosine);
    float tmp80 = left1_4321_43right_f32_f32(motorcycle_local_z, motorcycle_yaw_sine);
    float motorcycle_turned_x = left1_4331_43right_f32_f32(tmp79, tmp80);
    float tmp81 = left1_4321_43right_f32_f32(motorcycle_local_z, motorcycle_yaw_cosine);
    float tmp82 = left1_4321_43right_f32_f32(motorcycle_local_x, motorcycle_yaw_sine);
    float motorcycle_turned_z = left1_4351_43right_f32_f32(tmp81, tmp82);
    float tmp83 = 0.0;
    float tmp84 = 1.2599999904632568359375;
    float tmp85 = left1_4351_43right_f32_f32(tmp83, tmp84);
    float tmp86 = 1.17999994754791259765625;
    float tmp87 = left1_4321_43right_f32_f32(motorcycle_progress, tmp86);
    float tmp88 = left1_4331_43right_f32_f32(tmp85, tmp87);
    float motorcycle_world_x = left1_4331_43right_f32_f32(motorcycle_turned_x, tmp88);
    float tmp89 = 0.0;
    float tmp90 = 0.189999997615814208984375;
    float tmp91 = left1_4351_43right_f32_f32(tmp89, tmp90);
    float tmp92 = 0.119999997317790985107421875;
    float tmp93 = left1_4321_43right_f32_f32(motorcycle_progress, tmp92);
    float tmp94 = left1_4331_43right_f32_f32(tmp91, tmp93);
    float motorcycle_world_y = left1_4331_43right_f32_f32(motorcycle_local_y, tmp94);
    float tmp95 = 2.7999999523162841796875;
    float tmp96 = 4.900000095367431640625;
    float tmp97 = left1_4321_43right_f32_f32(motorcycle_progress, tmp96);
    float tmp98 = left1_4351_43right_f32_f32(tmp95, tmp97);
    float motorcycle_world_z = left1_4331_43right_f32_f32(motorcycle_turned_z, tmp98);
    float tmp99 = 4.19999980926513671875;
    float tmp100 = left1_4331_43right_f32_f32(motorcycle_world_z, tmp99);
    float tmp101 = 0.20000000298023223876953125;
    float motorcycle_depth = _the43_maximum_of_a_and_b_f32_f32(tmp100, tmp101);
    float tmp102 = 1.7200000286102294921875;
    float tmp103 = left1_4321_43right_f32_f32(motorcycle_world_x, tmp102);
    float tmp104 = 1.0;
    float tmp105 = _the43_maximum_of_a_and_b_f32_f32(aspect, tmp104);
    float tmp106 = left1_4321_43right_f32_f32(motorcycle_depth, tmp105);
    float motorcycle_ndc_x = left1_4371_43right_f32_f32(tmp103, tmp106);
    float tmp107 = 1.7200000286102294921875;
    float tmp108 = left1_4321_43right_f32_f32(motorcycle_world_y, tmp107);
    float motorcycle_ndc_y = left1_4371_43right_f32_f32(tmp108, motorcycle_depth);
    float tmp109 = 0.310000002384185791015625;
    float tmp110 = left1_4321_43right_f32_f32(time, tmp109);
    float tmp111 = _the43_sine_of_value_f32(tmp110);
    float tmp112 = 0.3400000035762786865234375;
    float target_yaw = left1_4321_43right_f32_f32(tmp111, tmp112);
    float target_yaw_sine = _the43_sine_of_value_f32(target_yaw);
    float target_yaw_cosine = _the43_cosine_of_value_f32(target_yaw);
    float tmp113 = left1_4321_43right_f32_f32(point_x, target_yaw_cosine);
    float tmp114 = left1_4321_43right_f32_f32(point_z, target_yaw_sine);
    float target_turned_x = left1_4331_43right_f32_f32(tmp113, tmp114);
    float tmp115 = left1_4321_43right_f32_f32(point_z, target_yaw_cosine);
    float tmp116 = left1_4321_43right_f32_f32(point_x, target_yaw_sine);
    float target_turned_z = left1_4351_43right_f32_f32(tmp115, tmp116);
    float tmp117 = 3.25;
    float tmp118 = 0.519999980926513671875;
    float tmp119 = left1_4321_43right_f32_f32(target_turned_z, tmp118);
    float target_depth = left1_4331_43right_f32_f32(tmp117, tmp119);
    float tmp120 = 1.65999996662139892578125;
    float tmp121 = 1.0;
    float tmp122 = _the43_maximum_of_a_and_b_f32_f32(aspect, tmp121);
    float horizontal_scale = left1_4371_43right_f32_f32(tmp120, tmp122);
    float tmp123 = left1_4321_43right_f32_f32(target_turned_x, horizontal_scale);
    float target_ndc_x = left1_4371_43right_f32_f32(tmp123, target_depth);
    float tmp124 = 1.86000001430511474609375;
    float tmp125 = left1_4321_43right_f32_f32(point_y, tmp124);
    float tmp126 = 0.039999999105930328369140625;
    float tmp127 = left1_4351_43right_f32_f32(tmp125, tmp126);
    float target_ndc_y = left1_4371_43right_f32_f32(tmp127, target_depth);
    float tmp128 = 3.13000011444091796875;
    float tmp129 = left1_4321_43right_f32_f32(point_x, tmp128);
    float tmp130 = 2.71000003814697265625;
    float tmp131 = left1_4321_43right_f32_f32(point_y, tmp130);
    float tmp132 = left1_4331_43right_f32_f32(tmp129, tmp131);
    float tmp133 = 4.190000057220458984375;
    float tmp134 = left1_4321_43right_f32_f32(point_z, tmp133);
    float tmp135 = left1_4331_43right_f32_f32(tmp132, tmp134);
    float tmp136 = _the43_sine_of_value_f32(tmp135);
    float tmp137 = 0.5;
    float tmp138 = left1_4321_43right_f32_f32(tmp136, tmp137);
    float tmp139 = 0.5;
    float drone_seed_a = left1_4331_43right_f32_f32(tmp138, tmp139);
    float tmp140 = 2.1700000762939453125;
    float tmp141 = _the_431negative_1of_34opposite_1of_3453value_f32(tmp140);
    float tmp142 = left1_4321_43right_f32_f32(point_x, tmp141);
    float tmp143 = 3.9100000858306884765625;
    float tmp144 = left1_4321_43right_f32_f32(point_y, tmp143);
    float tmp145 = left1_4331_43right_f32_f32(tmp142, tmp144);
    float tmp146 = 2.4300000667572021484375;
    float tmp147 = left1_4321_43right_f32_f32(point_z, tmp146);
    float tmp148 = left1_4331_43right_f32_f32(tmp145, tmp147);
    float tmp149 = _the43_sine_of_value_f32(tmp148);
    float tmp150 = 0.5;
    float tmp151 = left1_4321_43right_f32_f32(tmp149, tmp150);
    float tmp152 = 0.5;
    float drone_seed_b = left1_4331_43right_f32_f32(tmp151, tmp152);
    float tmp153 = 0.75;
    float drone_delay = left1_4321_43right_f32_f32(drone_seed_a, tmp153);
    float tmp154 = 4.44999980926513671875;
    float tmp155 = left1_4331_43right_f32_f32(tmp154, drone_delay);
    float tmp156 = 6.650000095367431640625;
    float tmp157 = left1_4331_43right_f32_f32(tmp156, drone_delay);
    float assembly_progress = smooth_transition_from_lower_to_upper_at_sample_f32_f32_f32(tmp155, tmp157, moment);
    float tmp158 = 3.1415927410125732421875;
    float tmp159 = left1_4321_43right_f32_f32(assembly_progress, tmp158);
    float tmp160 = _the43_sine_of_value_f32(tmp159);
    float tmp161 = 0.100000001490116119384765625;
    float tmp162 = 0.23999999463558197021484375;
    float tmp163 = left1_4321_43right_f32_f32(drone_seed_b, tmp162);
    float tmp164 = left1_4331_43right_f32_f32(tmp161, tmp163);
    float flight_arc = left1_4321_43right_f32_f32(tmp160, tmp164);
    float tmp165 = 6.283185482025146484375;
    float tmp166 = left1_4321_43right_f32_f32(drone_seed_b, tmp165);
    float tmp167 = 3.2000000476837158203125;
    float tmp168 = left1_4321_43right_f32_f32(assembly_progress, tmp167);
    float flight_phase = left1_4331_43right_f32_f32(tmp166, tmp168);
    float tmp169 = 3.1415927410125732421875;
    float tmp170 = left1_4321_43right_f32_f32(assembly_progress, tmp169);
    float tmp171 = _the43_sine_of_value_f32(tmp170);
    float tmp172 = 0.119999997317790985107421875;
    float flight_sweep = left1_4321_43right_f32_f32(tmp171, tmp172);
    float tmp173 = _the43_cosine_of_value_f32(flight_phase);
    float tmp174 = left1_4321_43right_f32_f32(tmp173, flight_arc);
    float flight_x = left1_4331_43right_f32_f32(tmp174, flight_sweep);
    float tmp175 = _the43_sine_of_value_f32(flight_phase);
    float tmp176 = left1_4321_43right_f32_f32(tmp175, flight_arc);
    float tmp177 = 0.7200000286102294921875;
    float flight_y = left1_4321_43right_f32_f32(tmp176, tmp177);
    float tmp178 = left1_4351_43right_f32_f32(target_ndc_x, motorcycle_ndc_x);
    float tmp179 = left1_4321_43right_f32_f32(tmp178, assembly_progress);
    float tmp180 = left1_4331_43right_f32_f32(motorcycle_ndc_x, tmp179);
    float assembled_ndc_x = left1_4331_43right_f32_f32(tmp180, flight_x);
    float tmp181 = left1_4351_43right_f32_f32(target_ndc_y, motorcycle_ndc_y);
    float tmp182 = left1_4321_43right_f32_f32(tmp181, assembly_progress);
    float tmp183 = left1_4331_43right_f32_f32(motorcycle_ndc_y, tmp182);
    float assembled_ndc_y = left1_4331_43right_f32_f32(tmp183, flight_y);
    float tmp184 = left1_4351_43right_f32_f32(target_depth, motorcycle_depth);
    float tmp185 = left1_4321_43right_f32_f32(tmp184, assembly_progress);
    float depth = left1_4331_43right_f32_f32(motorcycle_depth, tmp185);
    float tmp186 = 0.0010400000028312206268310546875;
    float tmp187 = 0.959999978542327880859375;
    float tmp188 = 0.039999999105930328369140625;
    float tmp189 = left1_4321_43right_f32_f32(render_pass, tmp188);
    float tmp190 = left1_4331_43right_f32_f32(tmp187, tmp189);
    float relative_point_size = left1_4321_43right_f32_f32(tmp186, tmp190);
    float point_size_x = left1_4371_43right_f32_f32(relative_point_size, aspect);
    float point_size_y = relative_point_size;
    float tmp191 = 0.0;
    float corner_x = left1_4351_43right_f32_f32(tmp191, point_size_x);
    float tmp192 = 0.0;
    float corner_y = left1_4351_43right_f32_f32(tmp192, point_size_y);
    float tmp195 = 1.0;
    if (left_1is_greater_than_or_equal_to4213_right_f32_f32(triangle_corner, tmp195))
    {
        corner_x = point_size_x;
    }
    float tmp198 = 2.0;
    if (left_1is_greater_than_or_equal_to4213_right_f32_f32(triangle_corner, tmp198))
    {
        corner_x = 0.0;
        float tmp199 = 1.4500000476837158203125;
        corner_y = left1_4321_43right_f32_f32(point_size_y, tmp199);
    }
    float tmp200 = left1_4331_43right_f32_f32(assembled_ndc_x, corner_x);
    float tmp201 = left1_4331_43right_f32_f32(assembled_ndc_y, corner_y);
    float tmp204 = 0.001000000047497451305389404296875;
    float _553 = 0.0;
    if (left_0_right_f32_f32(visibility, tmp204))
    {
        float tmp205 = 3.0;
        _553 = left1_4321_43right_f32_f32(depth, tmp205);
    }
    else
    {
        _553 = left1_4321_43right_f32_f32(tmp200, depth);
    }
    float tmp206 = 0.4199999868869781494140625;
    vec4 _556 = vec4(0.0);
    _556.w = depth;
    _556.z = left1_4321_43right_f32_f32(depth, tmp206);
    _556.y = left1_4321_43right_f32_f32(tmp201, depth);
    _556.x = _553;
    gl_Position = _556;
}
